/*
 *  i386 CPUID helper functions
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
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "sysemu/cpus.h"
#include "sysemu/sysemu.h"
#include "sysemu/tcg.h"
#include "hw/i386/topology.h"

#include "uc_priv.h"

static void x86_cpuid_version_set_family(X86CPU *cpu, int64_t value);
static void x86_cpuid_version_set_model(X86CPU *cpu, int64_t value);
static void x86_cpuid_version_set_stepping(X86CPU *cpu, int64_t value);
static void x86_cpuid_set_model_id(X86CPU *cpu, const char* model_id);
static void x86_cpuid_set_vendor(X86CPU *cpu , const char *value);

/* Helpers for building CPUID[2] descriptors: */

struct CPUID2CacheDescriptorInfo {
    enum CacheType type;
    int level;
    int size;
    int line_size;
    int associativity;
};

/*
 * Known CPUID 2 cache descriptors.
 * From Intel SDM Volume 2A, CPUID instruction
 */
static struct CPUID2CacheDescriptorInfo cpuid2_cache_descriptors[] = {
    [0x06] = { .level = 1, .type = INSTRUCTION_CACHE, .size =   8 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x08] = { .level = 1, .type = INSTRUCTION_CACHE, .size =  16 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x09] = { .level = 1, .type = INSTRUCTION_CACHE, .size =  32 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x0A] = { .level = 1, .type = DATA_CACHE,        .size =   8 * KiB,
               .associativity = 2,  .line_size = 32, },
    [0x0C] = { .level = 1, .type = DATA_CACHE,        .size =  16 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x0D] = { .level = 1, .type = DATA_CACHE,        .size =  16 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x0E] = { .level = 1, .type = DATA_CACHE,        .size =  24 * KiB,
               .associativity = 6,  .line_size = 64, },
    [0x1D] = { .level = 2, .type = UNIFIED_CACHE,     .size = 128 * KiB,
               .associativity = 2,  .line_size = 64, },
    [0x21] = { .level = 2, .type = UNIFIED_CACHE,     .size = 256 * KiB,
               .associativity = 8,  .line_size = 64, },
    /* lines per sector is not supported cpuid2_cache_descriptor(),
    * so descriptors 0x22, 0x23 are not included
    */
    [0x24] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 16, .line_size = 64, },
    /* lines per sector is not supported cpuid2_cache_descriptor(),
    * so descriptors 0x25, 0x20 are not included
    */
    [0x2C] = { .level = 1, .type = DATA_CACHE,        .size =  32 * KiB,
               .associativity = 8,  .line_size = 64, },
    [0x30] = { .level = 1, .type = INSTRUCTION_CACHE, .size =  32 * KiB,
               .associativity = 8,  .line_size = 64, },
    [0x41] = { .level = 2, .type = UNIFIED_CACHE,     .size = 128 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x42] = { .level = 2, .type = UNIFIED_CACHE,     .size = 256 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x43] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x44] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 4,  .line_size = 32, },
    [0x45] = { .level = 2, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 4,  .line_size = 32, },
    [0x46] = { .level = 3, .type = UNIFIED_CACHE,     .size =   4 * MiB,
               .associativity = 4,  .line_size = 64, },
    [0x47] = { .level = 3, .type = UNIFIED_CACHE,     .size =   8 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0x48] = { .level = 2, .type = UNIFIED_CACHE,     .size =   3 * MiB,
               .associativity = 12, .line_size = 64, },
    /* Descriptor 0x49 depends on CPU family/model, so it is not included */
    [0x4A] = { .level = 3, .type = UNIFIED_CACHE,     .size =   6 * MiB,
               .associativity = 12, .line_size = 64, },
    [0x4B] = { .level = 3, .type = UNIFIED_CACHE,     .size =   8 * MiB,
               .associativity = 16, .line_size = 64, },
    [0x4C] = { .level = 3, .type = UNIFIED_CACHE,     .size =  12 * MiB,
               .associativity = 12, .line_size = 64, },
    [0x4D] = { .level = 3, .type = UNIFIED_CACHE,     .size =  16 * MiB,
               .associativity = 16, .line_size = 64, },
    [0x4E] = { .level = 2, .type = UNIFIED_CACHE,     .size =   6 * MiB,
               .associativity = 24, .line_size = 64, },
    [0x60] = { .level = 1, .type = DATA_CACHE,        .size =  16 * KiB,
               .associativity = 8,  .line_size = 64, },
    [0x66] = { .level = 1, .type = DATA_CACHE,        .size =   8 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x67] = { .level = 1, .type = DATA_CACHE,        .size =  16 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x68] = { .level = 1, .type = DATA_CACHE,        .size =  32 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x78] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 4,  .line_size = 64, },
    /* lines per sector is not supported cpuid2_cache_descriptor(),
    * so descriptors 0x79, 0x7A, 0x7B, 0x7C are not included.
    */
    [0x7D] = { .level = 2, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0x7F] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 2,  .line_size = 64, },
    [0x80] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 8,  .line_size = 64, },
    [0x82] = { .level = 2, .type = UNIFIED_CACHE,     .size = 256 * KiB,
               .associativity = 8,  .line_size = 32, },
    [0x83] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 8,  .line_size = 32, },
    [0x84] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 8,  .line_size = 32, },
    [0x85] = { .level = 2, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 8,  .line_size = 32, },
    [0x86] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x87] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0xD0] = { .level = 3, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0xD1] = { .level = 3, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 4,  .line_size = 64, },
    [0xD2] = { .level = 3, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 4,  .line_size = 64, },
    [0xD6] = { .level = 3, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0xD7] = { .level = 3, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0xD8] = { .level = 3, .type = UNIFIED_CACHE,     .size =   4 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0xDC] = { .level = 3, .type = UNIFIED_CACHE,     .size = 1.5 * MiB,
               .associativity = 12, .line_size = 64, },
    [0xDD] = { .level = 3, .type = UNIFIED_CACHE,     .size =   3 * MiB,
               .associativity = 12, .line_size = 64, },
    [0xDE] = { .level = 3, .type = UNIFIED_CACHE,     .size =   6 * MiB,
               .associativity = 12, .line_size = 64, },
    [0xE2] = { .level = 3, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 16, .line_size = 64, },
    [0xE3] = { .level = 3, .type = UNIFIED_CACHE,     .size =   4 * MiB,
               .associativity = 16, .line_size = 64, },
    [0xE4] = { .level = 3, .type = UNIFIED_CACHE,     .size =   8 * MiB,
               .associativity = 16, .line_size = 64, },
    [0xEA] = { .level = 3, .type = UNIFIED_CACHE,     .size =  12 * MiB,
               .associativity = 24, .line_size = 64, },
    [0xEB] = { .level = 3, .type = UNIFIED_CACHE,     .size =  18 * MiB,
               .associativity = 24, .line_size = 64, },
    [0xEC] = { .level = 3, .type = UNIFIED_CACHE,     .size =  24 * MiB,
               .associativity = 24, .line_size = 64, },
};

/*
 * "CPUID leaf 2 does not report cache descriptor information,
 * use CPUID leaf 4 to query cache parameters"
 */
#define CACHE_DESCRIPTOR_UNAVAILABLE 0xFF

/*
 * Return a CPUID 2 cache descriptor for a given cache.
 * If no known descriptor is found, return CACHE_DESCRIPTOR_UNAVAILABLE
 */
static uint8_t cpuid2_cache_descriptor(CPUCacheInfo *cache)
{
    int i;

    assert(cache->size > 0);
    assert(cache->level > 0);
    assert(cache->line_size > 0);
    assert(cache->associativity > 0);
    for (i = 0; i < ARRAY_SIZE(cpuid2_cache_descriptors); i++) {
        struct CPUID2CacheDescriptorInfo *d = &cpuid2_cache_descriptors[i];
        if (d->level == cache->level && d->type == cache->type &&
            d->size == cache->size && d->line_size == cache->line_size &&
            d->associativity == cache->associativity) {
                return i;
            }
    }

    return CACHE_DESCRIPTOR_UNAVAILABLE;
}

/* CPUID Leaf 4 constants: */

/* EAX: */
#define CACHE_TYPE_D    1
#define CACHE_TYPE_I    2
#define CACHE_TYPE_UNIFIED   3

#define CACHE_LEVEL(l)        (l << 5)

#define CACHE_SELF_INIT_LEVEL (1 << 8)

/* EDX: */
#define CACHE_NO_INVD_SHARING   (1 << 0)
#define CACHE_INCLUSIVE       (1 << 1)
#define CACHE_COMPLEX_IDX     (1 << 2)

/* Encode CacheType for CPUID[4].EAX */
#define CACHE_TYPE(t) (((t) == DATA_CACHE) ? CACHE_TYPE_D : \
                       ((t) == INSTRUCTION_CACHE) ? CACHE_TYPE_I : \
                       ((t) == UNIFIED_CACHE) ? CACHE_TYPE_UNIFIED : \
                       0 /* Invalid value */)


/* Encode cache info for CPUID[4] */
static void encode_cache_cpuid4(CPUCacheInfo *cache,
                                int num_apic_ids, int num_cores,
                                uint32_t *eax, uint32_t *ebx,
                                uint32_t *ecx, uint32_t *edx)
{
    assert(cache->size == cache->line_size * cache->associativity *
                          cache->partitions * cache->sets);

    assert(num_apic_ids > 0);
    *eax = CACHE_TYPE(cache->type) |
           CACHE_LEVEL(cache->level) |
           (cache->self_init ? CACHE_SELF_INIT_LEVEL : 0) |
           ((num_cores - 1) << 26) |
           ((num_apic_ids - 1) << 14);

    assert(cache->line_size > 0);
    assert(cache->partitions > 0);
    assert(cache->associativity > 0);
    /* We don't implement fully-associative caches */
    assert(cache->associativity < cache->sets);
    *ebx = (cache->line_size - 1) |
           ((cache->partitions - 1) << 12) |
           ((cache->associativity - 1) << 22);

    assert(cache->sets > 0);
    *ecx = cache->sets - 1;

    *edx = (cache->no_invd_sharing ? CACHE_NO_INVD_SHARING : 0) |
           (cache->inclusive ? CACHE_INCLUSIVE : 0) |
           (cache->complex_indexing ? CACHE_COMPLEX_IDX : 0);
}

/* Encode cache info for CPUID[0x80000005].ECX or CPUID[0x80000005].EDX */
static uint32_t encode_cache_cpuid80000005(CPUCacheInfo *cache)
{
    assert(cache->size % 1024 == 0);
    assert(cache->lines_per_tag > 0);
    assert(cache->associativity > 0);
    assert(cache->line_size > 0);
    return ((cache->size / 1024) << 24) | (cache->associativity << 16) |
           (cache->lines_per_tag << 8) | (cache->line_size);
}

#define ASSOC_FULL 0xFF

/* AMD associativity encoding used on CPUID Leaf 0x80000006: */
#define AMD_ENC_ASSOC(a) (a <=   1 ? a   : \
                          a ==   2 ? 0x2 : \
                          a ==   4 ? 0x4 : \
                          a ==   8 ? 0x6 : \
                          a ==  16 ? 0x8 : \
                          a ==  32 ? 0xA : \
                          a ==  48 ? 0xB : \
                          a ==  64 ? 0xC : \
                          a ==  96 ? 0xD : \
                          a == 128 ? 0xE : \
                          a == ASSOC_FULL ? 0xF : \
                          0 /* invalid value */)

/*
 * Encode cache info for CPUID[0x80000006].ECX and CPUID[0x80000006].EDX
 * @l3 can be NULL.
 */
static void encode_cache_cpuid80000006(CPUCacheInfo *l2,
                                       CPUCacheInfo *l3,
                                       uint32_t *ecx, uint32_t *edx)
{
    assert(l2->size % 1024 == 0);
    assert(l2->associativity > 0);
    assert(l2->lines_per_tag > 0);
    assert(l2->line_size > 0);
    *ecx = ((l2->size / 1024) << 16) |
           (AMD_ENC_ASSOC(l2->associativity) << 12) |
           (l2->lines_per_tag << 8) | (l2->line_size);

    if (l3) {
        assert(l3->size % (512 * 1024) == 0);
        assert(l3->associativity > 0);
        assert(l3->lines_per_tag > 0);
        assert(l3->line_size > 0);
        *edx = ((l3->size / (512 * 1024)) << 18) |
               (AMD_ENC_ASSOC(l3->associativity) << 12) |
               (l3->lines_per_tag << 8) | (l3->line_size);
    } else {
        *edx = 0;
    }
}

/* Encode cache info for CPUID[8000001D] */
static void encode_cache_cpuid8000001d(CPUCacheInfo *cache,
                                       X86CPUTopoInfo *topo_info,
                                       uint32_t *eax, uint32_t *ebx,
                                       uint32_t *ecx, uint32_t *edx)
{
    uint32_t l3_cores;
    unsigned nodes = MAX(topo_info->nodes_per_pkg, 1);

    assert(cache->size == cache->line_size * cache->associativity *
                          cache->partitions * cache->sets);

    *eax = CACHE_TYPE(cache->type) | CACHE_LEVEL(cache->level) |
               (cache->self_init ? CACHE_SELF_INIT_LEVEL : 0);

    /* L3 is shared among multiple cores */
    if (cache->level == 3) {
        l3_cores = DIV_ROUND_UP((topo_info->dies_per_pkg *
                                 topo_info->cores_per_die *
                                 topo_info->threads_per_core),
                                 nodes);
        *eax |= (l3_cores - 1) << 14;
    } else {
        *eax |= ((topo_info->threads_per_core - 1) << 14);
    }

    assert(cache->line_size > 0);
    assert(cache->partitions > 0);
    assert(cache->associativity > 0);
    /* We don't implement fully-associative caches */
    assert(cache->associativity < cache->sets);
    *ebx = (cache->line_size - 1) |
           ((cache->partitions - 1) << 12) |
           ((cache->associativity - 1) << 22);

    assert(cache->sets > 0);
    *ecx = cache->sets - 1;

    *edx = (cache->no_invd_sharing ? CACHE_NO_INVD_SHARING : 0) |
           (cache->inclusive ? CACHE_INCLUSIVE : 0) |
           (cache->complex_indexing ? CACHE_COMPLEX_IDX : 0);
}

/* Encode cache info for CPUID[8000001E] */
static void encode_topo_cpuid8000001e(X86CPUTopoInfo *topo_info, X86CPU *cpu,
                                       uint32_t *eax, uint32_t *ebx,
                                       uint32_t *ecx, uint32_t *edx)
{
    X86CPUTopoIDs topo_ids = {0};
    unsigned long nodes = MAX(topo_info->nodes_per_pkg, 1);
    int shift;

    x86_topo_ids_from_apicid_epyc(cpu->apic_id, topo_info, &topo_ids);

    *eax = cpu->apic_id;
    /*
     * CPUID_Fn8000001E_EBX
     * 31:16 Reserved
     * 15:8  Threads per core (The number of threads per core is
     *       Threads per core + 1)
     *  7:0  Core id (see bit decoding below)
     *       SMT:
     *           4:3 node id
     *             2 Core complex id
     *           1:0 Core id
     *       Non SMT:
     *           5:4 node id
     *             3 Core complex id
     *           1:0 Core id
     */
    *ebx = ((topo_info->threads_per_core - 1) << 8) | (topo_ids.node_id << 3) |
            (topo_ids.core_id);
    /*
     * CPUID_Fn8000001E_ECX
     * 31:11 Reserved
     * 10:8  Nodes per processor (Nodes per processor is number of nodes + 1)
     *  7:0  Node id (see bit decoding below)
     *         2  Socket id
     *       1:0  Node id
     */
    if (nodes <= 4) {
        *ecx = ((nodes - 1) << 8) | (topo_ids.pkg_id << 2) | topo_ids.node_id;
    } else {
        /*
         * Node id fix up. Actual hardware supports up to 4 nodes. But with
         * more than 32 cores, we may end up with more than 4 nodes.
         * Node id is a combination of socket id and node id. Only requirement
         * here is that this number should be unique accross the system.
         * Shift the socket id to accommodate more nodes. We dont expect both
         * socket id and node id to be big number at the same time. This is not
         * an ideal config but we need to to support it. Max nodes we can have
         * is 32 (255/8) with 8 cores per node and 255 max cores. We only need
         * 5 bits for nodes. Find the left most set bit to represent the total
         * number of nodes. find_last_bit returns last set bit(0 based). Left
         * shift(+1) the socket id to represent all the nodes.
         */
        nodes -= 1;
        shift = find_last_bit(&nodes, 8);
        *ecx = (nodes << 8) | (topo_ids.pkg_id << (shift + 1)) |
               topo_ids.node_id;
    }
    *edx = 0;
}

/*
 * Definitions of the hardcoded cache entries we expose:
 * These are legacy cache values. If there is a need to change any
 * of these values please use builtin_x86_defs
 */

/* L1 data cache: */
static CPUCacheInfo legacy_l1d_cache = {
    .type = DATA_CACHE,
    .level = 1,
    .size = 32 * KiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 8,
    .sets = 64,
    .partitions = 1,
    .no_invd_sharing = true,
};

/*FIXME: CPUID leaf 0x80000005 is inconsistent with leaves 2 & 4 */
static CPUCacheInfo legacy_l1d_cache_amd = {
    .type = DATA_CACHE,
    .level = 1,
    .size = 64 * KiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 2,
    .sets = 512,
    .partitions = 1,
    .lines_per_tag = 1,
    .no_invd_sharing = true,
};

/* L1 instruction cache: */
static CPUCacheInfo legacy_l1i_cache = {
    .type = INSTRUCTION_CACHE,
    .level = 1,
    .size = 32 * KiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 8,
    .sets = 64,
    .partitions = 1,
    .no_invd_sharing = true,
};

/*FIXME: CPUID leaf 0x80000005 is inconsistent with leaves 2 & 4 */
static CPUCacheInfo legacy_l1i_cache_amd = {
    .type = INSTRUCTION_CACHE,
    .level = 1,
    .size = 64 * KiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 2,
    .sets = 512,
    .partitions = 1,
    .lines_per_tag = 1,
    .no_invd_sharing = true,
};

/* Level 2 unified cache: */
static CPUCacheInfo legacy_l2_cache = {
    .type = UNIFIED_CACHE,
    .level = 2,
    .size = 4 * MiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 16,
    .sets = 4096,
    .partitions = 1,
    .no_invd_sharing = true,
};

/*FIXME: CPUID leaf 2 descriptor is inconsistent with CPUID leaf 4 */
static CPUCacheInfo legacy_l2_cache_cpuid2 = {
    .type = UNIFIED_CACHE,
    .level = 2,
    .size = 2 * MiB,
    .line_size = 64,
    .associativity = 8,
};


/*FIXME: CPUID leaf 0x80000006 is inconsistent with leaves 2 & 4 */
static CPUCacheInfo legacy_l2_cache_amd = {
    .type = UNIFIED_CACHE,
    .level = 2,
    .size = 512 * KiB,
    .line_size = 64,
    .lines_per_tag = 1,
    .associativity = 16,
    .sets = 512,
    .partitions = 1,
};

/* Level 3 unified cache: */
static CPUCacheInfo legacy_l3_cache = {
    .type = UNIFIED_CACHE,
    .level = 3,
    .size = 16 * MiB,
    .line_size = 64,
    .associativity = 16,
    .sets = 16384,
    .partitions = 1,
    .lines_per_tag = 1,
    .self_init = true,
    .inclusive = true,
    .complex_indexing = true,
};

/* TLB definitions: */

#define L1_DTLB_2M_ASSOC       1
#define L1_DTLB_2M_ENTRIES   255
#define L1_DTLB_4K_ASSOC       1
#define L1_DTLB_4K_ENTRIES   255

#define L1_ITLB_2M_ASSOC       1
#define L1_ITLB_2M_ENTRIES   255
#define L1_ITLB_4K_ASSOC       1
#define L1_ITLB_4K_ENTRIES   255

#define L2_DTLB_2M_ASSOC       0 /* disabled */
#define L2_DTLB_2M_ENTRIES     0 /* disabled */
#define L2_DTLB_4K_ASSOC       4
#define L2_DTLB_4K_ENTRIES   512

#define L2_ITLB_2M_ASSOC       0 /* disabled */
#define L2_ITLB_2M_ENTRIES     0 /* disabled */
#define L2_ITLB_4K_ASSOC       4
#define L2_ITLB_4K_ENTRIES   512

/* CPUID Leaf 0x14 constants: */
#define INTEL_PT_MAX_SUBLEAF     0x1
/*
 * bit[00]: IA32_RTIT_CTL.CR3 filter can be set to 1 and IA32_RTIT_CR3_MATCH
 *          MSR can be accessed;
 * bit[01]: Support Configurable PSB and Cycle-Accurate Mode;
 * bit[02]: Support IP Filtering, TraceStop filtering, and preservation
 *          of Intel PT MSRs across warm reset;
 * bit[03]: Support MTC timing packet and suppression of COFI-based packets;
 */
#define INTEL_PT_MINIMAL_EBX     0xf
/*
 * bit[00]: Tracing can be enabled with IA32_RTIT_CTL.ToPA = 1 and
 *          IA32_RTIT_OUTPUT_BASE and IA32_RTIT_OUTPUT_MASK_PTRS MSRs can be
 *          accessed;
 * bit[01]: ToPA tables can hold any number of output entries, up to the
 *          maximum allowed by the MaskOrTableOffset field of
 *          IA32_RTIT_OUTPUT_MASK_PTRS;
 * bit[02]: Support Single-Range Output scheme;
 */
#define INTEL_PT_MINIMAL_ECX     0x7
/* generated packets which contain IP payloads have LIP values */
#define INTEL_PT_IP_LIP          (1 << 31)
#define INTEL_PT_ADDR_RANGES_NUM 0x2 /* Number of configurable address ranges */
#define INTEL_PT_ADDR_RANGES_NUM_MASK 0x3
#define INTEL_PT_MTC_BITMAP      (0x0249 << 16) /* Support ART(0,3,6,9) */
#define INTEL_PT_CYCLE_BITMAP    0x1fff         /* Support 0,2^(0~11) */
#define INTEL_PT_PSB_BITMAP      (0x003f << 16) /* Support 2K,4K,8K,16K,32K,64K */

