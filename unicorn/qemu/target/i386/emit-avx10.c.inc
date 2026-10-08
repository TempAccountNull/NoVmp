/*
 * Intel AVX10.2 instructions: code generation (NoVmp, ledgers U372-U399; included by
 * emit.c.inc). The EVEX engine gen_evex_insn (U146) does masking, {1toN}, masked memory and
 * the MXCSR / {er} / {sae} frame; these functions compute the full-length result.
 */
#include "avx10_helper.h"

