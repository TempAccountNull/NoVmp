/*
 * Intel AVX10.2 instruction helpers (NoVmp, ledgers U372-U399; AVX10.2 architecture
 * specification 361050-007 rev 7.0, chapters 5, 7, 8, 11 and 12).
 *
 * Every helper computes the full-length result of one instruction into its destination
 * pointer (or returns an opmask / EFLAGS value); merging/zeroing-masking, {1toN} broadcast,
 * masked memory and the MXCSR/#XM frame ({er}/{sae}) are done by the EVEX engine
 * (emit.c.inc gen_evex_insn, U146/U147). The destination may alias any source: every
 * element is computed from the same-index source elements before it is written.
 */
#include "qemu/osdep.h"
#if __Use_Original_Qemu != 1 /* ours (U372-U399): whole file */
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "avx10_helper.h"

#endif /* __Use_Original_Qemu (U372-U399) */