/* CPUID Leaf 0x1D constants: */
#define INTEL_AMX_TILE_MAX_SUBLEAF     0x1
#define INTEL_AMX_TOTAL_TILE_BYTES     0x2000
#define INTEL_AMX_BYTES_PER_TILE       0x400
#define INTEL_AMX_BYTES_PER_ROW        0x40
#define INTEL_AMX_TILE_MAX_NAMES       0x8
#define INTEL_AMX_TILE_MAX_ROWS        0x10

/* CPUID Leaf 0x1E constants: */
#define INTEL_AMX_TMUL_MAX_K           0x10
#define INTEL_AMX_TMUL_MAX_N           0x40

#define I486_FEATURES (CPUID_FP87 | CPUID_VME | CPUID_PSE)
#define PENTIUM_FEATURES (I486_FEATURES | CPUID_DE | CPUID_TSC | \
          CPUID_MSR | CPUID_MCE | CPUID_CX8 | CPUID_MMX | CPUID_APIC)
#define PENTIUM2_FEATURES (PENTIUM_FEATURES | CPUID_PAE | CPUID_SEP | \
          CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT | \
          CPUID_PSE36 | CPUID_FXSR)
#define PENTIUM3_FEATURES (PENTIUM2_FEATURES | CPUID_SSE)
#define PPRO_FEATURES (CPUID_FP87 | CPUID_DE | CPUID_PSE | CPUID_TSC | \
          CPUID_MSR | CPUID_MCE | CPUID_CX8 | CPUID_PGE | CPUID_CMOV | \
          CPUID_PAT | CPUID_FXSR | CPUID_MMX | CPUID_SSE | CPUID_SSE2 | \
          CPUID_PAE | CPUID_SEP | CPUID_APIC)

#define TCG_FEATURES (CPUID_FP87 | CPUID_PSE | CPUID_TSC | CPUID_MSR | \
          CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC | CPUID_SEP | \
          CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT | \
          CPUID_PSE36 | CPUID_CLFLUSH | CPUID_ACPI | CPUID_MMX | \
          CPUID_FXSR | CPUID_SSE | CPUID_SSE2 | CPUID_SS | CPUID_DE)
          /* partly implemented:
          CPUID_MTRR, CPUID_MCA, CPUID_CLFLUSH (needed for Win64) */
          /* missing:
          CPUID_VME, CPUID_DTS, CPUID_SS, CPUID_HT, CPUID_TM, CPUID_PBE */
#if __Use_Original_Qemu == 1 /* original QEMU (U113) */
#define TCG_EXT_FEATURES (CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | \
          CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 | CPUID_EXT_CX16 | \
          CPUID_EXT_SSE41 | CPUID_EXT_SSE42 | CPUID_EXT_POPCNT | \
          CPUID_EXT_XSAVE | /* CPUID_EXT_OSXSAVE is dynamic */   \
          CPUID_EXT_MOVBE | CPUID_EXT_AES | CPUID_EXT_HYPERVISOR | \
          CPUID_EXT_RDRAND | CPUID_EXT_AVX | CPUID_EXT_F16C | \
          CPUID_EXT_FMA)
#else /* ours (U113) */
/* NoVmp (ledger U113): SMX - GETSEC on a platform without an Intel TXT chipset */
#define TCG_EXT_FEATURES (CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | \
          CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 | CPUID_EXT_CX16 | \
          CPUID_EXT_SSE41 | CPUID_EXT_SSE42 | CPUID_EXT_POPCNT | \
          CPUID_EXT_XSAVE | /* CPUID_EXT_OSXSAVE is dynamic */   \
          CPUID_EXT_MOVBE | CPUID_EXT_AES | CPUID_EXT_HYPERVISOR | \
          CPUID_EXT_RDRAND | CPUID_EXT_AVX | CPUID_EXT_F16C | \
          CPUID_EXT_FMA | CPUID_EXT_SMX /* U113 */)
#endif /* __Use_Original_Qemu (U113) */
          /* missing:
          CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_VMX, CPUID_EXT_SMX,
          CPUID_EXT_EST, CPUID_EXT_TM2, CPUID_EXT_CID,
          CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_PCID, CPUID_EXT_DCA,
          CPUID_EXT_X2APIC, CPUID_EXT_TSC_DEADLINE_TIMER */

#ifdef TARGET_X86_64
#define TCG_EXT2_X86_64_FEATURES (CPUID_EXT2_SYSCALL | CPUID_EXT2_LM)
#else
#define TCG_EXT2_X86_64_FEATURES 0
#endif

#define TCG_EXT2_FEATURES ((TCG_FEATURES & CPUID_EXT2_AMD_ALIASES) | \
          CPUID_EXT2_NX | CPUID_EXT2_MMXEXT | CPUID_EXT2_RDTSCP | \
          CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_PDPE1GB | \
          TCG_EXT2_X86_64_FEATURES)
/* backport 8afce497e4 (U489): PREFETCHW (0F 0D /1) is implemented: CPUID.80000001H:ECX[8] */
#define TCG_EXT3_FEATURES (CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM | \
          CPUID_EXT3_CR8LEG | CPUID_EXT3_ABM | CPUID_EXT3_SSE4A | \
          CPUID_EXT3_3DNOWPREFETCH)
#define TCG_EXT4_FEATURES 0
#define TCG_SVM_FEATURES (CPUID_SVM_NPT | CPUID_SVM_VGIF | \
          CPUID_SVM_SVME_ADDR_CHK)
#define TCG_KVM_FEATURES 0
#if __Use_Original_Qemu == 1 /* original QEMU (U110) */
#define TCG_7_0_EBX_FEATURES (CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_SMAP | \
          CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ADX | \
          CPUID_7_0_EBX_CLFLUSHOPT | /* PCOMMIT dropped: 9f07e47a5e (U498) */ \
          CPUID_7_0_EBX_CLWB | CPUID_7_0_EBX_MPX | CPUID_7_0_EBX_FSGSBASE | \
          CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_RDSEED | \
          CPUID_7_0_EBX_SHA_NI)
#else /* ours (U110) */
/* NoVmp (ledger U110): HLE and RTM (every transaction aborts at once, RTM_ALWAYS_ABORT) */
#define TCG_7_0_EBX_FEATURES (CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_SMAP | \
          CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ADX | \
          CPUID_7_0_EBX_CLFLUSHOPT | /* PCOMMIT dropped: 9f07e47a5e (U499) */ \
          CPUID_7_0_EBX_CLWB | CPUID_7_0_EBX_MPX | CPUID_7_0_EBX_FSGSBASE | \
          CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_RDSEED | \
          CPUID_7_0_EBX_SHA_NI | CPUID_7_0_EBX_HLE /* U110 */ | \
          CPUID_7_0_EBX_RTM /* U110 */)
#endif /* __Use_Original_Qemu (U110) */
          /* missing:
          CPUID_7_0_EBX_HLE
          CPUID_7_0_EBX_INVPCID, CPUID_7_0_EBX_RTM */
#if __Use_Original_Qemu == 1 /* original QEMU (U69) */
#define TCG_7_0_ECX_FEATURES (CPUID_7_0_ECX_UMIP | CPUID_7_0_ECX_PKU | \
          /* CPUID_7_0_ECX_OSPKE is dynamic */ \
          CPUID_7_0_ECX_LA57 | CPUID_7_0_ECX_PKS | CPUID_7_0_ECX_VAES | \
          CPUID_7_0_ECX_RDPID)
#else /* ours (U69) */
/* NoVmp: each NoVmp-implemented feature is added by its own ledger entry */
#define TCG_7_0_ECX_FEATURES (CPUID_7_0_ECX_UMIP | CPUID_7_0_ECX_PKU | \
          /* CPUID_7_0_ECX_OSPKE is dynamic */ \
          CPUID_7_0_ECX_LA57 | CPUID_7_0_ECX_PKS | CPUID_7_0_ECX_VAES | \
          CPUID_7_0_ECX_RDPID | CPUID_7_0_ECX_VPCLMULQDQ /* U69 */ | \
          CPUID_7_0_ECX_GFNI /* U70 */ | CPUID_7_0_ECX_MOVDIRI /* U72 */ | \
          CPUID_7_0_ECX_MOVDIR64B /* U73 */ | CPUID_7_0_ECX_KeyLocker /* U100 */ | \
          CPUID_7_0_ECX_WAITPKG /* U111 */ | CPUID_7_0_ECX_ENQCMD /* U112 */ | \
          CPUID_7_0_ECX_CET_SHSTK /* U114 */)
#endif /* __Use_Original_Qemu (U69) */
#if __Use_Original_Qemu == 1 /* original QEMU (U74) */
#define TCG_7_0_EDX_FEATURES 0
#else /* ours (U74) */
#define TCG_7_0_EDX_FEATURES (CPUID_7_0_EDX_SERIALIZE | CPUID_7_0_EDX_UINTR /* U104 */ | \
          CPUID_7_0_EDX_TSX_LDTRK /* U110 */ | CPUID_7_0_EDX_RTM_ALWAYS_ABORT /* U110 */ | \
          CPUID_7_0_EDX_CET_IBT /* U116 */)
#endif /* __Use_Original_Qemu (U74) */
#if __Use_Original_Qemu == 1 /* original QEMU (U71) */
#define TCG_7_1_EAX_FEATURES CPUID_7_1_EAX_CMPCCXADD
#else /* ours (U71) */
#define TCG_7_1_EAX_FEATURES (CPUID_7_1_EAX_CMPCCXADD | CPUID_7_1_EAX_AVX_VNNI | \
          CPUID_7_1_EAX_SHA512 /* U82 */ | CPUID_7_1_EAX_SM3 /* U83 */ | \
          CPUID_7_1_EAX_SM4 /* U84 */ | CPUID_7_1_EAX_AVX_IFMA /* U87 */ | \
          CPUID_7_1_EAX_RAO_INT /* U101 */ | CPUID_7_1_EAX_MOVRS /* U102 */)
#endif /* __Use_Original_Qemu (U71) */
#if __Use_Original_Qemu != 1 /* ours (U85) */
#define TCG_7_1_EDX_FEATURES (CPUID_7_1_EDX_AVX_VNNI_INT8 | \
          CPUID_7_1_EDX_AVX_VNNI_INT16 /* U86 */ | \
          CPUID_7_1_EDX_AVX_NE_CONVERT /* U88 */ | \
          CPUID_7_1_EDX_USER_MSR /* U103 */ | \
          CPUID_7_1_EDX_UIRET_UIF /* U104 */)
#endif /* __Use_Original_Qemu (U85) */
#define TCG_APM_FEATURES 0
#define TCG_6_EAX_FEATURES CPUID_6_EAX_ARAT
#if __Use_Original_Qemu == 1 /* original QEMU (U66) */
#define TCG_XSAVE_FEATURES (CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XGETBV1)
#else /* ours (U66) */
/* NoVmp (ledger U66): XSAVEC and the compacted form of XRSTOR are implemented; U726 (was
   without XSAVES): XSAVES, XRSTORS and IA32_XSS (CPUID.(EAX=0DH,ECX=1):EAX[3]) too */
#define TCG_XSAVE_FEATURES (CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XGETBV1 | CPUID_XSAVE_XSAVEC | \
                            CPUID_XSAVE_XSAVES)
#endif /* __Use_Original_Qemu (U66) */
          /* missing:
          CPUID_XSAVE_XSAVEC, CPUID_XSAVE_XSAVES */

typedef enum FeatureWordType {
   CPUID_FEATURE_WORD,
   MSR_FEATURE_WORD,
} FeatureWordType;

typedef struct FeatureWordInfo {
    FeatureWordType type;
    /* feature flags names are taken from "Intel Processor Identification and
     * the CPUID Instruction" and AMD's "CPUID Specification".
     * In cases of disagreement between feature naming conventions,
     * aliases may be added.
     */
    const char *feat_names[64];
    union {
        /* If type==CPUID_FEATURE_WORD */
        struct {
            uint32_t eax;   /* Input EAX for CPUID */
            bool needs_ecx; /* CPUID instruction uses ECX as input */
            uint32_t ecx;   /* Input ECX value for CPUID */
            int reg;        /* output register (R_* constant) */
        } cpuid;
        /* If type==MSR_FEATURE_WORD */
        struct {
            uint32_t index;
        } msr;
    };
    uint64_t tcg_features; /* Feature flags supported by TCG */
    /* Features that shouldn't be auto-enabled by "-cpu host" */
    uint64_t no_autoenable_flags;
} FeatureWordInfo;

static FeatureWordInfo feature_word_info[FEATURE_WORDS] = {
    [FEAT_1_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "fpu", "vme", "de", "pse",
            "tsc", "msr", "pae", "mce",
            "cx8", "apic", NULL, "sep",
            "mtrr", "pge", "mca", "cmov",
            "pat", "pse36", "pn" /* Intel psn */, "clflush" /* Intel clfsh */,
            NULL, "ds" /* Intel dts */, "acpi", "mmx",
            "fxsr", "sse", "sse2", "ss",
            "ht" /* Intel htt */, "tm", "ia64", "pbe",
        },
        .cpuid = {.eax = 1, .reg = R_EDX, },
        .tcg_features = TCG_FEATURES,
    },
    [FEAT_1_ECX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "pni" /* Intel,AMD sse3 */, "pclmulqdq", "dtes64", "monitor",
            "ds-cpl", "vmx", "smx", "est",
            "tm2", "ssse3", "cid", NULL,
            "fma", "cx16", "xtpr", "pdcm",
            NULL, "pcid", "dca", "sse4.1",
            "sse4.2", "x2apic", "movbe", "popcnt",
            "tsc-deadline", "aes", "xsave", NULL /* osxsave */,
            "avx", "f16c", "rdrand", "hypervisor",
        },
        .cpuid = { .eax = 1, .reg = R_ECX, },
        .tcg_features = TCG_EXT_FEATURES,
    },
    /* Feature names that are already defined on feature_name[] but
     * are set on CPUID[8000_0001].EDX on AMD CPUs don't have their
     * names on feat_names below. They are copied automatically
     * to features[FEAT_8000_0001_EDX] if and only if CPU vendor is AMD.
     */
    [FEAT_8000_0001_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL /* fpu */, NULL /* vme */, NULL /* de */, NULL /* pse */,
            NULL /* tsc */, NULL /* msr */, NULL /* pae */, NULL /* mce */,
            NULL /* cx8 */, NULL /* apic */, NULL, "syscall",
            NULL /* mtrr */, NULL /* pge */, NULL /* mca */, NULL /* cmov */,
            NULL /* pat */, NULL /* pse36 */, NULL, NULL /* Linux mp */,
            "nx", NULL, "mmxext", NULL /* mmx */,
            NULL /* fxsr */, "fxsr-opt", "pdpe1gb", "rdtscp",
            NULL, "lm", "3dnowext", "3dnow",
        },
        .cpuid = { .eax = 0x80000001, .reg = R_EDX, },
        .tcg_features = TCG_EXT2_FEATURES,
    },
    [FEAT_8000_0001_ECX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "lahf-lm", "cmp-legacy", "svm", "extapic",
            "cr8legacy", "abm", "sse4a", "misalignsse",
            "3dnowprefetch", "osvw", "ibs", "xop",
            "skinit", "wdt", NULL, "lwp",
            "fma4", "tce", NULL, "nodeid-msr",
            NULL, "tbm", "topoext", "perfctr-core",
            "perfctr-nb", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0x80000001, .reg = R_ECX, },
        .tcg_features = TCG_EXT3_FEATURES,
        /*
         * TOPOEXT is always allowed but can't be enabled blindly by
         * "-cpu host", as it requires consistent cache topology info
         * to be provided so it doesn't confuse guests.
         */
        .no_autoenable_flags = CPUID_EXT3_TOPOEXT,
    },
    [FEAT_C000_0001_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "xstore", "xstore-en",
            NULL, NULL, "xcrypt", "xcrypt-en",
            "ace2", "ace2-en", "phe", "phe-en",
            "pmm", "pmm-en", NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0xC0000001, .reg = R_EDX, },
        .tcg_features = TCG_EXT4_FEATURES,
    },
    [FEAT_HV_RECOMM_EAX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL /* hv_recommend_pv_as_switch */,
            NULL /* hv_recommend_pv_tlbflush_local */,
            NULL /* hv_recommend_pv_tlbflush_remote */,
            NULL /* hv_recommend_msr_apic_access */,
            NULL /* hv_recommend_msr_reset */,
            NULL /* hv_recommend_relaxed_timing */,
            NULL /* hv_recommend_dma_remapping */,
            NULL /* hv_recommend_int_remapping */,
            NULL /* hv_recommend_x2apic_msrs */,
            NULL /* hv_recommend_autoeoi_deprecation */,
            NULL /* hv_recommend_pv_ipi */,
            NULL /* hv_recommend_ex_hypercalls */,
            NULL /* hv_hypervisor_is_nested */,
            NULL /* hv_recommend_int_mbec */,
            NULL /* hv_recommend_evmcs */,
            NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0x40000004, .reg = R_EAX, },
    },
    [FEAT_HV_NESTED_EAX] = {
        .type = CPUID_FEATURE_WORD,
        .cpuid = { .eax = 0x4000000A, .reg = R_EAX, },
    },
    [FEAT_SVM] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "npt", "lbrv", "svm-lock", "nrip-save",
            "tsc-scale", "vmcb-clean",  "flushbyasid", "decodeassists",
            NULL, NULL, "pause-filter", NULL,
            "pfthreshold", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0x8000000A, .reg = R_EDX, },
        .tcg_features = TCG_SVM_FEATURES,
    },
    [FEAT_7_0_EBX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "fsgsbase", "tsc-adjust", NULL, "bmi1",
            "hle", "avx2", NULL, "smep",
            "bmi2", "erms", "invpcid", "rtm",
            NULL, NULL, "mpx", NULL,
            "avx512f", "avx512dq", "rdseed", "adx",
            "smap", "avx512ifma", "pcommit", "clflushopt",
            "clwb", "intel-pt", "avx512pf", "avx512er",
            "avx512cd", "sha-ni", "avx512bw", "avx512vl",
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EBX,
        },
        .tcg_features = TCG_7_0_EBX_FEATURES,
    },
    [FEAT_7_0_ECX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, "avx512vbmi", "umip", "pku",
            NULL /* ospke */, "waitpkg", "avx512vbmi2", NULL,
            "gfni", "vaes", "vpclmulqdq", "avx512vnni",
            "avx512bitalg", NULL, "avx512-vpopcntdq", NULL,
            "la57", NULL, NULL, NULL,
            NULL, NULL, "rdpid", NULL,
            NULL, "cldemote", NULL, "movdiri",
            "movdir64b", NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 0,
            .reg = R_ECX,
        },
        .tcg_features = TCG_7_0_ECX_FEATURES,
    },
    [FEAT_7_0_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "avx512-4vnniw", "avx512-4fmaps",
            "fsrm", NULL, NULL, NULL,
            "avx512-vp2intersect", NULL, "md-clear", NULL,
            NULL, NULL, "serialize", NULL,
            "tsx-ldtrk", NULL, NULL /* pconfig */, "arch-lbr",
            NULL, NULL, "amx-bf16", "avx512-fp16",
            "amx-tile", "amx-int8", "spec-ctrl", "stibp",
            NULL, "arch-capabilities", "core-capability", "ssbd",
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EDX,
        },
        .tcg_features = TCG_7_0_EDX_FEATURES,
    },
    [FEAT_7_1_EAX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
#if __Use_Original_Qemu == 1 /* original QEMU (U82) */
            NULL, NULL, NULL, NULL,
#else /* ours (U82) */
            "sha512", "sm3" /* U83 */, "sm4" /* U84 */, NULL,
#endif /* __Use_Original_Qemu (U82) */
#if __Use_Original_Qemu == 1 /* original QEMU (U71) */
            NULL, "avx512-bf16", NULL, "cmpccxadd",
#else /* ours (U71) */
            "avx-vnni", "avx512-bf16", NULL, "cmpccxadd",
#endif /* __Use_Original_Qemu (U71) */
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
#if __Use_Original_Qemu == 1 /* original QEMU (U87) */
            NULL, NULL, NULL, NULL,
#else /* ours (U87) */
            NULL, NULL, NULL, "avx-ifma",
#endif /* __Use_Original_Qemu (U87) */
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 1,
            .reg = R_EAX,
        },
        .tcg_features = TCG_7_1_EAX_FEATURES,
    },
#if __Use_Original_Qemu != 1 /* ours (U85) */
    [FEAT_7_1_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            "avx-vnni-int8", "avx-ne-convert" /* U88 */, NULL, NULL,
            NULL, NULL, "avx-vnni-int16" /* U86 */, NULL,
            NULL, NULL, NULL, "user-msr" /* U103 */,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 1,
            .reg = R_EDX,
        },
        .tcg_features = TCG_7_1_EDX_FEATURES,
    },
