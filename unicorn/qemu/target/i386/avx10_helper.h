/*
 * Intel AVX10.2 helpers (NoVmp, ledgers U372-U399): descriptor layout shared by the
 * translator (emit-avx10.c.inc) and avx10_helper.c.
 */
#ifndef NOVMP_AVX10_HELPER_H
#define NOVMP_AVX10_HELPER_H
#if __Use_Original_Qemu != 1 /* ours (U372-U399) */

/* desc: bits 7:0 operation, 15:8 imm8, 23:16 element count, 31:24 operation flags */
#define AVX10_DESC(op, imm, n, fl) ((uint32_t)(op) | ((uint32_t)((imm) & 0xff) << 8) | \
                                    ((uint32_t)(n) << 16) | ((uint32_t)(fl) << 24))
#define AVX10_DESC_OP(d)   ((d) & 0xff)
#define AVX10_DESC_IMM(d)  (((d) >> 8) & 0xff)
#define AVX10_DESC_N(d)    (((d) >> 16) & 0xff)
#define AVX10_DESC_FL(d)   (((d) >> 24) & 0xff)

#endif /* __Use_Original_Qemu (U372-U399) */
#endif /* NOVMP_AVX10_HELPER_H */