#endif /* __Use_Original_Qemu (U85) */
    [FEAT_8000_0007_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "invtsc", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0x80000007, .reg = R_EDX, },
        .tcg_features = TCG_APM_FEATURES,
    },
    [FEAT_8000_0008_EBX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "clzero", NULL, "xsaveerptr", NULL,
            NULL, NULL, NULL, NULL,
            NULL, "wbnoinvd", NULL, NULL,
            "ibpb", NULL, NULL, "amd-stibp",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "amd-ssbd", "virt-ssbd", "amd-no-ssb", NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0x80000008, .reg = R_EBX, },
        .tcg_features = 0,
    },
    [FEAT_XSAVE] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "xsaveopt", "xsavec", "xgetbv1", "xsaves",
            "xfd", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 0xd,
            .needs_ecx = true, .ecx = 1,
            .reg = R_EAX,
        },
        .tcg_features = TCG_XSAVE_FEATURES,
    },
    [FEAT_XSAVE_XSS_LO] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 0xd,
            .needs_ecx = true, .ecx = 1,
            .reg = R_ECX,
        },
    },
    [FEAT_XSAVE_XSS_HI] = {
        .type = CPUID_FEATURE_WORD,
        .cpuid = {
            .eax = 0xd,
            .needs_ecx = true, .ecx = 1,
            .reg = R_EDX,
        },
    },
    [FEAT_6_EAX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "arat", NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 6, .reg = R_EAX, },
        .tcg_features = TCG_6_EAX_FEATURES,
    },
    [FEAT_XSAVE_XCR0_LO] = {
        .type = CPUID_FEATURE_WORD,
        .cpuid = {
            .eax = 0xD,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EAX,
        },
        .tcg_features = ~0U,
    },
    [FEAT_XSAVE_XCR0_HI] = {
        .type = CPUID_FEATURE_WORD,
        .cpuid = {
            .eax = 0xD,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EDX,
        },
        .tcg_features = ~0U,
    },
    /*Below are MSR exposed features*/
    [FEAT_ARCH_CAPABILITIES] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            "rdctl-no", "ibrs-all", "rsba", "skip-l1dfl-vmentry",
            "ssb-no", "mds-no", "pschange-mc-no", "tsx-ctrl",
            "taa-no", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_ARCH_CAPABILITIES,
        },
    },
    [FEAT_CORE_CAPABILITY] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, "split-lock-detect", NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_CORE_CAPABILITY,
        },
    },

    [FEAT_VMX_PROCBASED_CTLS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "vmx-vintr-pending", "vmx-tsc-offset",
            NULL, NULL, NULL, "vmx-hlt-exit",
            NULL, "vmx-invlpg-exit", "vmx-mwait-exit", "vmx-rdpmc-exit",
            "vmx-rdtsc-exit", NULL, NULL, "vmx-cr3-load-noexit",
            "vmx-cr3-store-noexit", NULL, NULL, "vmx-cr8-load-exit",
            "vmx-cr8-store-exit", "vmx-flexpriority", "vmx-vnmi-pending", "vmx-movdr-exit",
            "vmx-io-exit", "vmx-io-bitmap", NULL, "vmx-mtf",
            "vmx-msr-bitmap", "vmx-monitor-exit", "vmx-pause-exit", "vmx-secondary-ctls",
        },
        .msr = {
            .index = MSR_IA32_VMX_TRUE_PROCBASED_CTLS,
        }
    },

    [FEAT_VMX_SECONDARY_CTLS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            "vmx-apicv-xapic", "vmx-ept", "vmx-desc-exit", "vmx-rdtscp-exit",
            "vmx-apicv-x2apic", "vmx-vpid", "vmx-wbinvd-exit", "vmx-unrestricted-guest",
            "vmx-apicv-register", "vmx-apicv-vid", "vmx-ple", "vmx-rdrand-exit",
            "vmx-invpcid-exit", "vmx-vmfunc", "vmx-shadow-vmcs", "vmx-encls-exit",
            "vmx-rdseed-exit", "vmx-pml", NULL, NULL,
            "vmx-xsaves", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_PROCBASED_CTLS2,
        }
    },

    [FEAT_VMX_PINBASED_CTLS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            "vmx-intr-exit", NULL, NULL, "vmx-nmi-exit",
            NULL, "vmx-vnmi", "vmx-preemption-timer", "vmx-posted-intr",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_TRUE_PINBASED_CTLS,
        }
    },

    [FEAT_VMX_EXIT_CTLS] = {
        .type = MSR_FEATURE_WORD,
        /*
         * VMX_VM_EXIT_HOST_ADDR_SPACE_SIZE is copied from
         * the LM CPUID bit.
         */
        .feat_names = {
            NULL, NULL, "vmx-exit-nosave-debugctl", NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL /* vmx-exit-host-addr-space-size */, NULL, NULL,
            "vmx-exit-load-perf-global-ctrl", NULL, NULL, "vmx-exit-ack-intr",
            NULL, NULL, "vmx-exit-save-pat", "vmx-exit-load-pat",
            "vmx-exit-save-efer", "vmx-exit-load-efer",
                "vmx-exit-save-preemption-timer", "vmx-exit-clear-bndcfgs",
            NULL, "vmx-exit-clear-rtit-ctl", NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_TRUE_EXIT_CTLS,
        }
    },

    [FEAT_VMX_ENTRY_CTLS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "vmx-entry-noload-debugctl", NULL,
            NULL, NULL, NULL, NULL,
            NULL, "vmx-entry-ia32e-mode", NULL, NULL,
            NULL, "vmx-entry-load-perf-global-ctrl", "vmx-entry-load-pat", "vmx-entry-load-efer",
            "vmx-entry-load-bndcfgs", NULL, "vmx-entry-load-rtit-ctl", NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_TRUE_ENTRY_CTLS,
        }
    },

    [FEAT_VMX_MISC] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, "vmx-store-lma", "vmx-activity-hlt", "vmx-activity-shutdown",
            "vmx-activity-wait-sipi", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, "vmx-vmwrite-vmexit-fields", "vmx-zero-len-inject", NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_MISC,
        }
    },

    [FEAT_VMX_EPT_VPID_CAPS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            "vmx-ept-execonly", NULL, NULL, NULL,
            NULL, NULL, "vmx-page-walk-4", "vmx-page-walk-5",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "vmx-ept-2mb", "vmx-ept-1gb", NULL, NULL,
            "vmx-invept", "vmx-eptad", "vmx-ept-advanced-exitinfo", NULL,
            NULL, "vmx-invept-single-context", "vmx-invept-all-context", NULL,
            NULL, NULL, NULL, NULL,
            "vmx-invvpid", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "vmx-invvpid-single-addr", "vmx-invept-single-context",
                "vmx-invvpid-all-context", "vmx-invept-single-context-noglobals",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_EPT_VPID_CAP,
        }
    },

    [FEAT_VMX_BASIC] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            [54] = "vmx-ins-outs",
            [55] = "vmx-true-ctls",
        },
        .msr = {
            .index = MSR_IA32_VMX_BASIC,
        },
        /* Just to be safe - we don't support setting the MSEG version field.  */
        .no_autoenable_flags = MSR_VMX_BASIC_DUAL_MONITOR,
    },

    [FEAT_VMX_VMFUNC] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            [0] = "vmx-eptp-switching",
        },
        .msr = {
            .index = MSR_IA32_VMX_VMFUNC,
        }
    },

};

typedef enum X86CPURegister32 {
    X86_CPU_REGISTER32_EAX = 0,
    X86_CPU_REGISTER32_EBX = 1,
    X86_CPU_REGISTER32_ECX = 2,
    X86_CPU_REGISTER32_EDX = 3,
    X86_CPU_REGISTER32_ESP = 4,
    X86_CPU_REGISTER32_EBP = 5,
    X86_CPU_REGISTER32_ESI = 6,
    X86_CPU_REGISTER32_EDI = 7,
    X86_CPU_REGISTER32_MAX = 8,
} X86CPURegister32;


typedef struct X86RegisterInfo32 {
    /* Name of register */
    const char *name;
    /* QAPI enum value register */
    X86CPURegister32 qapi_enum;
} X86RegisterInfo32;

#define REGISTER(reg) \
    [R_##reg] = { .name = #reg, .qapi_enum = X86_CPU_REGISTER32_##reg }
static const X86RegisterInfo32 x86_reg_info_32[CPU_NB_REGS32] = {
    REGISTER(EAX),
    REGISTER(ECX),
    REGISTER(EDX),
    REGISTER(EBX),
    REGISTER(ESP),
    REGISTER(EBP),
    REGISTER(ESI),
    REGISTER(EDI),
};
#undef REGISTER

ExtSaveArea x86_ext_save_areas[XSAVE_STATE_AREA_COUNT] = {
    [XSTATE_FP_BIT] = {
        /* x87 FP state component is always enabled if XSAVE is supported */
        .feature = FEAT_1_ECX, .bits = CPUID_EXT_XSAVE,
        /* x87 state is in the legacy region of the XSAVE area */
        .offset = 0,
        .size = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader),
    },
    [XSTATE_SSE_BIT] = {
        /* SSE state component is always enabled if XSAVE is supported */
        .feature = FEAT_1_ECX, .bits = CPUID_EXT_XSAVE,
        /* SSE state is in the legacy region of the XSAVE area */
        .offset = 0,
        .size = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader),
    },
    [XSTATE_YMM_BIT] =
          { .feature = FEAT_1_ECX, .bits = CPUID_EXT_AVX,
            .offset = offsetof(X86XSaveArea, avx_state),
            .size = sizeof(XSaveAVX) },
    [XSTATE_BNDREGS_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_MPX,
            .offset = offsetof(X86XSaveArea, bndreg_state),
            .size = sizeof(XSaveBNDREG)  },
    [XSTATE_BNDCSR_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_MPX,
            .offset = offsetof(X86XSaveArea, bndcsr_state),
            .size = sizeof(XSaveBNDCSR)  },
    [XSTATE_OPMASK_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_AVX512F,
            .offset = offsetof(X86XSaveArea, opmask_state),
            .size = sizeof(XSaveOpmask) },
    [XSTATE_ZMM_Hi256_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_AVX512F,
            .offset = offsetof(X86XSaveArea, zmm_hi256_state),
            .size = sizeof(XSaveZMM_Hi256) },
    [XSTATE_Hi16_ZMM_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_AVX512F,
            .offset = offsetof(X86XSaveArea, hi16_zmm_state),
            .size = sizeof(XSaveHi16_ZMM) },
    [XSTATE_PKRU_BIT] =
          { .feature = FEAT_7_0_ECX, .bits = CPUID_7_0_ECX_PKU,
            .offset = offsetof(X86XSaveArea, pkru_state),
            .size = sizeof(XSavePKRU) },
#if __Use_Original_Qemu != 1 /* ours (U756) */
    /*
     * NoVmp (ledger U756): CET_U (IA32_U_CET, IA32_PL3_SSP) and CET_S (IA32_PL0-2_SSP),
     * supervisor components (SDM Vol1 13.5.9): CPUID.(EAX=0DH,ECX=11/12): EAX = 16 / 24,
     * EBX = 0, ECX[0] = 1 (IA32_XSS). Supported with CET_SS or CET_IBT (x86_cpu_esa_supported).
     */
    [XSTATE_CET_U_BIT] = {
        .feature = FEAT_7_0_ECX, .bits = CPUID_7_0_ECX_CET_SHSTK,
        .offset = 0,
        .size = 16,
    },
    [XSTATE_CET_S_BIT] = {
        .feature = FEAT_7_0_ECX, .bits = CPUID_7_0_ECX_CET_SHSTK,
        .offset = 0,
        .size = 24,
    },
#endif /* __Use_Original_Qemu (U756) */
    [XSTATE_ARCH_LBR_BIT] = {
        .feature = FEAT_7_0_EDX, .bits = CPUID_7_0_EDX_ARCH_LBR,
        .offset = 0,
        .size = sizeof(XSavesArchLBR),
    },
#if __Use_Original_Qemu == 1 /* original QEMU (U170) */
    [XSTATE_XTILE_CFG_BIT] = {
        .feature = FEAT_7_0_EDX, .bits = CPUID_7_0_EDX_AMX_TILE,
        .offset = 0xac0,
        .size = sizeof(XSaveXTILECFG),
    },
    [XSTATE_XTILE_DATA_BIT] = {
        .feature = FEAT_7_0_EDX, .bits = CPUID_7_0_EDX_AMX_TILE,
        .offset = 0xb00,
        .size = sizeof(XSaveXTILEDATA),
    },
#else /* ours (U170) */
    /*
     * NoVmp (ledger U170): CPUID.(EAX=0DH,ECX=17/18):ECX (SDM Vol1 13.2) - both AMX
     * components start on a 64-byte boundary in the compacted format (ECX[1]); XFD is
     * supported for TILEDATA only (ECX[2]; SDM Vol1 19.2: "The first processors
     * implementing Intel AMX will support setting IA32_XFD[18] but not IA32_XFD[17]").
     */
    [XSTATE_XTILE_CFG_BIT] = {
        .feature = FEAT_7_0_EDX, .bits = CPUID_7_0_EDX_AMX_TILE,
        .offset = 0xac0,
        .size = sizeof(XSaveXTILECFG),
        .ecx = ESA_FEATURE_ALIGN64_MASK,
    },
    [XSTATE_XTILE_DATA_BIT] = {
        .feature = FEAT_7_0_EDX, .bits = CPUID_7_0_EDX_AMX_TILE,
        .offset = 0xb00,
        .size = sizeof(XSaveXTILEDATA),
        .ecx = ESA_FEATURE_ALIGN64_MASK | ESA_FEATURE_XFD_MASK,
    },
#endif /* __Use_Original_Qemu (U170) */
#if __Use_Original_Qemu != 1 /* ours (U610) */
    /*
     * NoVmp (ledger U610): Intel APX EGPR state (APX spec 355828-009 3.1.4.3.2/3.1.4.3.3):
     * CPUID.(EAX=0DH,ECX=19): EAX = 128 (R16-R31, 8 bytes each, Table 3.9), EBX = 960 (3C0H,
     * the deprecated MPX area), ECX = 0 (user state, no 64-byte alignment in the compacted
     * format, no XFD). In the compacted format it follows the components below 19.
     */
    [XSTATE_APX_BIT] = {
        .feature = FEAT_7_1_EDX, .bits = CPUID_7_1_EDX_APX_F,
        .offset = 0x3c0,
        .size = 16 * 8,
        .ecx = 0,
    },
#endif /* __Use_Original_Qemu (U610) */
};

uint32_t xsave_area_size(uint64_t mask, bool compacted)
{
    int i;
    uint64_t ret = x86_ext_save_areas[0].size;

    for (i = 2; i < ARRAY_SIZE(x86_ext_save_areas); i++) {
        const ExtSaveArea *esa = &x86_ext_save_areas[i];
        if ((mask >> i) & 1) {
            uint32_t offset = compacted ? ret : esa->offset;
#if __Use_Original_Qemu != 1 /* ours (U170) */
            /* compacted format: ECX[1] components start on the next 64-byte boundary */
            if (compacted && (esa->ecx & ESA_FEATURE_ALIGN64_MASK)) {
                offset = QEMU_ALIGN_UP(offset, 64);
            }
#endif /* __Use_Original_Qemu (U170) */
            ret = MAX(ret, offset + esa->size);
        }
    }
    return ret;
}

static inline bool accel_uses_host_cpuid(void)
{
    return false;
}

static inline uint64_t x86_cpu_xsave_xcr0_components(X86CPU *cpu)
{
    return ((uint64_t)cpu->env.features[FEAT_XSAVE_XCR0_HI]) << 32 |
           cpu->env.features[FEAT_XSAVE_XCR0_LO];
}

static inline uint64_t x86_cpu_xsave_xss_components(X86CPU *cpu)
{
    return ((uint64_t)cpu->env.features[FEAT_XSAVE_XSS_HI]) << 32 |
           cpu->env.features[FEAT_XSAVE_XSS_LO];
}

const char *get_register_name_32(unsigned int reg)
{
    if (reg >= CPU_NB_REGS32) {
        return NULL;
    }
    return x86_reg_info_32[reg].name;
}

void host_cpuid(uint32_t function, uint32_t count,
                uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    uint32_t vec[4];

#ifdef _MSC_VER
    __cpuidex((int*)vec, function, count);
#else
#ifdef __x86_64__
    asm volatile("cpuid"
                 : "=a"(vec[0]), "=b"(vec[1]),
                   "=c"(vec[2]), "=d"(vec[3])
                 : "0"(function), "c"(count) : "cc");
#elif defined(__i386__)
    asm volatile("pusha \n\t"
                 "cpuid \n\t"
                 "mov %%eax, 0(%2) \n\t"
                 "mov %%ebx, 4(%2) \n\t"
                 "mov %%ecx, 8(%2) \n\t"
                 "mov %%edx, 12(%2) \n\t"
                 "popa"
                 : : "a"(function), "c"(count), "S"(vec)
                 : "memory", "cc");
#else
    abort();
#endif
#endif // _MSC_VER

    if (eax)
        *eax = vec[0];
    if (ebx)
        *ebx = vec[1];
    if (ecx)
        *ecx = vec[2];
    if (edx)
        *edx = vec[3];
}

void host_vendor_fms(char *vendor, int *family, int *model, int *stepping)
{
    uint32_t eax, ebx, ecx, edx;

    host_cpuid(0x0, 0, &eax, &ebx, &ecx, &edx);

    host_cpuid(0x1, 0, &eax, &ebx, &ecx, &edx);
    if (family) {
        *family = ((eax >> 8) & 0x0F) + ((eax >> 20) & 0xFF);
    }
    if (model) {
        *model = ((eax >> 4) & 0x0F) | ((eax & 0xF0000) >> 12);
    }
    if (stepping) {
        *stepping = eax & 0x0F;
    }
}

typedef struct PropValue {
    const char *prop, *value;
} PropValue;

typedef struct X86CPUVersionDefinition {
    X86CPUVersion version;
    const char *alias;
    const char *note;
    PropValue *props;
} X86CPUVersionDefinition;

/* Base definition for a CPU model */
typedef struct X86CPUDefinition {
    const char *name;
    uint32_t level;
    uint32_t xlevel;
    /* vendor is zero-terminated, 12 character ASCII string */
    char vendor[CPUID_VENDOR_SZ + 1];
    int family;
    int model;
    int stepping;
    FeatureWordArray features;
    const char *model_id;
    CPUCaches *cache_info;

    /* Use AMD EPYC encoding for apic id */
    bool use_epyc_apic_id_encoding;

    /*
     * Definitions for alternative versions of CPU model.
     * List is terminated by item with version == 0.
     * If NULL, version 1 will be registered automatically.
     */
    const X86CPUVersionDefinition *versions;
} X86CPUDefinition;

/* Reference to a specific CPU model version */
struct X86CPUModel {
    /* Base CPU definition */
    X86CPUDefinition *cpudef;
    /* CPU model version */
    X86CPUVersion version;
    const char *note;
    /*
     * If true, this is an alias CPU model.
     * This matters only for "-cpu help" and query-cpu-definitions
     */
    bool is_alias;
};

static CPUCaches epyc_cache_info = {
    .l1d_cache = &(CPUCacheInfo) {
        .type = DATA_CACHE,
        .level = 1,
        .size = 32 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 64,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l1i_cache = &(CPUCacheInfo) {
        .type = INSTRUCTION_CACHE,
        .level = 1,
        .size = 64 * KiB,
        .line_size = 64,
        .associativity = 4,
        .partitions = 1,
        .sets = 256,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l2_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 2,
        .size = 512 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 1024,
        .lines_per_tag = 1,
    },
    .l3_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 3,
        .size = 8 * MiB,
        .line_size = 64,
        .associativity = 16,
        .partitions = 1,
        .sets = 8192,
        .lines_per_tag = 1,
        .self_init = true,
        .inclusive = true,
        .complex_indexing = true,
    },
};

static CPUCaches epyc_rome_cache_info = {
    .l1d_cache = &(CPUCacheInfo) {
        .type = DATA_CACHE,
        .level = 1,
        .size = 32 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 64,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l1i_cache = &(CPUCacheInfo) {
        .type = INSTRUCTION_CACHE,
        .level = 1,
        .size = 32 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 64,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l2_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 2,
        .size = 512 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 1024,
        .lines_per_tag = 1,
    },
    .l3_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 3,
        .size = 16 * MiB,
        .line_size = 64,
        .associativity = 16,
        .partitions = 1,
        .sets = 16384,
        .lines_per_tag = 1,
        .self_init = true,
        .inclusive = true,
        .complex_indexing = true,
    },
};

/* The following VMX features are not supported by KVM and are left out in the
 * CPU definitions:
 *
 *  Dual-monitor support (all processors)
 *  Entry to SMM
 *  Deactivate dual-monitor treatment
 *  Number of CR3-target values
 *  Shutdown activity state
 *  Wait-for-SIPI activity state
 *  PAUSE-loop exiting (Westmere and newer)
 *  EPT-violation #VE (Broadwell and newer)
 *  Inject event with insn length=0 (Skylake and newer)
 *  Conceal non-root operation from PT
 *  Conceal VM exits from PT
 *  Conceal VM entries from PT
 *  Enable ENCLS exiting
 *  Mode-based execute control (XS/XU)
 s  TSC scaling (Skylake Server and newer)
 *  GPA translation for PT (IceLake and newer)
 *  User wait and pause
 *  ENCLV exiting
 *  Load IA32_RTIT_CTL
 *  Clear IA32_RTIT_CTL
 *  Advanced VM-exit information for EPT violations
 *  Sub-page write permissions
 *  PT in VMX operation
 */

static X86CPUDefinition builtin_x86_defs[] = {
    {
        .name = "qemu64",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 6,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM,
        .xlevel = 0x8000000A,
        .model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION,
    },
    {
        .name = "phenom",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 16,
        .model = 2,
        .stepping = 3,
        /* Missing: CPUID_HT */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_CX16 |
            CPUID_EXT_POPCNT,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX |
            CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_MMXEXT |
            CPUID_EXT2_FFXSR | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP,
        /* Missing: CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS */
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        /* Missing: CPUID_SVM_LBRV */
        .features[FEAT_SVM] =
            CPUID_SVM_NPT,
        .xlevel = 0x8000001A,
        .model_id = "AMD Phenom(tm) 9550 Quad-Core Processor"
    },
    {
        .name = "core2duo",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 15,
        .stepping = 11,
        /* Missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_ACPI | CPUID_SS,
        /* Missing: CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_EST,
         * CPUID_EXT_TM2, CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_VMX */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_CX16,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES,
        .xlevel = 0x80000008,
        .model_id = "Intel(R) Core(TM)2 Duo CPU     T7700  @ 2.40GHz",
    },
    {
        .name = "kvm64",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        /* Missing: CPUID_HT */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        /* Missing: CPUID_EXT_POPCNT, CPUID_EXT_MONITOR */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        /* Missing: CPUID_EXT2_PDPE1GB, CPUID_EXT2_RDTSCP */
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        /* Missing: CPUID_EXT3_LAHF_LM, CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG, CPUID_EXT3_ABM, CPUID_EXT3_SSE4A,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS, CPUID_EXT3_SVM */
        .features[FEAT_8000_0001_ECX] =
            0,
        /* VMX features from Cedar Mill/Prescott */
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING,
        .xlevel = 0x80000008,
        .model_id = "Common KVM processor"
    },
    {
        .name = "qemu32",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 6,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .xlevel = 0x80000004,
        .model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION,
    },
    {
        .name = "kvm32",
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_ECX] =
            0,
        /* VMX features from Yonah */
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_MOV_DR_EXITING | VMX_CPU_BASED_UNCOND_IO_EXITING |
             VMX_CPU_BASED_USE_IO_BITMAPS | VMX_CPU_BASED_MONITOR_EXITING |
             VMX_CPU_BASED_PAUSE_EXITING | VMX_CPU_BASED_USE_MSR_BITMAPS,
        .xlevel = 0x80000008,
        .model_id = "Common 32-bit KVM processor"
    },
    {
        .name = "coreduo",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 14,
        .stepping = 8,
        /* Missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_ACPI |
            CPUID_SS,
        /* Missing: CPUID_EXT_EST, CPUID_EXT_TM2 , CPUID_EXT_XTPR,
         * CPUID_EXT_PDCM, CPUID_EXT_VMX */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_NX,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_MOV_DR_EXITING | VMX_CPU_BASED_UNCOND_IO_EXITING |
             VMX_CPU_BASED_USE_IO_BITMAPS | VMX_CPU_BASED_MONITOR_EXITING |
             VMX_CPU_BASED_PAUSE_EXITING | VMX_CPU_BASED_USE_MSR_BITMAPS,
        .xlevel = 0x80000008,
        .model_id = "Genuine Intel(R) CPU           T2600  @ 2.16GHz",
    },
    {
        .name = "486",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 4,
        .model = 8,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            I486_FEATURES,
        .xlevel = 0,
        .model_id = "",
    },
    {
        .name = "pentium",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 5,
        .model = 4,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PENTIUM_FEATURES,
        .xlevel = 0,
        .model_id = "",
    },
    {
        .name = "pentium2",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 5,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            PENTIUM2_FEATURES,
        .xlevel = 0,
        .model_id = "",
    },
    {
        .name = "pentium3",
        .level = 3,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 7,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PENTIUM3_FEATURES,
        .xlevel = 0,
        .model_id = "",
    },
    {
        .name = "athlon",
        .level = 2,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_PSE36 | CPUID_VME | CPUID_MTRR |
            CPUID_MCA,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_MMXEXT | CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT,
        .xlevel = 0x80000008,
        .model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION,
    },
    {
        .name = "n270",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 28,
        .stepping = 2,
        /* Missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_VME |
            CPUID_ACPI | CPUID_SS,
            /* Some CPUs got no CPUID_SEP */
        /* Missing: CPUID_EXT_DSCPL, CPUID_EXT_EST, CPUID_EXT_TM2,
         * CPUID_EXT_XTPR */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_MOVBE,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "Intel(R) Atom(TM) CPU N270   @ 1.60GHz",
    },
    {
        .name = "Conroe",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 15,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES,
        .xlevel = 0x80000008,
        .model_id = "Intel Celeron_4x0 (Conroe/Merom Class Core 2)",
    },
    {
        .name = "Penryn",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 23,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING,
        .xlevel = 0x80000008,
        .model_id = "Intel Core 2 Duo P9xxx (Penryn Class Core 2)",
    },
    {
        .name = "Nehalem",
        .level = 11,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 26,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID,
        .xlevel = 0x80000008,
        .model_id = "Intel Core i7 9xx (Nehalem Class Core i7)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Nehalem-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Core i7 9xx (Nehalem Core i7, IBRS update)" },
                    { NULL /* end of list */ }
                }
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Westmere",
        .level = 11,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 44,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST,
        .xlevel = 0x80000008,
        .model_id = "Westmere E56xx/L56xx/X56xx (Nehalem-C)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Westmere-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Westmere E56xx/L56xx/X56xx (IBRS update)" },
                    { NULL /* end of list */ }
                }
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "SandyBridge",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 42,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_POPCNT |
            CPUID_EXT_X2APIC | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon E312xx (Sandy Bridge)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "SandyBridge-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Xeon E312xx (Sandy Bridge, IBRS update)" },
                    { NULL /* end of list */ }
                }
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "IvyBridge",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 58,
        .stepping = 9,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_POPCNT |
            CPUID_EXT_X2APIC | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3 | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_ERMS,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon E3-12xx v2 (Ivy Bridge)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "IvyBridge-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Xeon E3-12xx v2 (Ivy Bridge, IBRS)" },
                    { NULL /* end of list */ }
                }
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Haswell",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 60,
        .stepping = 4,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Haswell)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Haswell-noTSX",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { "stepping", "1" },
                    { "model-id", "Intel Core Processor (Haswell, no TSX)", },
                    { NULL /* end of list */ }
                },
            },
            {
                .version = 3,
                .alias = "Haswell-IBRS",
                .props = (PropValue[]) {
                    /* Restore TSX features removed by -v2 above */
                    { "hle", "on" },
                    { "rtm", "on" },
                    /*
                     * Haswell and Haswell-IBRS had stepping=4 in
                     * QEMU 4.0 and older
                     */
                    { "stepping", "4" },
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Core Processor (Haswell, IBRS)" },
                    { NULL /* end of list */ }
                }
            },
            {
                .version = 4,
                .alias = "Haswell-noTSX-IBRS",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    /* spec-ctrl was already enabled by -v3 above */
                    { "stepping", "1" },
                    { "model-id",
                      "Intel Core Processor (Haswell, no TSX, IBRS)" },
                    { NULL /* end of list */ }
                }
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Broadwell",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 61,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Broadwell)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Broadwell-noTSX",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { "model-id", "Intel Core Processor (Broadwell, no TSX)", },
                    { NULL /* end of list */ }
                },
            },
            {
                .version = 3,
                .alias = "Broadwell-IBRS",
                .props = (PropValue[]) {
                    /* Restore TSX features removed by -v2 above */
                    { "hle", "on" },
                    { "rtm", "on" },
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Core Processor (Broadwell, IBRS)" },
                    { NULL /* end of list */ }
                }
            },
            {
                .version = 4,
                .alias = "Broadwell-noTSX-IBRS",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    /* spec-ctrl was already enabled by -v3 above */
                    { "model-id",
                      "Intel Core Processor (Broadwell, no TSX, IBRS)" },
                    { NULL /* end of list */ }
                }
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Skylake-Client",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 94,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP,
        /* Missing: XSAVES (not supported by some Linux versions,
         * including v4.1 to v4.12).
         * KVM doesn't yet expose any XSAVES state save component,
         * and the only one defined in Skylake (processor tracing)
         * probably will block migration anyway.
         */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Skylake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Skylake-Client-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Core Processor (Skylake, IBRS)" },
                    { NULL /* end of list */ }
                }
            },
            {
                .version = 3,
                .alias = "Skylake-Client-noTSX-IBRS",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { "model-id",
                      "Intel Core Processor (Skylake, IBRS, no TSX)" },
                    { NULL /* end of list */ }
                }
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Skylake-Server",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 85,
        .stepping = 4,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_AVX512F | CPUID_7_0_EBX_AVX512DQ |
            CPUID_7_0_EBX_AVX512BW | CPUID_7_0_EBX_AVX512CD |
            CPUID_7_0_EBX_AVX512VL | CPUID_7_0_EBX_CLFLUSHOPT,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_PKU,
        /* Missing: XSAVES (not supported by some Linux versions,
         * including v4.1 to v4.12).
         * KVM doesn't yet expose any XSAVES state save component,
         * and the only one defined in Skylake (processor tracing)
         * probably will block migration anyway.
         */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Processor (Skylake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Skylake-Server-IBRS",
                .props = (PropValue[]) {
                    /* clflushopt was not added to Skylake-Server-IBRS */
                    /* TODO: add -v3 including clflushopt */
                    { "clflushopt", "off" },
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Xeon Processor (Skylake, IBRS)" },
                    { NULL /* end of list */ }
                }
            },
            {
                .version = 3,
                .alias = "Skylake-Server-noTSX-IBRS",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { "model-id",
                      "Intel Xeon Processor (Skylake, IBRS, no TSX)" },
                    { NULL /* end of list */ }
                }
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Cascadelake-Server",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 85,
        .stepping = 6,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_AVX512F | CPUID_7_0_EBX_AVX512DQ |
            CPUID_7_0_EBX_AVX512BW | CPUID_7_0_EBX_AVX512CD |
            CPUID_7_0_EBX_AVX512VL | CPUID_7_0_EBX_CLFLUSHOPT,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_PKU |
            CPUID_7_0_ECX_AVX512VNNI,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL | CPUID_7_0_EDX_SPEC_CTRL_SSBD,
        /* Missing: XSAVES (not supported by some Linux versions,
                * including v4.1 to v4.12).
                * KVM doesn't yet expose any XSAVES state save component,
                * and the only one defined in Skylake (processor tracing)
                * probably will block migration anyway.
                */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Processor (Cascadelake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            { .version = 2,
              .props = (PropValue[]) {
                  { "arch-capabilities", "on" },
                  { "rdctl-no", "on" },
                  { "ibrs-all", "on" },
                  { "skip-l1dfl-vmentry", "on" },
                  { "mds-no", "on" },
                  { NULL /* end of list */ }
              },
            },
            { .version = 3,
              .alias = "Cascadelake-Server-noTSX",
              .props = (PropValue[]) {
                  { "hle", "off" },
                  { "rtm", "off" },
                  { NULL /* end of list */ }
              },
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Cooperlake",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 85,
        .stepping = 10,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_AVX512F | CPUID_7_0_EBX_AVX512DQ |
            CPUID_7_0_EBX_AVX512BW | CPUID_7_0_EBX_AVX512CD |
            CPUID_7_0_EBX_AVX512VL | CPUID_7_0_EBX_CLFLUSHOPT,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_PKU |
            CPUID_7_0_ECX_AVX512VNNI,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL | CPUID_7_0_EDX_STIBP |
            CPUID_7_0_EDX_SPEC_CTRL_SSBD | CPUID_7_0_EDX_ARCH_CAPABILITIES,
        .features[FEAT_ARCH_CAPABILITIES] =
            MSR_ARCH_CAP_RDCL_NO | MSR_ARCH_CAP_IBRS_ALL |
            MSR_ARCH_CAP_SKIP_L1DFL_VMENTRY | MSR_ARCH_CAP_MDS_NO |
            MSR_ARCH_CAP_PSCHANGE_MC_NO | MSR_ARCH_CAP_TAA_NO,
        .features[FEAT_7_1_EAX] =
            CPUID_7_1_EAX_AVX512_BF16,
        /*
         * Missing: XSAVES (not supported by some Linux versions,
         * including v4.1 to v4.12).
         * KVM doesn't yet expose any XSAVES state save component,
         * and the only one defined in Skylake (processor tracing)
         * probably will block migration anyway.
         */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Processor (Cooperlake)",
    },
    {
        .name = "Icelake-Client",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 126,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_8000_0008_EBX] =
            CPUID_8000_0008_EBX_WBNOINVD,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_AVX512_VBMI | CPUID_7_0_ECX_UMIP | CPUID_7_0_ECX_PKU |
            CPUID_7_0_ECX_AVX512_VBMI2 | CPUID_7_0_ECX_GFNI |
            CPUID_7_0_ECX_VAES | CPUID_7_0_ECX_VPCLMULQDQ |
            CPUID_7_0_ECX_AVX512VNNI | CPUID_7_0_ECX_AVX512BITALG |
            CPUID_7_0_ECX_AVX512_VPOPCNTDQ,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL | CPUID_7_0_EDX_SPEC_CTRL_SSBD,
        /* Missing: XSAVES (not supported by some Linux versions,
                * including v4.1 to v4.12).
                * KVM doesn't yet expose any XSAVES state save component,
                * and the only one defined in Skylake (processor tracing)
                * probably will block migration anyway.
                */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Icelake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Icelake-Client-noTSX",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { NULL /* end of list */ }
                },
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Icelake-Server",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 134,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_8000_0008_EBX] =
            CPUID_8000_0008_EBX_WBNOINVD,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_AVX512F | CPUID_7_0_EBX_AVX512DQ |
            CPUID_7_0_EBX_AVX512BW | CPUID_7_0_EBX_AVX512CD |
            CPUID_7_0_EBX_AVX512VL | CPUID_7_0_EBX_CLFLUSHOPT,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_AVX512_VBMI | CPUID_7_0_ECX_UMIP | CPUID_7_0_ECX_PKU |
            CPUID_7_0_ECX_AVX512_VBMI2 | CPUID_7_0_ECX_GFNI |
            CPUID_7_0_ECX_VAES | CPUID_7_0_ECX_VPCLMULQDQ |
            CPUID_7_0_ECX_AVX512VNNI | CPUID_7_0_ECX_AVX512BITALG |
            CPUID_7_0_ECX_AVX512_VPOPCNTDQ | CPUID_7_0_ECX_LA57,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL | CPUID_7_0_EDX_SPEC_CTRL_SSBD,
        /* Missing: XSAVES (not supported by some Linux versions,
                * including v4.1 to v4.12).
                * KVM doesn't yet expose any XSAVES state save component,
                * and the only one defined in Skylake (processor tracing)
                * probably will block migration anyway.
                */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Processor (Icelake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Icelake-Server-noTSX",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { NULL /* end of list */ }
                },
            },
            {
                .version = 3,
                .props = (PropValue[]) {
                    { "arch-capabilities", "on" },
                    { "rdctl-no", "on" },
                    { "ibrs-all", "on" },
                    { "skip-l1dfl-vmentry", "on" },
                    { "mds-no", "on" },
                    { "pschange-mc-no", "on" },
                    { "taa-no", "on" },
                    { NULL /* end of list */ }
                },
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Denverton",
        .level = 21,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 95,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_FP87 | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC |
            CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC |
            CPUID_SEP | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV |
            CPUID_PAT | CPUID_PSE36 | CPUID_CLFLUSH | CPUID_MMX | CPUID_FXSR |
            CPUID_SSE | CPUID_SSE2,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_MONITOR |
            CPUID_EXT_SSSE3 | CPUID_EXT_CX16 | CPUID_EXT_SSE41 |
            CPUID_EXT_SSE42 | CPUID_EXT_X2APIC | CPUID_EXT_MOVBE |
            CPUID_EXT_POPCNT | CPUID_EXT_TSC_DEADLINE_TIMER |
            CPUID_EXT_AES | CPUID_EXT_XSAVE | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_SYSCALL | CPUID_EXT2_NX | CPUID_EXT2_PDPE1GB |
            CPUID_EXT2_RDTSCP | CPUID_EXT2_LM,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_ERMS |
            CPUID_7_0_EBX_MPX | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_SMAP |
            CPUID_7_0_EBX_CLFLUSHOPT | CPUID_7_0_EBX_SHA_NI,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL | CPUID_7_0_EDX_ARCH_CAPABILITIES |
            CPUID_7_0_EDX_SPEC_CTRL_SSBD,
        /*
         * Missing: XSAVES (not supported by some Linux versions,
         * including v4.1 to v4.12).
         * KVM doesn't yet expose any XSAVES state save component,
         * and the only one defined in Skylake (processor tracing)
         * probably will block migration anyway.
         */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC | CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_ARCH_CAPABILITIES] =
            MSR_ARCH_CAP_RDCL_NO | MSR_ARCH_CAP_SKIP_L1DFL_VMENTRY,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Atom Processor (Denverton)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .props = (PropValue[]) {
                    { "monitor", "off" },
                    { "mpx", "off" },
                    { NULL /* end of list */ },
                },
            },
            { 0 /* end of list */ },
        },
    },
    {
        .name = "Snowridge",
        .level = 27,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 134,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            /* missing: CPUID_PN CPUID_IA64 */
            /* missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
            CPUID_FP87 | CPUID_VME | CPUID_DE | CPUID_PSE |
            CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_MCE |
            CPUID_CX8 | CPUID_APIC | CPUID_SEP |
            CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV |
            CPUID_PAT | CPUID_PSE36 | CPUID_CLFLUSH |
            CPUID_MMX |
            CPUID_FXSR | CPUID_SSE | CPUID_SSE2,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_MONITOR |
            CPUID_EXT_SSSE3 |
            CPUID_EXT_CX16 |
            CPUID_EXT_SSE41 |
            CPUID_EXT_SSE42 | CPUID_EXT_X2APIC | CPUID_EXT_MOVBE |
            CPUID_EXT_POPCNT |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_AES | CPUID_EXT_XSAVE |
            CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_SYSCALL |
            CPUID_EXT2_NX |
            CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_LM,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM |
            CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE |
            CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_ERMS |
            CPUID_7_0_EBX_MPX |  /* missing bits 13, 15 */
            CPUID_7_0_EBX_RDSEED |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT |
            CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_SHA_NI,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_UMIP |
            /* missing bit 5 */
            CPUID_7_0_ECX_GFNI |
            CPUID_7_0_ECX_MOVDIRI | CPUID_7_0_ECX_CLDEMOTE |
            CPUID_7_0_ECX_MOVDIR64B,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL |
            CPUID_7_0_EDX_ARCH_CAPABILITIES | CPUID_7_0_EDX_SPEC_CTRL_SSBD |
            CPUID_7_0_EDX_CORE_CAPABILITY,
        .features[FEAT_CORE_CAPABILITY] =
            MSR_CORE_CAP_SPLIT_LOCK_DETECT,
        /*
         * Missing: XSAVES (not supported by some Linux versions,
         * including v4.1 to v4.12).
         * KVM doesn't yet expose any XSAVES state save component,
         * and the only one defined in Skylake (processor tracing)
         * probably will block migration anyway.
         */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Atom Processor (SnowRidge)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .props = (PropValue[]) {
                    { "mpx", "off" },
                    { "model-id", "Intel Atom Processor (Snowridge, no MPX)" },
                    { NULL /* end of list */ },
                },
            },
            { 0 /* end of list */ },
        },
    },
    {
        .name = "KnightsMill",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 133,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SS | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR |
            CPUID_MMX | CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV |
            CPUID_MCA | CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC |
            CPUID_CX8 | CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC |
            CPUID_PSE | CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_AVX2 |
            CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS |
            CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX | CPUID_7_0_EBX_AVX512F |
            CPUID_7_0_EBX_AVX512CD | CPUID_7_0_EBX_AVX512PF |
            CPUID_7_0_EBX_AVX512ER,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_AVX512_VPOPCNTDQ,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_AVX512_4VNNIW | CPUID_7_0_EDX_AVX512_4FMAPS,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Phi Processor (Knights Mill)",
    },
    {
        .name = "Opteron_G1",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 240 (Gen 1 Class Opteron)",
    },
    {
        .name = "Opteron_G2",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_CX16 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 22xx (Gen 2 Class Opteron)",
    },
    {
        .name = "Opteron_G3",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 16,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_POPCNT | CPUID_EXT_CX16 | CPUID_EXT_MONITOR |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL |
            CPUID_EXT2_RDTSCP,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A |
            CPUID_EXT3_ABM | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 23xx (Gen 3 Class Opteron)",
    },
    {
        .name = "Opteron_G4",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 21,
        .model = 1,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL | CPUID_EXT2_RDTSCP,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
            CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
            CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        /* no xsaveopt! */
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 62xx class CPU",
    },
    {
        .name = "Opteron_G5",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 21,
        .model = 2,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_F16C | CPUID_EXT_AVX | CPUID_EXT_XSAVE |
            CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_FMA |
            CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL | CPUID_EXT2_RDTSCP,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_TBM | CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
            CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
            CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        /* no xsaveopt! */
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 63xx class CPU",
    },
    {
        .name = "EPYC",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 23,
        .model = 1,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX | CPUID_CLFLUSH |
            CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA | CPUID_PGE |
            CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 | CPUID_MCE |
            CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE | CPUID_DE |
            CPUID_VME | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_RDRAND | CPUID_EXT_F16C | CPUID_EXT_AVX |
            CPUID_EXT_XSAVE | CPUID_EXT_AES |  CPUID_EXT_POPCNT |
            CPUID_EXT_MOVBE | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_FMA | CPUID_EXT_SSSE3 |
            CPUID_EXT_MONITOR | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_PDPE1GB |
            CPUID_EXT2_FFXSR | CPUID_EXT2_MMXEXT | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_OSVW | CPUID_EXT3_3DNOWPREFETCH |
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A | CPUID_EXT3_ABM |
            CPUID_EXT3_CR8LEG | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM |
            CPUID_EXT3_TOPOEXT,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_AVX2 |
            CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_RDSEED |
            CPUID_7_0_EBX_ADX | CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT |
            CPUID_7_0_EBX_SHA_NI,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        .xlevel = 0x8000001E,
        .model_id = "AMD EPYC Processor",
        .cache_info = &epyc_cache_info,
        .use_epyc_apic_id_encoding = 1,
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "EPYC-IBPB",
                .props = (PropValue[]) {
                    { "ibpb", "on" },
                    { "model-id",
                      "AMD EPYC Processor (with IBPB)" },
                    { NULL /* end of list */ }
                }
            },
            {
                .version = 3,
                .props = (PropValue[]) {
                    { "ibpb", "on" },
                    { "perfctr-core", "on" },
                    { "clzero", "on" },
                    { "xsaveerptr", "on" },
                    { "xsaves", "on" },
                    { "model-id",
                      "AMD EPYC Processor" },
                    { NULL /* end of list */ }
                }
            },
            { 0 /* end of list */ }
        }
    },
    {
        .name = "Dhyana",
        .level = 0xd,
        .vendor = CPUID_VENDOR_HYGON,
        .family = 24,
        .model = 0,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX | CPUID_CLFLUSH |
            CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA | CPUID_PGE |
            CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 | CPUID_MCE |
            CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE | CPUID_DE |
            CPUID_VME | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_RDRAND | CPUID_EXT_F16C | CPUID_EXT_AVX |
            CPUID_EXT_XSAVE | CPUID_EXT_POPCNT |
            CPUID_EXT_MOVBE | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_FMA | CPUID_EXT_SSSE3 |
            CPUID_EXT_MONITOR | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_PDPE1GB |
            CPUID_EXT2_FFXSR | CPUID_EXT2_MMXEXT | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_OSVW | CPUID_EXT3_3DNOWPREFETCH |
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A | CPUID_EXT3_ABM |
            CPUID_EXT3_CR8LEG | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM |
            CPUID_EXT3_TOPOEXT,
        .features[FEAT_8000_0008_EBX] =
            CPUID_8000_0008_EBX_IBPB,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_AVX2 |
            CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_RDSEED |
            CPUID_7_0_EBX_ADX | CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT,
        /*
         * Missing: XSAVES (not supported by some Linux versions,
         * including v4.1 to v4.12).
         * KVM doesn't yet expose any XSAVES state save component.
         */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        .xlevel = 0x8000001E,
        .model_id = "Hygon Dhyana Processor",
        .cache_info = &epyc_cache_info,
    },
    {
        .name = "EPYC-Rome",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 23,
        .model = 49,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX | CPUID_CLFLUSH |
            CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA | CPUID_PGE |
            CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 | CPUID_MCE |
            CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE | CPUID_DE |
            CPUID_VME | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_RDRAND | CPUID_EXT_F16C | CPUID_EXT_AVX |
            CPUID_EXT_XSAVE | CPUID_EXT_AES |  CPUID_EXT_POPCNT |
            CPUID_EXT_MOVBE | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_FMA | CPUID_EXT_SSSE3 |
            CPUID_EXT_MONITOR | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_PDPE1GB |
            CPUID_EXT2_FFXSR | CPUID_EXT2_MMXEXT | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_OSVW | CPUID_EXT3_3DNOWPREFETCH |
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A | CPUID_EXT3_ABM |
            CPUID_EXT3_CR8LEG | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM |
            CPUID_EXT3_TOPOEXT | CPUID_EXT3_PERFCORE,
        .features[FEAT_8000_0008_EBX] =
            CPUID_8000_0008_EBX_CLZERO | CPUID_8000_0008_EBX_XSAVEERPTR |
            CPUID_8000_0008_EBX_WBNOINVD | CPUID_8000_0008_EBX_IBPB |
            CPUID_8000_0008_EBX_STIBP,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_AVX2 |
            CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_RDSEED |
            CPUID_7_0_EBX_ADX | CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT |
            CPUID_7_0_EBX_SHA_NI | CPUID_7_0_EBX_CLWB,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_UMIP | CPUID_7_0_ECX_RDPID,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1 | CPUID_XSAVE_XSAVES,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        .xlevel = 0x8000001E,
        .model_id = "AMD EPYC-Rome Processor",
        .cache_info = &epyc_rome_cache_info,
        .use_epyc_apic_id_encoding = 1,
    },
};


/*
 * We resolve CPU model aliases using -v1 when using "-machine
 * none", but this is just for compatibility while libvirt isn't
 * adapted to resolve CPU model versions before creating VMs.
 * See "Runnability guarantee of CPU models" at * qemu-deprecated.texi.
 */
X86CPUVersion default_cpu_version = 1;

void x86_cpu_set_default_version(X86CPUVersion version)
{
    /* Translating CPU_VERSION_AUTO to CPU_VERSION_AUTO doesn't make sense */
    assert(version != CPU_VERSION_AUTO);
    default_cpu_version = version;
}

#define CPUID_MODEL_ID_SZ 48

static bool x86_cpu_have_filtered_features(X86CPU *cpu)
{
    FeatureWord w;

    for (w = 0; w < FEATURE_WORDS; w++) {
        if (cpu->filtered_features[w]) {
            return true;
        }
    }

    return false;
}

static void mark_unavailable_features(X86CPU *cpu, FeatureWord w, uint64_t mask,
                                      const char *verbose_prefix)
{
    CPUX86State *env = &cpu->env;

    if (!cpu->force_features) {
        env->features[w] &= ~mask;
    }
    cpu->filtered_features[w] |= mask;

    if (!verbose_prefix) {
        return;
    }
}

/* Convert all '_' in a feature string option name to '-', to make feature
 * name conform to QOM property naming rule, which uses '-' instead of '_'.
 */
static inline void feat2prop(char *s)
{
    while ((s = strchr(s, '_'))) {
        *s = '-';
    }
}

static void x86_cpu_filter_features(X86CPU *cpu, bool verbose);

static uint64_t x86_cpu_get_supported_feature_word(FeatureWord w,
                                                   bool migratable_only)
{
    FeatureWordInfo *wi = &feature_word_info[w];
    uint64_t r;

    // TCG enable
    r = wi->tcg_features;

    return r;
}

/* Load data from X86CPUDefinition into a X86CPU object
 */
static void x86_cpu_load_model(X86CPU *cpu, X86CPUModel *model)
{
    X86CPUDefinition *def = model->cpudef;
    CPUX86State *env = &cpu->env;
    FeatureWord w;

    env->cpuid_min_level = def->level;
    env->cpuid_xlevel = def->xlevel;
    x86_cpuid_version_set_family(cpu, def->family);
    x86_cpuid_version_set_model(cpu, def->model);
    x86_cpuid_version_set_stepping(cpu, def->stepping);
    x86_cpuid_set_model_id(cpu, def->model_id);
    for (w = 0; w < FEATURE_WORDS; w++) {
        env->features[w] = def->features[w];
    }

    /* legacy-cache defaults to 'off' if CPU model provides cache info */
    cpu->legacy_cache = !def->cache_info;

    env->features[FEAT_1_ECX] |= CPUID_EXT_HYPERVISOR;

    /* sysenter isn't supported in compatibility mode on AMD,
     * syscall isn't supported in compatibility mode on Intel.
     * Normally we advertise the actual CPU vendor, but you can
     * override this using the 'vendor' property if you want to use
     * KVM's sysenter/syscall emulation in compatibility mode and
     * when doing cross vendor migration
     */
    if (accel_uses_host_cpuid()) {
        uint32_t  ebx = 0, ecx = 0, edx = 0;
        host_cpuid(0, 0, NULL, &ebx, &ecx, &edx);
    }

    x86_cpuid_set_vendor(cpu, def->vendor);
}

void cpu_clear_apic_feature(CPUX86State *env)
{
    env->features[FEAT_1_EDX] &= ~CPUID_APIC;
}

static void x86_cpuid_version_set_family(X86CPU *cpu, int64_t value)
{
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff + 0xf;

    if (value < min || value > max) {
        // error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
        //            name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xff00f00;
    if (value > 0x0f) {
        env->cpuid_version |= 0xf00 | ((value - 0x0f) << 20);
    } else {
        env->cpuid_version |= value << 8;
    }
}

static void x86_cpuid_version_set_model(X86CPU *cpu, int64_t value)
{
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff;

    if (value < min || value > max) {
        // error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
        //            name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf00f0;
    env->cpuid_version |= ((value & 0xf) << 4) | ((value >> 4) << 16);
}

static void x86_cpuid_version_set_stepping(X86CPU *cpu, int64_t value)
{
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xf;

    if (value < min || value > max) {
        // error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
        //            name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf;
    env->cpuid_version |= value & 0xf;
}

static void x86_cpuid_set_model_id(X86CPU *cpu, const char* model_id)
{
    CPUX86State *env = &cpu->env;
    int c, len, i;

    if (model_id == NULL) {
        model_id = "";
    }
    len = strlen(model_id);
    memset(env->cpuid_model, 0, 48);
    for (i = 0; i < 48; i++) {
        if (i >= len) {
            c = '\0';
        } else {
            c = (uint8_t)model_id[i];
        }
        env->cpuid_model[i >> 2] |= c << (8 * (i & 3));
    }
}

static void x86_cpuid_set_vendor(X86CPU *cpu , const char *value)
{
    CPUX86State *env = &cpu->env;
    int i;

    if (strlen(value) != CPUID_VENDOR_SZ) {
        // error_setg(errp, QERR_PROPERTY_VALUE_BAD, "", "vendor", value);
        return;
    }

    env->cpuid_vendor1 = 0;
    env->cpuid_vendor2 = 0;
    env->cpuid_vendor3 = 0;
    for (i = 0; i < 4; i++) {
        env->cpuid_vendor1 |= ((uint8_t)value[i    ]) << (8 * i);
        env->cpuid_vendor2 |= ((uint8_t)value[i + 4]) << (8 * i);
        env->cpuid_vendor3 |= ((uint8_t)value[i + 8]) << (8 * i);
    }
}

#if __Use_Original_Qemu != 1 /* ours (U68) */
/*
 * NoVmp (ledger U68): CPUID from the UC_CTL_X86_CPUID profile. A leaf above the
 * profile's maximum basic / extended leaf returns the highest basic leaf (SDM Vol2
 * CPUID, Goldmont MSROM U478a/U1bce); a (leaf, subleaf) the profile does not list
 * reads as zero except leaves 0Bh/1Fh, which echo ECX[7:0] and keep the x2APIC ID.
 * OSXSAVE (1:ECX[27]) and OSPKE (7.0:ECX[4]) follow CR4. Returns false without a
 * profile.
 */
static const struct uc_x86_cpuid *x86_cpuid_find(struct uc_struct *uc, uint32_t leaf,
                                                 uint32_t sub, bool any_sub)
{
    size_t i;
    for (i = 0; i < uc->x86_cpuid_count; i++) {
        if (uc->x86_cpuid[i].leaf == leaf && (any_sub || uc->x86_cpuid[i].subleaf == sub)) {
            return &uc->x86_cpuid[i];
        }
    }
    return NULL;
}

static bool x86_cpuid_leaf_has_subleaves(uint32_t leaf)
{
    switch (leaf) {
    case 0x4: case 0x7: case 0xb: case 0xd: case 0xf: case 0x10: case 0x12: case 0x14:
    case 0x17: case 0x18: case 0x1b: case 0x1d: case 0x1f: case 0x20: case 0x23: case 0x24:
    case 0x29: /* APX leaf, ECX = sub-leaf (U610) */
#if __Use_Original_Qemu != 1 /* ours (U721) */
    /*
     * NoVmp (ledger U721): leaf 1EH has sub-leaves too (ISE 319433-062 Table 1-3: 1EH.0 TMUL
     * main leaf, 1EH.1 the AMX feature flags); a profile lookup of (1EH, 1) returned sub-leaf
     * 0 (EAX = 1 = "AMX_INT8"), so CPUID and the strict-profile mask of 1EH.1 were wrong.
     */
    case 0x1e:
#endif /* __Use_Original_Qemu (U721) */
        return true;
    default:
        return false;
    }
}

#if __Use_Original_Qemu != 1 /* ours (U125) */
/*
 * NoVmp (ledger U125): CPUID.(EAX=0DH,ECX=0):EBX is the size of the standard-format
 * XSAVE area for the components enabled in XCR0 (SDM Vol2 CPUID), so a profile's
 * captured EBX (the capture machine's XCR0) is recomputed from the current XCR0 and
 * the profile's own subleaf offsets/sizes (0DH.i: EBX offset, EAX size), at least
 * the 576-byte legacy area + header.
 */
static uint32_t x86_cpuid_profile_xsave_size(struct uc_struct *uc, uint64_t xcr0)
{
    uint32_t size = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader);
    int i;

    for (i = 2; i < 63; i++) {
        const struct uc_x86_cpuid *s;
        if (!((xcr0 >> i) & 1)) {
            continue;
        }
        s = x86_cpuid_find(uc, 0xd, i, false);
        if (s && s->ebx + s->eax > size) {
            size = s->ebx + s->eax;
        }
    }
    return size;
}

#endif /* __Use_Original_Qemu (U125) */
static bool x86_cpuid_profile(CPUX86State *env, uint32_t index, uint32_t count,
                              uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    struct uc_struct *uc = env->uc;
    const struct uc_x86_cpuid *e, *top;
    uint32_t max_basic, max_ext;

    if (!uc || !uc->x86_cpuid_count) {
        return false;
    }
    top = x86_cpuid_find(uc, 0, 0, false);
    max_basic = top ? top->eax : 0;
    e = x86_cpuid_find(uc, 0x80000000, 0, false);
    max_ext = e ? e->eax : 0;
    if (index >= 0x80000000 ? index > max_ext : index > max_basic) {
        index = max_basic;
    }
    if (!x86_cpuid_leaf_has_subleaves(index)) {
        count = 0;
    }
    e = x86_cpuid_find(uc, index, count, false);
    *eax = *ebx = *ecx = *edx = 0;
    if (e) {
        *eax = e->eax; *ebx = e->ebx; *ecx = e->ecx; *edx = e->edx;
    } else if (index == 0xb || index == 0x1f) {
        const struct uc_x86_cpuid *s0 = x86_cpuid_find(uc, index, 0, false);
        *ecx = count & 0xff;
        *edx = s0 ? s0->edx : 0;
    }
    if (index == 1) {
        *ecx = (*ecx & ~CPUID_EXT_OSXSAVE) |
               ((env->cr[4] & CR4_OSXSAVE_MASK) ? CPUID_EXT_OSXSAVE : 0);
    } else if (index == 7 && count == 0) {
        *ecx = (*ecx & ~CPUID_7_0_ECX_OSPKE) |
               ((env->cr[4] & CR4_PKE_MASK) ? CPUID_7_0_ECX_OSPKE : 0);
    }
#if __Use_Original_Qemu != 1 /* ours (U125) */
    else if (index == 0xd && count == 0 && e) {
        *ebx = x86_cpuid_profile_xsave_size(uc, env->xcr0);
    }
#endif /* __Use_Original_Qemu (U125) */
    return true;
}

/* strict mode: the feature word as the profile advertises it (translator, U68) */
uint32_t x86_cpuid_profile_mask(CPUX86State *env, uint32_t leaf, uint32_t sub, int reg)
{
    uint32_t r[4];

    if (!env->uc || !env->uc->x86_cpuid_strict ||
        !x86_cpuid_profile(env, leaf, sub, &r[0], &r[1], &r[2], &r[3])) {
        return ~0u;
    }
    return r[reg];
}

#if __Use_Original_Qemu != 1 /* ours (U860) */
/*
 * NoVmp (ledger U860): SDM Vol1 8.1.8 ties the x87 data pointer and the FCS/FDS selectors to
 * CPUID.(EAX=07H,ECX=0):EBX[6] (FDP_EXCPTN_ONLY: FDP/FDS updated only by a non-control
 * instruction that incurs an unmasked x87 exception) and EBX[13] (FCS and FDS deprecated,
 * saved as 0000H). The behaviour follows what CPUID reports: the UC_CTL_X86_CPUID profile
 * as given (a custom profile may clear either bit), else the model (both bits, U433; a
 * model without leaf 7 reports neither).
 */
uint32_t x86_cpu_x87_ptr_bits(CPUX86State *env)
{
    uint32_t eax, ebx, ecx, edx;

    cpu_x86_cpuid(env, 7, 0, &eax, &ebx, &ecx, &edx);
    return ebx & (X86_X87_FDP_EXCPTN_ONLY | X86_X87_FCS_FDS_DEPR);
}

#endif /* __Use_Original_Qemu (U860) */
#if __Use_Original_Qemu != 1 /* ours (U593) */
/*
 * NoVmp (ledger U593): MAXPHYADDR (cpu->phys_bits: the reserved bits of CR3 and of the paging
 * structures, SDM Vol3A 5.3/5.5) follows the CPU that CPUID describes: with a UC_CTL_X86_CPUID
 * profile that reports leaf 80000008H, its EAX[7:0] (i5-13600K: 45 = 2Dh), else the model's
 * value (TCG's 40 for 64-bit models, 36/32 without long mode - x86_cpu_realizefn). Called at
 * realize and whenever the profile changes.
 */
void x86_cpu_update_phys_bits(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    const struct uc_x86_cpuid *top, *e;

    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        cpu->phys_bits = TCG_PHYS_ADDR_BITS;
    } else {
        cpu->phys_bits = (env->features[FEAT_1_EDX] & CPUID_PSE36) ? 36 : 32;
    }
    if (!env->uc || !env->uc->x86_cpuid_count) {
        return;
    }
    top = x86_cpuid_find(env->uc, 0x80000000, 0, false);
    e = x86_cpuid_find(env->uc, 0x80000008, 0, false);
    if (top && top->eax >= 0x80000008 && e && (e->eax & 0xff) >= 32 && (e->eax & 0xff) <= 52) {
        cpu->phys_bits = e->eax & 0xff;
    }
}
#endif /* __Use_Original_Qemu (U593) */
#endif /* __Use_Original_Qemu (U68) */
#if __Use_Original_Qemu != 1 /* ours (U120) */
/*
 * NoVmp (ledger U120): XCR0 restricted to the components CPUID.(EAX=0DH,ECX=0):EDX:EAX
 * of the active UC_CTL_X86_CPUID profile reports, i.e. to what XSETBV accepts, keeping
 * the XSETBV pairing rules (SDM Vol1 13.3: bit 0 always set, YMM only with SSE, MPX 4:3
 * both or neither, AVX-512 7:5 all or none and only with 2:1). Used for reset XCR0 and
 * when a profile is set later; without a profile the model value stands.
 */
uint64_t x86_cpu_xcr0_in_profile(CPUX86State *env, uint64_t xcr0)
{
    const uint64_t avx512 = XSTATE_OPMASK_MASK | XSTATE_ZMM_Hi256_MASK |
                            XSTATE_Hi16_ZMM_MASK;
    const uint64_t mpx = XSTATE_BNDREGS_MASK | XSTATE_BNDCSR_MASK;
    uint32_t eax, ebx, ecx, edx;

    if (!x86_cpuid_profile(env, 0xd, 0, &eax, &ebx, &ecx, &edx)) {
        return xcr0;
    }
    xcr0 &= ((uint64_t)edx << 32) | eax;
    xcr0 |= XSTATE_FP_MASK;
    if (!(xcr0 & XSTATE_SSE_MASK)) {
        xcr0 &= ~XSTATE_YMM_MASK;
    }
    if ((xcr0 & mpx) != mpx) {
        xcr0 &= ~mpx;
    }
    if ((xcr0 & (avx512 | XSTATE_SSE_MASK | XSTATE_YMM_MASK)) !=
        (avx512 | XSTATE_SSE_MASK | XSTATE_YMM_MASK)) {
        xcr0 &= ~avx512;
    }
#if __Use_Original_Qemu != 1 /* ours (U170) */
    /* AMX: TILECFG and TILEDATA both or neither (SDM Vol1 13.3) */
    if ((xcr0 & XSTATE_AMX_MASK) != XSTATE_AMX_MASK) {
        xcr0 &= ~XSTATE_AMX_MASK;
    }
#endif /* __Use_Original_Qemu (U170) */
    return xcr0;
}
#endif /* __Use_Original_Qemu (U120) */
#if __Use_Original_Qemu != 1 /* ours (U173) */
/*
 * NoVmp (ledger U173): the state components whose IA32_XFD / IA32_XFD_ERR bit can be set
 * (SDM Vol1 13.14): CPUID.(EAX=0DH,ECX=1):EAX.XFD and, per supported user component i,
 * CPUID.(EAX=0DH,ECX=i):ECX[2] (TILEDATA only, U170).
 */
uint64_t x86_cpu_xfd_supported(CPUX86State *env)
{
    uint64_t comps = ((uint64_t)env->features[FEAT_XSAVE_XCR0_HI] << 32) |
                     env->features[FEAT_XSAVE_XCR0_LO];
    uint64_t mask = 0;
    int i;

    if (!(env->features[FEAT_XSAVE] & CPUID_D_1_EAX_XFD)) {
        return 0;
    }
    for (i = 2; i < XSAVE_STATE_AREA_COUNT; i++) {
        if (((comps >> i) & 1) && (x86_ext_save_areas[i].ecx & ESA_FEATURE_XFD_MASK)) {
            mask |= 1ULL << i;
        }
    }
    return mask;
}

/* the components for which XFD is enabled: XCR0[i] = IA32_XFD[i] = 1 (supported bits) */
uint64_t x86_cpu_xfd_armed(CPUX86State *env)
{
    return env->msr_xfd ? env->xcr0 & env->msr_xfd & x86_cpu_xfd_supported(env) : 0;
}
#endif /* __Use_Original_Qemu (U173) */
#if __Use_Original_Qemu != 1 /* ours (U720) */
/*
 * NoVmp (ledger U720): CPUID.(EAX=1EH,ECX=1):EAX of the model (ISE 319433-062 Table 1-3):
 * bits 0-3 mirror AMX_INT8 / AMX_BF16 / AMX_COMPLEX / AMX_FP16 of leaf 7 ("enumerated in
 * both"); AMX_FP8 (4), AMX_AVX512 (7) and AMX_MOVRS (8) exist only here and come from the
 * UC_CTL_X86_AMX mask. Bits 6:5 are reserved (5 was AMX-TRANSPOSE, 6 AMX-TF32: both removed
 * from the ISE). 0 without AMX_TILE.
 */
uint32_t x86_cpu_amx_1e_1_eax(CPUX86State *env)
{
    uint32_t eax = 0;
    int mask = env->uc ? env->uc->x86_amx : 0;

    if (!(env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_TILE)) {
        return 0;
    }
    if (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_INT8) {
        eax |= CPUID_1E_1_EAX_AMX_INT8;
    }
    if (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_BF16) {
        eax |= CPUID_1E_1_EAX_AMX_BF16;
    }
    if (env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_AMX_COMPLEX) {
        eax |= CPUID_1E_1_EAX_AMX_COMPLEX;
    }
    if (env->features[FEAT_7_1_EAX] & CPUID_7_1_EAX_AMX_FP16) {
        eax |= CPUID_1E_1_EAX_AMX_FP16;
    }
    if (mask & UC_X86_AMX_FP8) {
        eax |= CPUID_1E_1_EAX_AMX_FP8;
    }
    if (mask & UC_X86_AMX_AVX512) {
        eax |= CPUID_1E_1_EAX_AMX_AVX512;
    }
    if (mask & UC_X86_AMX_MOVRS) {
        eax |= CPUID_1E_1_EAX_AMX_MOVRS;
    }
    return eax;
}
#endif /* __Use_Original_Qemu (U720) */

void cpu_x86_cpuid(CPUX86State *env, uint32_t index, uint32_t count,
                   uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx)
{
    X86CPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    uint32_t die_offset;
    uint32_t limit;
    uint32_t signature[3];
    X86CPUTopoInfo topo_info;

    topo_info.nodes_per_pkg = env->nr_nodes;
    topo_info.dies_per_pkg = env->nr_dies;
    topo_info.cores_per_die = cs->nr_cores;
    topo_info.threads_per_core = cs->nr_threads;

    /* Calculate & apply limits for different index ranges */
    if (index >= 0xC0000000) {
        limit = env->cpuid_xlevel2;
    } else if (index >= 0x80000000) {
        limit = env->cpuid_xlevel;
    } else if (index >= 0x40000000) {
        limit = 0x40000001;
    } else {
        limit = env->cpuid_level;
    }

#if __Use_Original_Qemu != 1 /* ours (U68) */
    if (x86_cpuid_profile(env, index, count, eax, ebx, ecx, edx)) {
        /* NoVmp (ledger U460): SYSCALL/SYSRET reported only in 64-bit mode (see 80000001H) */
        if (index == 0x80000001 && env->cpuid_vendor1 == CPUID_VENDOR_INTEL_1 &&
            !(env->hflags & HF_CS64_MASK)) {
            *edx &= ~CPUID_EXT2_SYSCALL;
        }
        return;
    }
#endif /* __Use_Original_Qemu (U68) */
    if (index > limit) {
        /* Intel documentation states that invalid EAX input will
         * return the same information as EAX=cpuid_level
         * (Intel SDM Vol. 2A - Instruction Set Reference - CPUID)
         */
        index = env->cpuid_level;
    }

    switch(index) {
    case 0:
        *eax = env->cpuid_level;
        *ebx = env->cpuid_vendor1;
        *edx = env->cpuid_vendor2;
        *ecx = env->cpuid_vendor3;
        break;
    case 1:
        *eax = env->cpuid_version;
        *ebx = (cpu->apic_id << 24) |
               8 << 8; /* CLFLUSH size in quad words, Linux wants it. */
        *ecx = env->features[FEAT_1_ECX];
        if ((*ecx & CPUID_EXT_XSAVE) && (env->cr[4] & CR4_OSXSAVE_MASK)) {
            *ecx |= CPUID_EXT_OSXSAVE;
        }
        *edx = env->features[FEAT_1_EDX];
        if (cs->nr_cores * cs->nr_threads > 1) {
            *ebx |= (cs->nr_cores * cs->nr_threads) << 16;
            *edx |= CPUID_HT;
        }
        break;
    case 2:
        /* cache info: needed for Pentium Pro compatibility */
        if (cpu->cache_info_passthrough) {
            host_cpuid(index, 0, eax, ebx, ecx, edx);
            break;
        }
        *eax = 1; /* Number of CPUID[EAX=2] calls required */
        *ebx = 0;
        if (!cpu->enable_l3_cache) {
            *ecx = 0;
        } else {
            *ecx = cpuid2_cache_descriptor(env->cache_info_cpuid2.l3_cache);
        }
        *edx = (cpuid2_cache_descriptor(env->cache_info_cpuid2.l1d_cache) << 16) |
               (cpuid2_cache_descriptor(env->cache_info_cpuid2.l1i_cache) <<  8) |
               (cpuid2_cache_descriptor(env->cache_info_cpuid2.l2_cache));
        break;
    case 4:
        /* cache info: needed for Core compatibility */
        if (cpu->cache_info_passthrough) {
            host_cpuid(index, count, eax, ebx, ecx, edx);
            /* QEMU gives out its own APIC IDs, never pass down bits 31..26.  */
            *eax &= ~0xFC000000;
            if ((*eax & 31) && cs->nr_cores > 1) {
                *eax |= (cs->nr_cores - 1) << 26;
            }
        } else {
            *eax = 0;
            switch (count) {
            case 0: /* L1 dcache info */
                encode_cache_cpuid4(env->cache_info_cpuid4.l1d_cache,
                                    1, cs->nr_cores,
                                    eax, ebx, ecx, edx);
                break;
            case 1: /* L1 icache info */
                encode_cache_cpuid4(env->cache_info_cpuid4.l1i_cache,
                                    1, cs->nr_cores,
                                    eax, ebx, ecx, edx);
                break;
            case 2: /* L2 cache info */
                encode_cache_cpuid4(env->cache_info_cpuid4.l2_cache,
                                    cs->nr_threads, cs->nr_cores,
                                    eax, ebx, ecx, edx);
                break;
            case 3: /* L3 cache info */
                die_offset = apicid_die_offset(&topo_info);
                if (cpu->enable_l3_cache) {
                    encode_cache_cpuid4(env->cache_info_cpuid4.l3_cache,
                                        (1 << die_offset), cs->nr_cores,
                                        eax, ebx, ecx, edx);
                    break;
                }
                /* fall through */
            default: /* end of info */
                *eax = *ebx = *ecx = *edx = 0;
                break;
            }
        }
        break;
    case 5:
        /* MONITOR/MWAIT Leaf */
        *eax = cpu->mwait.eax; /* Smallest monitor-line size in bytes */
        *ebx = cpu->mwait.ebx; /* Largest monitor-line size in bytes */
        *ecx = cpu->mwait.ecx; /* flags */
        *edx = cpu->mwait.edx; /* mwait substates */
        break;
    case 6:
        /* Thermal and Power Leaf */
        *eax = env->features[FEAT_6_EAX];
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 7:
        /* Structured Extended Feature Flags Enumeration Leaf */
        if (count == 0) {
            /* Maximum ECX value for sub-leaves */
            *eax = env->cpuid_level_func7;
            *ebx = env->features[FEAT_7_0_EBX]; /* Feature flags */
#if __Use_Original_Qemu != 1 /* ours (U433) */
            /*
             * NoVmp (ledger U433): every built-in model reports
             * CPUID.(EAX=07H,ECX=0):EBX[6] (FDP_EXCPTN_ONLY) and EBX[13]
             * (ZERO_FCS_FDS) like the i5-13600K. The x87 follows whatever CPUID
             * reports (x86_cpu_x87_ptr_bits, U860, SDM Vol1 8.1.8), so a
             * UC_CTL_X86_CPUID profile (returned as given) may clear either bit.
             */
            *ebx |= (1U << 6) | (1U << 13);
#endif /* __Use_Original_Qemu (U433) */
            *ecx = env->features[FEAT_7_0_ECX]; /* Feature flags */
            if ((*ecx & CPUID_7_0_ECX_PKU) && env->cr[4] & CR4_PKE_MASK) {
                *ecx |= CPUID_7_0_ECX_OSPKE;
            }
            *edx = env->features[FEAT_7_0_EDX]; /* Feature flags */
        } else if (count == 1) {
            *eax = env->features[FEAT_7_1_EAX];
            *ebx = 0;
            *ecx = 0;
#if __Use_Original_Qemu == 1 /* original QEMU (U85) */
            *edx = 0;
#else /* ours (U85) */
            *edx = env->features[FEAT_7_1_EDX];
#endif /* __Use_Original_Qemu (U85) */
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 9:
        /* Direct Cache Access Information Leaf */
        *eax = 0; /* Bits 0-31 in DCA_CAP MSR */
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xA:
        /* Architectural Performance Monitoring Leaf */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xB:
        /* Extended Topology Enumeration Leaf */
        if (!cpu->enable_cpuid_0xb) {
                *eax = *ebx = *ecx = *edx = 0;
                break;
        }

        *ecx = count & 0xff;
        *edx = cpu->apic_id;

        switch (count) {
        case 0:
            *eax = apicid_core_offset(&topo_info);
            *ebx = cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_SMT;
            break;
        case 1:
            *eax = env->pkg_offset;
            *ebx = cs->nr_cores * cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_CORE;
            break;
        default:
            *eax = 0;
            *ebx = 0;
            *ecx |= CPUID_TOPOLOGY_LEVEL_INVALID;
        }

        assert(!(*eax & ~0x1f));
        *ebx &= 0xffff; /* The count doesn't need to be reliable. */
        break;
    case 0x1F:
        /* V2 Extended Topology Enumeration Leaf */
        if (env->nr_dies < 2) {
            *eax = *ebx = *ecx = *edx = 0;
            break;
        }

        *ecx = count & 0xff;
        *edx = cpu->apic_id;
        switch (count) {
        case 0:
            *eax = apicid_core_offset(&topo_info);
            *ebx = cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_SMT;
            break;
        case 1:
            *eax = apicid_die_offset(&topo_info);
            *ebx = cs->nr_cores * cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_CORE;
            break;
        case 2:
            *eax = env->pkg_offset;
            *ebx = env->nr_dies * cs->nr_cores * cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_DIE;
            break;
        default:
            *eax = 0;
            *ebx = 0;
            *ecx |= CPUID_TOPOLOGY_LEVEL_INVALID;
        }
        assert(!(*eax & ~0x1f));
        *ebx &= 0xffff; /* The count doesn't need to be reliable. */
        break;
    case 0xD: {
        /* Processor Extended State */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE)) {
            break;
        }

        if (count == 0) {
            *ecx = xsave_area_size(x86_cpu_xsave_xcr0_components(cpu), false);
            *eax = env->features[FEAT_XSAVE_XCR0_LO];
            *edx = env->features[FEAT_XSAVE_XCR0_HI];
            /*
             * The initial value of xcr0 and ebx == 0, On host without kvm
             * commit 412a3c41(e.g., CentOS 6), the ebx's value always == 0
             * even through guest update xcr0, this will crash some legacy guest
             * (e.g., CentOS 6), So set ebx == ecx to workaroud it.
             */
            *ebx = xsave_area_size(env->xcr0, false);
        } else if (count == 1) {
            uint64_t xstate = x86_cpu_xsave_xcr0_components(cpu) |
                              x86_cpu_xsave_xss_components(cpu);

            *eax = env->features[FEAT_XSAVE];
#if __Use_Original_Qemu == 1 /* original QEMU (U125) */
            *ebx = xsave_area_size(xstate, true);
#else /* ours (U125) */
            /* NoVmp (ledger U125): SDM Vol1 13.2 - EBX is the XSAVES size for the
               components currently set in XCR0 | IA32_XSS if EAX[3], else the XSAVEC
               size for those currently set in XCR0 (not for all supported ones).
               Without XSAVEC the SDM says 0; QEMU's non-zero value is kept there
               (models without XSAVEC, e.g. Haswell, are outside this change). */
            (void)xstate;
            *ebx = xsave_area_size((*eax & CPUID_XSAVE_XSAVES) ? env->xcr0 | env->xss
                                                                : env->xcr0, true);
#endif /* __Use_Original_Qemu (U125) */
            *ecx = env->features[FEAT_XSAVE_XSS_LO];
            *edx = env->features[FEAT_XSAVE_XSS_HI];
            *ecx &= ~XSTATE_ARCH_LBR_MASK;
        } else if (count < ARRAY_SIZE(x86_ext_save_areas)) {
            const ExtSaveArea *esa = &x86_ext_save_areas[count];

            if ((x86_cpu_xsave_xcr0_components(cpu) >> count) & 1) {
                *eax = esa->size;
                *ebx = esa->offset;
                *ecx = esa->ecx &
                       (ESA_FEATURE_ALIGN64_MASK | ESA_FEATURE_XFD_MASK);
            } else if ((x86_cpu_xsave_xss_components(cpu) >> count) & 1) {
                *eax = esa->size;
                *ebx = 0;
                *ecx = 1;
            }
        }
        break;
    }
    case 0x14: {
        /* Intel Processor Trace Enumeration */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_INTEL_PT)) {
            break;
        }

        if (count == 0) {
            *eax = INTEL_PT_MAX_SUBLEAF;
            *ebx = INTEL_PT_MINIMAL_EBX;
            *ecx = INTEL_PT_MINIMAL_ECX;
        } else if (count == 1) {
            *eax = INTEL_PT_MTC_BITMAP | INTEL_PT_ADDR_RANGES_NUM;
            *ebx = INTEL_PT_PSB_BITMAP | INTEL_PT_CYCLE_BITMAP;
        }
        break;
    }
#if __Use_Original_Qemu != 1 /* ours (U100) */
    case 0x19:
        /*
         * NoVmp (ledger U100): Key Locker leaf (Key Locker spec 2, table 2-1).
         * AESKLE reads 0 unless CR4.KL is set.
         */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_KeyLocker) {
            *eax = NOVMP_CPUID_19_EAX;
            *ebx = NOVMP_CPUID_19_EBX;
            if (!(env->cr[4] & CR4_KL_MASK)) {
                *ebx &= ~CPUID_19_EBX_AESKLE;
            }
            *ecx = NOVMP_CPUID_19_ECX;
        }
        break;
#endif /* __Use_Original_Qemu (U100) */
    case 0x1D: {
        /* AMX TILE */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_TILE)) {
            break;
        }

        if (count == 0) {
            *eax = INTEL_AMX_TILE_MAX_SUBLEAF;
        } else if (count == 1) {
            *eax = INTEL_AMX_TOTAL_TILE_BYTES |
                   (INTEL_AMX_BYTES_PER_TILE << 16);
            *ebx = INTEL_AMX_BYTES_PER_ROW |
                   (INTEL_AMX_TILE_MAX_NAMES << 16);
            *ecx = INTEL_AMX_TILE_MAX_ROWS;
        }
        break;
    }
    case 0x1E: {
        /* AMX TMUL */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_TILE)) {
            break;
        }

#if __Use_Original_Qemu == 1 /* original QEMU (U170) */
        if (count == 0) {
            *ebx = INTEL_AMX_TMUL_MAX_K | (INTEL_AMX_TMUL_MAX_N << 8);
        }
#else /* ours (U170) */
        /*
         * NoVmp (ledger U170): ISE 319433 Table 1-3 - 1EH.0: EAX = highest sub-leaf (1),
         * EBX = tmul_maxk 16 | tmul_maxn 64 << 8; 1EH.1: EAX = the AMX computation
         * features again (INT8 0, BF16 1, COMPLEX 2, FP16 3; "enumerated in both").
         */
        if (count == 0) {
            *eax = 1;
            *ebx = INTEL_AMX_TMUL_MAX_K | (INTEL_AMX_TMUL_MAX_N << 8);
        } else if (count == 1) {
            if (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_INT8) {
                *eax |= CPUID_1E_1_EAX_AMX_INT8;
            }
            if (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_BF16) {
                *eax |= CPUID_1E_1_EAX_AMX_BF16;
            }
            if (env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_AMX_COMPLEX) {
                *eax |= CPUID_1E_1_EAX_AMX_COMPLEX;
            }
            if (env->features[FEAT_7_1_EAX] & CPUID_7_1_EAX_AMX_FP16) {
                *eax |= CPUID_1E_1_EAX_AMX_FP16;
            }
#if __Use_Original_Qemu != 1 /* ours (U720) */
            /* + AMX_FP8 / AMX_AVX512 / AMX_MOVRS (x86_cpu_amx_1e_1_eax) */
            *eax |= x86_cpu_amx_1e_1_eax(env);
#endif /* __Use_Original_Qemu (U720) */
        }
#endif /* __Use_Original_Qemu (U170) */
        break;
    }
#if __Use_Original_Qemu != 1 /* ours (U370) */
    case 0x24:
        /*
         * NoVmp (ledger U370): Intel AVX10 Converged Vector ISA leaf (AVX10.2 spec 361050-007
         * Table 3.1, ISE 319433-062): (24H,0) EAX = maximum sub-leaf, EBX[7:0] = version,
         * EBX[18:16] reserved at 1; (24H,1) ECX[2] = AVX10_V1_AUX (always with AVX10.2:
         * "Intel CPUs which support Intel AVX10.2 will include an enumeration for
         * AVX10_V1_AUX"). All zero without AVX10.
         */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_AVX10) {
            int v = env->uc->x86_avx10;
            bool aux = (v & UC_X86_AVX10_V1_AUX) || (v & UC_X86_AVX10_VERSION) >= 2;

            if (count == 0) {
                *eax = aux ? 1 : 0;
                *ebx = (v & UC_X86_AVX10_VERSION) | CPUID_24_0_EBX_AVX10_VL_MASK;
            } else if (count == 1 && aux) {
                *ecx = CPUID_24_1_ECX_AVX10_V1_AUX;
            }
        }
        break;
#endif /* __Use_Original_Qemu (U370) */
#if __Use_Original_Qemu != 1 /* ours (U610) */
    case 0x29:
        /*
         * NoVmp (ledger U610): Intel APX leaf (APX spec 355828-009 3.1.4.3.1): (29H,0) EAX =
         * maximum sub-leaf = 0, EBX[0] = APX_NCI_NDD_NF (always present with APX_F), EBX[31:1],
         * ECX, EDX reserved = 0. All zero without APX_F and for other sub-leaves.
         */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if ((env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_APX_F) && count == 0) {
            *ebx = CPUID_29_0_EBX_APX_NCI_NDD_NF;
        }
        break;
#endif /* __Use_Original_Qemu (U610) */
    case 0x40000000:
        /*
         * CPUID code in kvm_arch_init_vcpu() ignores stuff
         * set here, but we restrict to TCG none the less.
         */
        if (cpu->expose_tcg) {
            memcpy(signature, "TCGTCGTCGTCG", 12);
            *eax = 0x40000001;
            *ebx = signature[0];
            *ecx = signature[1];
            *edx = signature[2];
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0x40000001:
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0x80000000:
        *eax = env->cpuid_xlevel;
        /*
         * backport a539cd2614 (U488): EBX/ECX/EDX are reserved on Intel (SDM Vol2A CPUID
         * 80000000H: "EBX Reserved. ECX Reserved. EDX Reserved."; i5-13600K: 0, 0, 0).
         * Upstream also tests its vendor_cpuid_only_v2 machine property, which Unicorn
         * lacks (always the new behaviour here).
         */
        if (IS_INTEL_CPU(env)) {
            *ebx = *ecx = *edx = 0;
        } else {
            *ebx = env->cpuid_vendor1;
            *edx = env->cpuid_vendor2;
            *ecx = env->cpuid_vendor3;
        }
        break;
    case 0x80000001:
        *eax = env->cpuid_version;
        *ebx = 0;
        *ecx = env->features[FEAT_8000_0001_ECX];
        *edx = env->features[FEAT_8000_0001_EDX];

        /* The Linux kernel checks for the CMPLegacy bit and
         * discards multiple thread information if it is set.
         * So don't set it here for Intel to make Linux guests happy.
         */
        if (cs->nr_cores * cs->nr_threads > 1) {
            if (env->cpuid_vendor1 != CPUID_VENDOR_INTEL_1 ||
                env->cpuid_vendor2 != CPUID_VENDOR_INTEL_2 ||
                env->cpuid_vendor3 != CPUID_VENDOR_INTEL_3) {
                *ecx |= 1 << 1;    /* CmpLegacy bit */
            }
        }
#if __Use_Original_Qemu == 1 /* original QEMU (U460) */
        /* QEMU fd5dcb1ccd: Intel reports SYSCALL only in long mode */
        if (env->cpuid_vendor1 == CPUID_VENDOR_INTEL_1 &&
            !(env->hflags & HF_LMA_MASK)) {
            *edx &= ~CPUID_EXT2_SYSCALL;
        }
#else /* ours (U460) */
        /*
         * NoVmp (ledger U460): CPUID.80000001H:EDX[11] "SYSCALL/SYSRET available (when in
         * 64-bit mode)" (SDM Vol2A CPUID / ISE): Intel returns it only in 64-bit mode,
         * also 0 in compatibility mode (i5-13600K: EDX 2C100000h there, 2C100800h in 64-bit
         * mode), matching SYSCALL/SYSRET being #UD there.
         */
        if (env->cpuid_vendor1 == CPUID_VENDOR_INTEL_1 &&
            !(env->hflags & HF_CS64_MASK)) {
            *edx &= ~CPUID_EXT2_SYSCALL;
        }
#endif /* __Use_Original_Qemu (U460) */
        break;
    case 0x80000002:
    case 0x80000003:
    case 0x80000004:
        *eax = env->cpuid_model[(index - 0x80000002) * 4 + 0];
        *ebx = env->cpuid_model[(index - 0x80000002) * 4 + 1];
        *ecx = env->cpuid_model[(index - 0x80000002) * 4 + 2];
        *edx = env->cpuid_model[(index - 0x80000002) * 4 + 3];
        break;
    case 0x80000005:
        /* cache info (L1 cache) */
        if (cpu->cache_info_passthrough) {
            host_cpuid(index, 0, eax, ebx, ecx, edx);
            break;
        }
        *eax = (L1_DTLB_2M_ASSOC << 24) | (L1_DTLB_2M_ENTRIES << 16) | \
               (L1_ITLB_2M_ASSOC <<  8) | (L1_ITLB_2M_ENTRIES);
        *ebx = (L1_DTLB_4K_ASSOC << 24) | (L1_DTLB_4K_ENTRIES << 16) | \
               (L1_ITLB_4K_ASSOC <<  8) | (L1_ITLB_4K_ENTRIES);
        *ecx = encode_cache_cpuid80000005(env->cache_info_amd.l1d_cache);
        *edx = encode_cache_cpuid80000005(env->cache_info_amd.l1i_cache);
        break;
    case 0x80000006:
        /* cache info (L2 cache) */
        if (cpu->cache_info_passthrough) {
            host_cpuid(index, 0, eax, ebx, ecx, edx);
            break;
        }
        *eax = (AMD_ENC_ASSOC(L2_DTLB_2M_ASSOC) << 28) | \
               (L2_DTLB_2M_ENTRIES << 16) | \
               (AMD_ENC_ASSOC(L2_ITLB_2M_ASSOC) << 12) | \
               (L2_ITLB_2M_ENTRIES);
        *ebx = (AMD_ENC_ASSOC(L2_DTLB_4K_ASSOC) << 28) | \
               (L2_DTLB_4K_ENTRIES << 16) | \
               (AMD_ENC_ASSOC(L2_ITLB_4K_ASSOC) << 12) | \
               (L2_ITLB_4K_ENTRIES);
        encode_cache_cpuid80000006(env->cache_info_amd.l2_cache,
                                   cpu->enable_l3_cache ?
                                   env->cache_info_amd.l3_cache : NULL,
                                   ecx, edx);
        break;
    case 0x80000007:
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = env->features[FEAT_8000_0007_EDX];
        break;
    case 0x80000008:
        /* virtual & phys address size in low 2 bytes. */
        if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
            /* 64 bit processor */
            *eax = cpu->phys_bits; /* configurable physical bits */
            if  (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_LA57) {
                *eax |= 0x00003900; /* 57 bits virtual */
            } else {
                *eax |= 0x00003000; /* 48 bits virtual */
            }
        } else {
            *eax = cpu->phys_bits;
        }
        *ebx = env->features[FEAT_8000_0008_EBX];
        *ecx = 0;
        *edx = 0;
        if (cs->nr_cores * cs->nr_threads > 1) {
            *ecx |= (cs->nr_cores * cs->nr_threads) - 1;
        }
        break;
    case 0x8000000A:
        if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
            *eax = 0x00000001; /* SVM Revision */
            *ebx = 0x00000010; /* nr of ASIDs */
            *ecx = 0;
            *edx = env->features[FEAT_SVM]; /* optional features */
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0x8000001D:
        *eax = 0;
        if (cpu->cache_info_passthrough) {
            host_cpuid(index, count, eax, ebx, ecx, edx);
            break;
        }
        switch (count) {
        case 0: /* L1 dcache info */
            encode_cache_cpuid8000001d(env->cache_info_amd.l1d_cache,
                                       &topo_info, eax, ebx, ecx, edx);
            break;
        case 1: /* L1 icache info */
            encode_cache_cpuid8000001d(env->cache_info_amd.l1i_cache,
                                       &topo_info, eax, ebx, ecx, edx);
            break;
        case 2: /* L2 cache info */
            encode_cache_cpuid8000001d(env->cache_info_amd.l2_cache,
                                       &topo_info, eax, ebx, ecx, edx);
            break;
        case 3: /* L3 cache info */
            encode_cache_cpuid8000001d(env->cache_info_amd.l3_cache,
                                       &topo_info, eax, ebx, ecx, edx);
            break;
        default: /* end of info */
            *eax = *ebx = *ecx = *edx = 0;
            break;
        }
        break;
    case 0x8000001E:
        assert(cpu->core_id <= 255);
        encode_topo_cpuid8000001e(&topo_info, cpu, eax, ebx, ecx, edx);
        break;
    case 0xC0000000:
        *eax = env->cpuid_xlevel2;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xC0000001:
        /* Support for VIA CPU's CPUID instruction */
        *eax = env->cpuid_version;
        *ebx = 0;
        *ecx = 0;
        *edx = env->features[FEAT_C000_0001_EDX];
        break;
    case 0xC0000002:
    case 0xC0000003:
    case 0xC0000004:
        /* Reserved for the future, and now filled with zero */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    default:
        /* reserved values: zero */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    }
}

#if __Use_Original_Qemu != 1 /* ours (U370) */
/*
 * NoVmp (ledger U370): XSAVE state component i is supported by the CPU model: its own
 * feature bit, or AVX10 for components 5-7 (AVX10.2 spec 361050-007 3.1.3: AVX10 state is
 * enumerated in CPUID 0DH and enabled in XCR0 exactly as the AVX-512 state, Table 3.4).
 */
static bool x86_cpu_esa_supported(CPUX86State *env, int i)
{
    const ExtSaveArea *esa = &x86_ext_save_areas[i];

    if (env->features[esa->feature] & esa->bits) {
        return true;
    }
#if __Use_Original_Qemu != 1 /* ours (U756) */
    /* CET_U / CET_S: "CET_SS or CET_IBT" (QEMU 11.1 x86_ext_save_areas) */
    if ((i == XSTATE_CET_U_BIT || i == XSTATE_CET_S_BIT) &&
        (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_CET_IBT)) {
        return true;
    }
#endif /* __Use_Original_Qemu (U756) */
    return i >= XSTATE_OPMASK_BIT && i <= XSTATE_Hi16_ZMM_BIT &&
           (env->features[FEAT_7_1_EDX] & CPUID_7_1_EDX_AVX10);
}

#endif /* __Use_Original_Qemu (U370) */
static void x86_cpu_reset(CPUState *dev)
{
    CPUState *s = CPU(dev);
    X86CPU *cpu = X86_CPU(s);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);
    CPUX86State *env = &cpu->env;
    target_ulong cr4;
    uint64_t xcr0;
    int i;

    xcc->parent_reset(s);

    memset(env, 0, offsetof(CPUX86State, end_reset_fields));

    env->old_exception = -1;

    /* init to reset state */

    env->hflags2 |= HF2_GIF_MASK;

    cpu_x86_update_cr0(env, 0x60000010);
    env->a20_mask = ~0x0;
    env->smbase = 0x30000;
    env->msr_smi_count = 0;

    env->idt.limit = 0xffff;
    env->gdt.limit = 0xffff;
    env->ldt.limit = 0xffff;
    env->ldt.flags = DESC_P_MASK | (2 << DESC_TYPE_SHIFT);
    env->tr.limit = 0xffff;
    env->tr.flags = DESC_P_MASK | (11 << DESC_TYPE_SHIFT);

    cpu_x86_load_seg_cache(env, R_CS, 0xf000, 0xffff0000, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                           DESC_R_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_SS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);

    env->eip = 0xfff0;
    env->regs[R_EDX] = env->cpuid_version;

    env->eflags = 0x2;

    /* FPU init */
    for (i = 0; i < 8; i++) {
        env->fptags[i] = 1;
    }
    cpu_set_fpuc(env, 0x37f);

    env->mxcsr = 0x1f80;
#if __Use_Original_Qemu != 1 /* ours (U96) */
    /*
     * NoVmp (ledger U96): the memset above cleared sse_status; resync it with MXCSR
     * (rounding, DAZ/FTZ and the SSE NaN rule use_first_nan) on RESET / INIT too.
     */
    update_mxcsr_status(env);
#endif /* __Use_Original_Qemu (U96) */
    /* All units are in INIT state.  */
    env->xstate_bv = 0;

    env->pat = 0x0007040600070406ULL;
    env->msr_ia32_misc_enable = MSR_IA32_MISC_ENABLE_DEFAULT;
    if (env->features[FEAT_1_ECX] & CPUID_EXT_MONITOR) {
        env->msr_ia32_misc_enable |= MSR_IA32_MISC_ENABLE_MWAIT;
    }

    memset(env->dr, 0, sizeof(env->dr));
    env->dr[6] = DR6_FIXED_1;
    env->dr[7] = DR7_FIXED_1;
    cpu_breakpoint_remove_all(s, BP_CPU);
    cpu_watchpoint_remove_all(s, BP_CPU);

    cr4 = 0;
    xcr0 = XSTATE_FP_MASK;

    /* Enable all the features for user-mode.  */
    if (env->features[FEAT_1_EDX] & CPUID_SSE) {
        xcr0 |= XSTATE_SSE_MASK;
    }
    for (i = 2; i < ARRAY_SIZE(x86_ext_save_areas); i++) {
        const ExtSaveArea *esa = &x86_ext_save_areas[i];
        if (!((1ULL << i) & CPUID_XSTATE_XCR0_MASK)) {
            continue;
        }
#if __Use_Original_Qemu == 1 /* original QEMU (U370) */
        if (env->features[esa->feature] & esa->bits) {
#else /* ours (U370) */
        (void)esa;
        if (x86_cpu_esa_supported(env, i)) {
#endif /* __Use_Original_Qemu (U370) */
            xcr0 |= 1ull << i;
        }
    }

    if (env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE) {
#if __Use_Original_Qemu == 1 /* original QEMU (U67) */
        cr4 |= CR4_OSFXSR_MASK | CR4_OSXSAVE_MASK;
#else /* ours (U67) */
        /* an OS that enables SSE also enables #XM (Windows, Linux) */
        cr4 |= CR4_OSFXSR_MASK | CR4_OSXMMEXCPT_MASK | CR4_OSXSAVE_MASK;
#endif /* __Use_Original_Qemu (U67) */
    }
    if (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_FSGSBASE) {
        cr4 |= CR4_FSGSBASE_MASK;
    }

#if __Use_Original_Qemu != 1 /* ours (U120) */
    /* reset XCR0 within the CPUID profile's leaf 0DH (what XSETBV accepts) */
    xcr0 = x86_cpu_xcr0_in_profile(env, xcr0);
#endif /* __Use_Original_Qemu (U120) */
    env->xcr0 = xcr0;
    cpu_x86_update_cr4(env, cr4);

    /*
     * SDM 11.11.5 requires:
     *  - IA32_MTRR_DEF_TYPE MSR.E = 0
     *  - IA32_MTRR_PHYSMASKn.V = 0
     * All other bits are undefined.  For simplification, zero it all.
     */
    env->mtrr_deftype = 0;
    memset(env->mtrr_var, 0, sizeof(env->mtrr_var));
    memset(env->mtrr_fixed, 0, sizeof(env->mtrr_fixed));
}

static void mce_init(X86CPU *cpu)
{
    CPUX86State *cenv = &cpu->env;
    unsigned int bank;

    if (((cenv->cpuid_version >> 8) & 0xf) >= 6
        && (cenv->features[FEAT_1_EDX] & (CPUID_MCE | CPUID_MCA)) ==
            (CPUID_MCE | CPUID_MCA)) {
        cenv->mcg_cap = MCE_CAP_DEF | MCE_BANKS_DEF |
                        (cpu->enable_lmce ? MCG_LMCE_P : 0);
        cenv->mcg_ctl = ~(uint64_t)0;
        for (bank = 0; bank < MCE_BANKS_DEF; bank++) {
            cenv->mce_banks[bank * 4] = ~(uint64_t)0;
        }
    }
}

static void x86_cpu_adjust_level(X86CPU *cpu, uint32_t *min, uint32_t value)
{
    if (*min < value) {
        *min = value;
    }
}

/* Increase cpuid_min_{level,xlevel,xlevel2} automatically, if appropriate */
static void x86_cpu_adjust_feat_level(X86CPU *cpu, FeatureWord w)
{
    CPUX86State *env = &cpu->env;
    FeatureWordInfo *fi = &feature_word_info[w];
    uint32_t eax = fi->cpuid.eax;
    uint32_t region = eax & 0xF0000000;

    assert(feature_word_info[w].type == CPUID_FEATURE_WORD);
    if (!env->features[w]) {
        return;
    }

    switch (region) {
    case 0x00000000:
        x86_cpu_adjust_level(cpu, &env->cpuid_min_level, eax);
    break;
    case 0x80000000:
        x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel, eax);
    break;
    case 0xC0000000:
        x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel2, eax);
    break;
    }

    if (eax == 7) {
        x86_cpu_adjust_level(cpu, &env->cpuid_min_level_func7,
                             fi->cpuid.ecx);
    }
}

/* Calculate XSAVE components based on the configured CPU feature flags */
static void x86_cpu_enable_xsave_components(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    int i;
    uint64_t mask;

    if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE)) {
        env->features[FEAT_XSAVE_XCR0_LO] = 0;
        env->features[FEAT_XSAVE_XCR0_HI] = 0;
        env->features[FEAT_XSAVE_XSS_LO] = 0;
        env->features[FEAT_XSAVE_XSS_HI] = 0;
        return;
    }

    mask = 0;
    for (i = 0; i < ARRAY_SIZE(x86_ext_save_areas); i++) {
        const ExtSaveArea *esa = &x86_ext_save_areas[i];
#if __Use_Original_Qemu == 1 /* original QEMU (U370) */
        if (env->features[esa->feature] & esa->bits) {
#else /* ours (U370) */
        (void)esa;
        if (x86_cpu_esa_supported(env, i)) {
#endif /* __Use_Original_Qemu (U370) */
            mask |= (1ULL << i);
        }
    }

    env->features[FEAT_XSAVE_XCR0_LO] = mask & CPUID_XSTATE_XCR0_MASK;
    env->features[FEAT_XSAVE_XCR0_HI] = (mask & CPUID_XSTATE_XCR0_MASK) >> 32;
    env->features[FEAT_XSAVE_XSS_LO] = mask & CPUID_XSTATE_XSS_MASK;
    env->features[FEAT_XSAVE_XSS_HI] = (mask & CPUID_XSTATE_XSS_MASK) >> 32;
}

/***** Steps involved on loading and filtering CPUID data
 *
 * When initializing and realizing a CPU object, the steps
 * involved in setting up CPUID data are:
 *
 * 1) Loading CPU model definition (X86CPUDefinition). This is
 *    implemented by x86_cpu_load_model() and should be completely
 *    transparent, as it is done automatically by instance_init.
 *    No code should need to look at X86CPUDefinition structs
 *    outside instance_init.
 *
 * 2) CPU expansion. This is done by realize before CPUID
 *    filtering, and will make sure host/accelerator data is
 *    loaded for CPU models that depend on host capabilities
 *    (e.g. "host"). Done by x86_cpu_expand_features().
 *
 * 3) CPUID filtering. This initializes extra data related to
 *    CPUID, and checks if the host supports all capabilities
 *    required by the CPU. Runnability of a CPU model is
 *    determined at this step. Done by x86_cpu_filter_features().
 *
 * Some operations don't require all steps to be performed.
 * More precisely:
 *
 * - CPU instance creation (instance_init) will run only CPU
 *   model loading. CPU expansion can't run at instance_init-time
 *   because host/accelerator data may be not available yet.
 * - CPU realization will perform both CPU model expansion and CPUID
 *   filtering, and return an error in case one of them fails.
 * - query-cpu-definitions needs to run all 3 steps. It needs
 *   to run CPUID filtering, as the 'unavailable-features'
 *   field is set based on the filtering results.
 * - The query-cpu-model-expansion QMP command only needs to run
 *   CPU model loading and CPU expansion. It should not filter
 *   any CPUID data based on host capabilities.
 */

/* Expand CPU configuration data, based on configured features
 * and host/accelerator capabilities when appropriate.
 */
static void x86_cpu_expand_features(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    FeatureWord w;

    /*TODO: Now cpu->max_features doesn't overwrite features
     * set using QOM properties, and we can convert
     * plus_features & minus_features to global properties
     * inside x86_cpu_parse_featurestr() too.
     */
    if (cpu->max_features) {
        for (w = 0; w < FEATURE_WORDS; w++) {
            /* Override only features that weren't set explicitly
             * by the user.
             */
            env->features[w] |=
                x86_cpu_get_supported_feature_word(w, cpu->migratable) &
                ~env->user_features[w] & \
                ~feature_word_info[w].no_autoenable_flags;
        }
    }

    env->features[FEAT_KVM] = 0;

    x86_cpu_enable_xsave_components(cpu);

    /* CPUID[EAX=7,ECX=0].EBX always increased level automatically: */
    x86_cpu_adjust_feat_level(cpu, FEAT_7_0_EBX);
    if (cpu->full_cpuid_auto_level) {
        x86_cpu_adjust_feat_level(cpu, FEAT_1_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_1_ECX);
        x86_cpu_adjust_feat_level(cpu, FEAT_6_EAX);
        x86_cpu_adjust_feat_level(cpu, FEAT_7_0_ECX);
        x86_cpu_adjust_feat_level(cpu, FEAT_7_1_EAX);
#if __Use_Original_Qemu != 1 /* ours (U85) */
        x86_cpu_adjust_feat_level(cpu, FEAT_7_1_EDX);
#endif /* __Use_Original_Qemu (U85) */
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0001_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0001_ECX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0007_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0008_EBX);
        x86_cpu_adjust_feat_level(cpu, FEAT_C000_0001_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_SVM);
        x86_cpu_adjust_feat_level(cpu, FEAT_XSAVE);

        /* Intel Processor Trace requires CPUID[0x14] */
        if ((env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_INTEL_PT)) {
            if (cpu->intel_pt_auto_level) {
                x86_cpu_adjust_level(cpu, &cpu->env.cpuid_min_level, 0x14);
            } else if (cpu->env.cpuid_min_level < 0x14) {
                // TODO: Add a warning?
                // mark_unavailable_features(cpu, FEAT_7_0_EBX,
                //     CPUID_7_0_EBX_INTEL_PT,
                //     "Intel PT need CPUID leaf 0x14, please set by \"-cpu ...,+intel-pt,level=0x14\"");
            }
        }

#if __Use_Original_Qemu != 1 /* ours (U100) */
        /* Key Locker requires CPUID[0x19] */
        if (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_KeyLocker) {
            x86_cpu_adjust_level(cpu, &env->cpuid_min_level, 0x19);
        }
#endif /* __Use_Original_Qemu (U100) */
        /* CPU topology with multi-dies support requires CPUID[0x1F] */
        if (env->nr_dies > 1) {
            x86_cpu_adjust_level(cpu, &env->cpuid_min_level, 0x1F);
        }

        /* SVM requires CPUID[0x8000000A] */
        if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
            x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel, 0x8000000A);
        }

        /* SEV requires CPUID[0x8000001F] */
        // if (sev_enabled()) {
        //     x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel, 0x8000001F);
        // }
    }

    /* Set cpuid_*level* based on cpuid_min_*level, if not explicitly set */
    if (env->cpuid_level_func7 == UINT32_MAX) {
        env->cpuid_level_func7 = env->cpuid_min_level_func7;
    }
    if (env->cpuid_level == UINT32_MAX) {
        env->cpuid_level = env->cpuid_min_level;
    }
    if (env->cpuid_xlevel == UINT32_MAX) {
        env->cpuid_xlevel = env->cpuid_min_xlevel;
    }
    if (env->cpuid_xlevel2 == UINT32_MAX) {
        env->cpuid_xlevel2 = env->cpuid_min_xlevel2;
    }
}

/*
 * Finishes initialization of CPUID data, filters CPU feature
 * words based on host availability of each feature.
 *
 * Returns: 0 if all flags are supported by the host, non-zero otherwise.
 */
static void x86_cpu_filter_features(X86CPU *cpu, bool verbose)
{
    CPUX86State *env = &cpu->env;
    FeatureWord w;
    const char *prefix = NULL;

    for (w = 0; w < FEATURE_WORDS; w++) {
        uint64_t host_feat =
            x86_cpu_get_supported_feature_word(w, false);
        uint64_t requested_features = env->features[w];
        uint64_t unavailable_features = requested_features & ~host_feat;
        mark_unavailable_features(cpu, w, unavailable_features, prefix);
    }
}

static void x86_cpu_realizefn(struct uc_struct *uc, CPUState *dev)
{
    CPUState *cs = CPU(dev);
    X86CPU *cpu = X86_CPU(cs);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cs);
    CPUX86State *env = &cpu->env;

    if (cpu->ucode_rev == 0) {
        /* The default is the same as KVM's.  */
        if (IS_AMD_CPU(env)) {
            cpu->ucode_rev = 0x01000065;
        } else {
            cpu->ucode_rev = 0x100000000ULL;
        }
    }

    /* mwait extended info: needed for Core compatibility */
    /* We always wake on interrupt even if host does not have the capability */
    cpu->mwait.ecx |= CPUID_MWAIT_EMX | CPUID_MWAIT_IBE;

    if (cpu->apic_id == UNASSIGNED_APIC_ID) {
        //error_setg(errp, "apic-id property was not initialized properly");
        return;
    }

    x86_cpu_expand_features(cpu);

    x86_cpu_filter_features(cpu, cpu->check_cpuid || cpu->enforce_cpuid);

#if __Use_Original_Qemu != 1 /* ours (U120) */
    /*
     * NoVmp (ledger U120): UC_CTL_X86_AVX512 opts in to AVX512F after the TCG filter
     * (which drops it), so the recomputation below derives XSAVE components 5-7
     * (opmask, ZMM_Hi256, Hi16_ZMM) for CPUID 0DH, XSETBV and reset XCR0.
     */
    if (uc->x86_avx512) {
        env->features[FEAT_7_0_EBX] |= CPUID_7_0_EBX_AVX512F;
    }
#endif /* __Use_Original_Qemu (U120) */
#if __Use_Original_Qemu != 1 /* ours (U128) */
    /*
     * NoVmp (ledger U128): UC_X86_AVX512_DQ / UC_X86_AVX512_BW in the UC_CTL_X86_AVX512
     * mask add CPUID.(EAX=7,ECX=0):EBX.AVX512DQ[17] / AVX512BW[30] (the opmask B and
     * D/Q forms, SDM Vol2A KADD..KXOR). The translator's copy is narrowed by a strict
     * CPUID profile as for every other leaf-7 bit (U68).
     */
    if (uc->x86_avx512 & UC_X86_AVX512_DQ) {
        env->features[FEAT_7_0_EBX] |= CPUID_7_0_EBX_AVX512DQ;
    }
    if (uc->x86_avx512 & UC_X86_AVX512_BW) {
        env->features[FEAT_7_0_EBX] |= CPUID_7_0_EBX_AVX512BW;
    }
#endif /* __Use_Original_Qemu (U128) */
#if __Use_Original_Qemu != 1 /* ours (U170) */
    /*
     * NoVmp (ledger U170): UC_CTL_X86_AMX opts in to Intel AMX after the TCG filter:
     * AMX_TILE (and the requested AMX_INT8/BF16/FP16/COMPLEX bits), so the U37
     * recomputation below derives XSAVE components 17-18 (TILECFG, TILEDATA); XFD
     * (CPUID.(EAX=0DH,ECX=1):EAX[4], SDM Vol1 13.14) for TILEDATA; leaves 1DH/1EH
     * need a basic level of at least 1EH (leaf 7 subleaf 1 for AMX_FP16/COMPLEX).
     */
    if (uc->x86_amx) {
        env->features[FEAT_7_0_EDX] |= CPUID_7_0_EDX_AMX_TILE;
        if (uc->x86_amx & UC_X86_AMX_INT8) {
            env->features[FEAT_7_0_EDX] |= CPUID_7_0_EDX_AMX_INT8;
        }
        if (uc->x86_amx & UC_X86_AMX_BF16) {
            env->features[FEAT_7_0_EDX] |= CPUID_7_0_EDX_AMX_BF16;
        }
        if (uc->x86_amx & UC_X86_AMX_FP16) {
            env->features[FEAT_7_1_EAX] |= CPUID_7_1_EAX_AMX_FP16;
        }
        if (uc->x86_amx & UC_X86_AMX_COMPLEX) {
            env->features[FEAT_7_1_EDX] |= CPUID_7_1_EDX_AMX_COMPLEX;
        }
        if (uc->x86_amx & (UC_X86_AMX_FP16 | UC_X86_AMX_COMPLEX)) {
            env->cpuid_level_func7 = MAX(env->cpuid_level_func7, 1);
        }
        env->features[FEAT_XSAVE] |= CPUID_D_1_EAX_XFD;
        env->cpuid_level = MAX(env->cpuid_level, 0x1e);
    }
#endif /* __Use_Original_Qemu (U170) */
#if __Use_Original_Qemu != 1 /* ours (U140) */
    /*
     * NoVmp (ledger U140): UC_X86_AVX512_VL adds CPUID.(EAX=7,ECX=0):EBX.AVX512VL[31]:
     * the EVEX.128/256 forms of AVX512F/DQ/BW instructions need it (SDM Vol2A "(AVX512VL
     * AND AVX512F) OR AVX10.1" CPUID columns). A strict profile narrows it like DQ/BW.
     */
    if (uc->x86_avx512 & UC_X86_AVX512_VL) {
        env->features[FEAT_7_0_EBX] |= CPUID_7_0_EBX_AVX512VL;
    }
#endif /* __Use_Original_Qemu (U140) */
#if __Use_Original_Qemu != 1 /* ours (U320) */
    /*
     * NoVmp (ledger U320): UC_X86_AVX512_CD adds CPUID.(EAX=7,ECX=0):EBX.AVX512CD[28]
     * (VPCONFLICTD/Q, VPLZCNTD/Q, VPBROADCASTMB2Q/MW2D; SDM Vol2C "AVX512CD OR AVX10.1",
     * the EVEX.128/256 forms also need AVX512VL). A strict CPUID profile narrows the
     * translator's copy as for every leaf-7 bit (U68).
     */
    if (uc->x86_avx512 & UC_X86_AVX512_CD) {
        env->features[FEAT_7_0_EBX] |= CPUID_7_0_EBX_AVX512CD;
    }
#endif /* __Use_Original_Qemu (U320) */
#if __Use_Original_Qemu != 1 /* ours (U322) */
    /* NoVmp (ledger U322): UC_X86_AVX512_IFMA adds CPUID.(EAX=7,ECX=0):EBX.AVX512_IFMA[21] */
    if (uc->x86_avx512 & UC_X86_AVX512_IFMA) {
        env->features[FEAT_7_0_EBX] |= CPUID_7_0_EBX_AVX512IFMA;
    }
#endif /* __Use_Original_Qemu (U322) */
#if __Use_Original_Qemu != 1 /* ours (U323) */
    /* NoVmp (ledger U323): UC_X86_AVX512_VPOPCNTDQ adds CPUID.(EAX=7,ECX=0):ECX[14] */
    if (uc->x86_avx512 & UC_X86_AVX512_VPOPCNTDQ) {
        env->features[FEAT_7_0_ECX] |= CPUID_7_0_ECX_AVX512_VPOPCNTDQ;
    }
#endif /* __Use_Original_Qemu (U323) */
#if __Use_Original_Qemu != 1 /* ours (U324) */
    /* NoVmp (ledger U324): UC_X86_AVX512_BITALG adds CPUID.(EAX=7,ECX=0):ECX[12] */
    if (uc->x86_avx512 & UC_X86_AVX512_BITALG) {
        env->features[FEAT_7_0_ECX] |= CPUID_7_0_ECX_AVX512BITALG;
    }
#endif /* __Use_Original_Qemu (U324) */
#if __Use_Original_Qemu != 1 /* ours (U325) */
    /* NoVmp (ledger U325): UC_X86_AVX512_VBMI adds CPUID.(EAX=7,ECX=0):ECX[1] */
    if (uc->x86_avx512 & UC_X86_AVX512_VBMI) {
        env->features[FEAT_7_0_ECX] |= CPUID_7_0_ECX_AVX512_VBMI;
    }
#endif /* __Use_Original_Qemu (U325) */
#if __Use_Original_Qemu != 1 /* ours (U330) */
    /*
     * NoVmp (ledger U330): UC_X86_AVX512_FP16 adds CPUID.(EAX=7,ECX=0):EDX.AVX512_FP16[23]
     * (EVEX maps 5 and 6, SDM Vol2C VADDPH..VUCOMISH; uc_ctl already added AVX512BW, which
     * the FP16 extensions require, SDM Vol1 15.2.2). A strict profile narrows it like DQ/BW.
     */
    if (uc->x86_avx512 & UC_X86_AVX512_FP16) {
        env->features[FEAT_7_0_EDX] |= CPUID_7_0_EDX_AVX512_FP16;
    }
#endif /* __Use_Original_Qemu (U330) */
#if __Use_Original_Qemu != 1 /* ours (U570) */
    /*
     * NoVmp (ledger U570): UC_X86_AVX512_VP2INTERSECT adds CPUID.(EAX=7,ECX=0):EDX.
     * AVX512_VP2INTERSECT[8] (SDM Vol2C VP2INTERSECTD/Q). A strict profile narrows the
     * translator's copy as for every leaf-7 bit (U68).
     */
    if (uc->x86_avx512 & UC_X86_AVX512_VP2INTERSECT) {
        env->features[FEAT_7_0_EDX] |= CPUID_7_0_EDX_AVX512_VP2INTERSECT;
    }
#endif /* __Use_Original_Qemu (U570) */
#if __Use_Original_Qemu != 1 /* ours (U550) */
    /* NoVmp (ledger U550): UC_X86_AVX512_VBMI2 adds CPUID.(EAX=7,ECX=0):ECX.AVX512_VBMI2[6] */
    if (uc->x86_avx512 & UC_X86_AVX512_VBMI2) {
        env->features[FEAT_7_0_ECX] |= CPUID_7_0_ECX_AVX512_VBMI2;
    }
#endif /* __Use_Original_Qemu (U550) */
#if __Use_Original_Qemu != 1 /* ours (U555) */
    /* NoVmp (ledger U555): UC_X86_AVX512_VNNI adds CPUID.(EAX=7,ECX=0):ECX.AVX512_VNNI[11] */
    if (uc->x86_avx512 & UC_X86_AVX512_VNNI) {
        env->features[FEAT_7_0_ECX] |= CPUID_7_0_ECX_AVX512VNNI;
    }
#endif /* __Use_Original_Qemu (U555) */
#if __Use_Original_Qemu != 1 /* ours (U556) */
    /*
     * NoVmp (ledger U556): UC_X86_AVX512_BF16 adds CPUID.(EAX=7,ECX=1):EAX.AVX512_BF16[5]
     * (subleaf 1 needs CPUID.(EAX=7,ECX=0):EAX >= 1). A strict profile narrows it (U68).
     */
    if (uc->x86_avx512 & UC_X86_AVX512_BF16) {
        env->features[FEAT_7_1_EAX] |= CPUID_7_1_EAX_AVX512_BF16;
        env->cpuid_level_func7 = MAX(env->cpuid_level_func7, 1);
    }
#endif /* __Use_Original_Qemu (U556) */
#if __Use_Original_Qemu != 1 /* ours (U370) */
    /*
     * NoVmp (ledger U370): UC_CTL_X86_AVX10 opts in to AVX10 after the TCG filter:
     * CPUID.(EAX=7,ECX=1):EDX.AVX10[19] (leaf-7 level >= 1) and leaf 24H (basic level >=
     * 24H); the U37 recomputation below then derives state components 5-7 from it
     * (x86_cpu_esa_supported) for CPUID 0DH, XSETBV and reset XCR0.
     */
    if (uc->x86_avx10) {
        env->features[FEAT_7_1_EDX] |= CPUID_7_1_EDX_AVX10;
        env->cpuid_level_func7 = MAX(env->cpuid_level_func7, 1);
        env->cpuid_level = MAX(env->cpuid_level, 0x24);
    }
#endif /* __Use_Original_Qemu (U370) */
#if __Use_Original_Qemu != 1 /* ours (U610) */
    /*
     * NoVmp (ledger U610): UC_CTL_X86_APX opts in to Intel APX after the TCG filter (APX spec
     * 355828-009 3.1.4.3.1): CPUID.(EAX=7,ECX=1):EDX.APX_F[21] (leaf-7 level >= 1) and the
     * APX leaf 29H (basic level >= 29H). "No processor will enumerate support for both Intel
     * MPX and Intel APX" (3.1.4.3.3): MPX (7.0:EBX[14], state components 3-4) is withdrawn,
     * so the U37 recomputation below derives component 19 in place of 3-4 at offset 3C0H.
     * "Any Intel processor that enumerates support for APX_F ... will also enumerate CMOV".
     */
    if (uc->x86_apx) {
        env->features[FEAT_7_1_EDX] |= CPUID_7_1_EDX_APX_F;
        env->features[FEAT_7_0_EBX] &= ~CPUID_7_0_EBX_MPX;
        env->features[FEAT_1_EDX] |= CPUID_CMOV;
        env->cpuid_level_func7 = MAX(env->cpuid_level_func7, 1);
        env->cpuid_level = MAX(env->cpuid_level, 0x29);
    }
#endif /* __Use_Original_Qemu (U610) */
#if __Use_Original_Qemu != 1 /* ours (U37) */
    /*
     * Unicorn: recompute the XSAVE component masks from the *filtered* features.
     * expand_features() derived them from the unfiltered model, so e.g. Icelake's
     * AVX512F advertised opmask/ZMM components (CPUID 0xD, XSETBV) that TCG can
     * neither execute nor save.
     */
    x86_cpu_enable_xsave_components(cpu);

#endif /* __Use_Original_Qemu (U37) */
    if (cpu->enforce_cpuid && x86_cpu_have_filtered_features(cpu)) {
        // error_setg(&local_err,
        //           accel_uses_host_cpuid() ?
        //               "Host doesn't support requested features" :
        //               "TCG doesn't support requested features");
        return;
    }

    /* On AMD CPUs, some CPUID[8000_0001].EDX bits must match the bits on
     * CPUID[1].EDX.
     */
    if (IS_AMD_CPU(env)) {
        env->features[FEAT_8000_0001_EDX] &= ~CPUID_EXT2_AMD_ALIASES;
        env->features[FEAT_8000_0001_EDX] |= (env->features[FEAT_1_EDX]
           & CPUID_EXT2_AMD_ALIASES);
    }

    /* For 64bit systems think about the number of physical bits to present.
     * ideally this should be the same as the host; anything other than matching
     * the host can cause incorrect guest behaviour.
     * QEMU used to pick the magic value of 40 bits that corresponds to
     * consumer AMD devices but nothing else.
     */
    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        if (cpu->phys_bits && cpu->phys_bits != TCG_PHYS_ADDR_BITS) {
            //error_setg(errp, "TCG only supports phys-bits=%u",
            //                  TCG_PHYS_ADDR_BITS);
            return;
        }

        /* 0 means it was not explicitly set by the user (or by machine
         * compat_props or by the host code above). In this case, the default
         * is the value used by TCG (40).
         */
        if (cpu->phys_bits == 0) {
            cpu->phys_bits = TCG_PHYS_ADDR_BITS;
        }
    } else {
        /* For 32 bit systems don't use the user set value, but keep
         * phys_bits consistent with what we tell the guest.
         */
        if (cpu->phys_bits != 0) {
            //error_setg(errp, "phys-bits is not user-configurable in 32 bit");
            return;
        }

        if (env->features[FEAT_1_EDX] & CPUID_PSE36) {
            cpu->phys_bits = 36;
        } else {
            cpu->phys_bits = 32;
        }
    }
#if __Use_Original_Qemu != 1 /* ours (U593) */
    x86_cpu_update_phys_bits(cpu);   /* a UC_CTL_X86_CPUID profile set before init */
#endif /* __Use_Original_Qemu (U593) */

    /* Cache information initialization */
    if (!cpu->legacy_cache) {
        if (!xcc->model || !xcc->model->cpudef->cache_info) {
            // g_autofree char *name = x86_cpu_class_get_model_name(xcc);
            //error_setg(errp,
            //           "CPU model '%s' doesn't support legacy-cache=off", name);
            return;
        }
        env->cache_info_cpuid2 = env->cache_info_cpuid4 = env->cache_info_amd =
            *xcc->model->cpudef->cache_info;
    } else {
        /* Build legacy cache information */
        env->cache_info_cpuid2.l1d_cache = &legacy_l1d_cache;
        env->cache_info_cpuid2.l1i_cache = &legacy_l1i_cache;
        env->cache_info_cpuid2.l2_cache = &legacy_l2_cache_cpuid2;
        env->cache_info_cpuid2.l3_cache = &legacy_l3_cache;

        env->cache_info_cpuid4.l1d_cache = &legacy_l1d_cache;
        env->cache_info_cpuid4.l1i_cache = &legacy_l1i_cache;
        env->cache_info_cpuid4.l2_cache = &legacy_l2_cache;
        env->cache_info_cpuid4.l3_cache = &legacy_l3_cache;

        env->cache_info_amd.l1d_cache = &legacy_l1d_cache_amd;
        env->cache_info_amd.l1i_cache = &legacy_l1i_cache_amd;
        env->cache_info_amd.l2_cache = &legacy_l2_cache_amd;
        env->cache_info_amd.l3_cache = &legacy_l3_cache;
    }

    cpu_exec_realizefn(cs);

    mce_init(cpu);

    cpu_reset(cs);
}

static void x86_cpu_initfn(struct uc_struct *uc, CPUState *obj)
{
    X86CPU *cpu = X86_CPU(obj);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(obj);
    CPUX86State *env = &cpu->env;

    env->nr_dies = 1;
    env->nr_nodes = 1;
    cpu_set_cpustate_pointers(cpu);
    env->uc = uc;

    if (xcc->model) {
        x86_cpu_load_model(cpu, xcc->model);
    }
}

static int64_t x86_cpu_get_arch_id(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    return cpu->apic_id;
}

static bool x86_cpu_get_paging_enabled(const CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    return cpu->env.cr[0] & CR0_PG_MASK;
}

static void x86_cpu_set_pc(CPUState *cs, vaddr value)
{
    X86CPU *cpu = X86_CPU(cs);

    cpu->env.eip = value;
}

static void x86_cpu_synchronize_from_tb(CPUState *cs, TranslationBlock *tb)
{
    X86CPU *cpu = X86_CPU(cs);

    /* QEMU 15e207b9ed */
    if (tb->flags & HF_CS64_MASK) {
        cpu->env.eip = tb->pc;
    } else {
        cpu->env.eip = (uint32_t)(tb->pc - tb->cs_base);
    }
}

int x86_cpu_pending_interrupt(CPUState *cs, int interrupt_request)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (interrupt_request & CPU_INTERRUPT_POLL) {
        return CPU_INTERRUPT_POLL;
    }

    if (interrupt_request & CPU_INTERRUPT_SIPI) {
        return CPU_INTERRUPT_SIPI;
    }

    if (env->hflags2 & HF2_GIF_MASK) {
        if ((interrupt_request & CPU_INTERRUPT_SMI) &&
            !(env->hflags & HF_SMM_MASK)) {
            return CPU_INTERRUPT_SMI;
        } else if ((interrupt_request & CPU_INTERRUPT_NMI) &&
                   !(env->hflags2 & HF2_NMI_MASK)) {
            return CPU_INTERRUPT_NMI;
        } else if (interrupt_request & CPU_INTERRUPT_MCE) {
            return CPU_INTERRUPT_MCE;
        } else if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                   (((env->hflags2 & HF2_VINTR_MASK) &&
                     (env->hflags2 & HF2_HIF_MASK)) ||
                    (!(env->hflags2 & HF2_VINTR_MASK) &&
                     (env->eflags & IF_MASK &&
                      !(env->hflags & HF_INHIBIT_IRQ_MASK))))) {
            return CPU_INTERRUPT_HARD;
        } else if ((interrupt_request & CPU_INTERRUPT_VIRQ) &&
                   (env->eflags & IF_MASK) &&
                   !(env->hflags & HF_INHIBIT_IRQ_MASK)) {
            return CPU_INTERRUPT_VIRQ;
        }
    }
#if __Use_Original_Qemu != 1 /* ours (U104) */
    /* user-interrupt delivery: priority just below ordinary interrupts (SDM Vol3A 9.4.2) */
    if ((interrupt_request & CPU_INTERRUPT_UINTR) && x86_uintr_deliverable(env)) {
        return CPU_INTERRUPT_UINTR;
    }
#endif /* __Use_Original_Qemu (U104) */

    return 0;
}

static bool x86_cpu_has_work(CPUState *cs)
{
    return x86_cpu_pending_interrupt(cs, cs->interrupt_request) != 0;
}

void x86_update_hflags(CPUX86State *env)
{
   uint32_t hflags;
#define HFLAG_COPY_MASK \
    ~( HF_CPL_MASK | HF_PE_MASK | HF_MP_MASK | HF_EM_MASK | \
       HF_TS_MASK | HF_TF_MASK | HF_VM_MASK | HF_IOPL_MASK | \
       HF_OSFXSR_MASK | HF_LMA_MASK | HF_CS32_MASK | \
       HF_SS32_MASK | HF_CS64_MASK | HF_ADDSEG_MASK)

    hflags = env->hflags & HFLAG_COPY_MASK;
    hflags |= (env->segs[R_SS].flags >> DESC_DPL_SHIFT) & HF_CPL_MASK;
    hflags |= (env->cr[0] & CR0_PE_MASK) << (HF_PE_SHIFT - CR0_PE_SHIFT);
    hflags |= (env->cr[0] << (HF_MP_SHIFT - CR0_MP_SHIFT)) &
                (HF_MP_MASK | HF_EM_MASK | HF_TS_MASK);
    hflags |= (env->eflags & (HF_TF_MASK | HF_VM_MASK | HF_IOPL_MASK));

    if (env->cr[4] & CR4_OSFXSR_MASK) {
        hflags |= HF_OSFXSR_MASK;
    }

    if (env->efer & MSR_EFER_LMA) {
        hflags |= HF_LMA_MASK;
    }

    if ((hflags & HF_LMA_MASK) && (env->segs[R_CS].flags & DESC_L_MASK)) {
        hflags |= HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK;
    } else {
        hflags |= (env->segs[R_CS].flags & DESC_B_MASK) >>
                    (DESC_B_SHIFT - HF_CS32_SHIFT);
        hflags |= (env->segs[R_SS].flags & DESC_B_MASK) >>
                    (DESC_B_SHIFT - HF_SS32_SHIFT);
        if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK) ||
            !(hflags & HF_CS32_MASK)) {
            hflags |= HF_ADDSEG_MASK;
        } else {
            hflags |= ((env->segs[R_DS].base | env->segs[R_ES].base |
                        env->segs[R_SS].base) != 0) << HF_ADDSEG_SHIFT;
        }
    }
    env->hflags = hflags;
#if __Use_Original_Qemu != 1 /* ours (U114) */
    cpu_sync_cet_hflags(env);
#endif /* __Use_Original_Qemu (U114) */
}

/*
 * backport 7174cd2eec (U490): outside 64-bit mode (legacy and compatibility mode) a linear
 * address is 32 bits, so a multi-byte access that crosses FFFFFFFFh continues at 0 (SDM
 * Vol3A 3.4 / Vol1 3.3.7: 64-bit addresses only in 64-bit mode). No MMU-index split.
 */
static vaddr x86_pointer_wrap(CPUState *cs, int mmu_idx, vaddr result, vaddr base)
{
    return (X86_CPU(cs)->env.hflags & HF_CS64_MASK) ? result : (uint32_t)result;
}

static void x86_cpu_common_class_init(struct uc_struct *uc, CPUClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);

    /* parent class is CPUClass, parent_reset() is cpu_common_reset(). */
    xcc->parent_reset = cc->reset;
    /* overwrite the CPUClass->reset to arch reset: x86_cpu_reset(). */
    cc->reset = x86_cpu_reset;
    cc->has_work = x86_cpu_has_work;
    cc->do_interrupt = x86_cpu_do_interrupt;
    cc->cpu_exec_interrupt = x86_cpu_exec_interrupt;
    cc->set_pc = x86_cpu_set_pc;
    cc->synchronize_from_tb = x86_cpu_synchronize_from_tb;
    cc->get_arch_id = x86_cpu_get_arch_id;
    cc->get_paging_enabled = x86_cpu_get_paging_enabled;
    cc->asidx_from_attrs = x86_asidx_from_attrs;
    cc->get_memory_mapping = x86_cpu_get_memory_mapping;
    cc->get_phys_page_attrs_debug = x86_cpu_get_phys_page_attrs_debug;
    cc->debug_excp_handler = breakpoint_handler;
    cc->cpu_exec_enter = x86_cpu_exec_enter;
    cc->cpu_exec_exit = x86_cpu_exec_exit;
    cc->tcg_initialize = tcg_x86_init;
    cc->tlb_fill_cpu = x86_cpu_tlb_fill;
    cc->pointer_wrap = x86_pointer_wrap;     /* backport 7174cd2eec (U490) */
    cc->do_unaligned_access = x86_cpu_do_unaligned_access;
}

X86CPU *cpu_x86_init(struct uc_struct *uc)
{
    X86CPU *cpu;
    CPUState *cs;
    CPUClass *cc;
    X86CPUClass *xcc;

    // env->fpregs
    cpu = qemu_memalign(16, sizeof(*cpu));
    if (cpu == NULL) {
        return NULL;
    }
    memset((void*)cpu, 0, sizeof(*cpu));

    if (uc->cpu_model == INT_MAX) {
#ifdef TARGET_X86_64
        uc->cpu_model = UC_CPU_X86_HASWELL; // qemu64
#else
        uc->cpu_model = UC_CPU_X86_HASWELL; // qemu32
#endif
    } else if (uc->cpu_model != UC_CPU_X86_MAX &&
               uc->cpu_model >= ARRAY_SIZE(builtin_x86_defs)) {
        free(cpu);
        return NULL;
    }

    cs = (CPUState *)cpu;
    cc = (CPUClass *)&cpu->cc;
    cs->cc = cc;
    cs->uc = uc;
    uc->cpu = (CPUState *)cpu;
    cpu->env.cpuid_level_func7 = UINT32_MAX;
    cpu->env.cpuid_level = UINT32_MAX;
    cpu->env.cpuid_xlevel = UINT32_MAX;
    cpu->env.cpuid_xlevel2 = UINT32_MAX;

    /* init CPUClass */
    cpu_class_init(uc, cc);

    /* init X86CPUClass */
    x86_cpu_common_class_init(uc, cc, NULL);

    /* init X86CPUModel */
    /* Ignore X86CPUVersion, X86CPUVersionDefinition.
       we do not need so many cpu types and their property.
       version: more typename. x86_cpu_versioned_model_name().
       alias: more property. */
    xcc = &cpu->cc;
    xcc->model = calloc(1, sizeof(*(xcc->model)));
    if (xcc->model == NULL) {
        free(cpu);
        return NULL;
    }

    xcc->model->version = CPU_VERSION_AUTO;
    // UC_CPU_X86_MAX: Icelake-Server identity; all TCG features are added below (max_features)
    xcc->model->cpudef = &builtin_x86_defs[uc->cpu_model == UC_CPU_X86_MAX
                                               ? UC_CPU_X86_ICELAKE_SERVER
                                               : uc->cpu_model];

    if (xcc->model->cpudef == NULL) {
        free(xcc->model);
        free(cpu);
        return NULL;
    }

    /* init CPUState */
    cpu_common_initfn(uc, cs);

    /* init X86CPU */
    x86_cpu_initfn(uc, cs);

    if (uc->cpu_model == UC_CPU_X86_MAX) {
        /* like QEMU's "max" model: x86_cpu_expand_features() ORs in every
           feature the TCG translator supports */
        cpu->max_features = true;
        cpu->migratable = false;
#if __Use_Original_Qemu != 1 /* ours (U37) */
        /* QEMU's "full-cpuid-auto-level" property defaults to true but Unicorn never
           applies properties; without it CPUID.7.0:EAX (max sub-leaf) stays 0 and
           leaf 7.1 (CMPccXADD) is unreachable for software that checks it */
        cpu->full_cpuid_auto_level = true;
#endif /* __Use_Original_Qemu (U37) */
    }

    /* realize X86CPU */
    x86_cpu_realizefn(uc, cs);

    // init address space
    cpu_address_space_init(cs, 0, cs->memory);

    qemu_init_vcpu(cs);

    /* realize CPUState */

    return cpu;
}
