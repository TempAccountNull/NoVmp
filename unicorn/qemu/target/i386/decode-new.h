/*
 * Decode table flags, mostly based on Intel SDM.
 *
 *  Copyright (c) 2022 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

typedef enum X86OpType {
    X86_TYPE_None,

    X86_TYPE_A, /* Implicit */
    X86_TYPE_B, /* VEX.vvvv selects a GPR */
    X86_TYPE_C, /* REG in the modrm byte selects a control register */
    X86_TYPE_D, /* REG in the modrm byte selects a debug register */
    X86_TYPE_E, /* ALU modrm operand */
    X86_TYPE_F, /* EFLAGS/RFLAGS */
    X86_TYPE_G, /* REG in the modrm byte selects a GPR */
    X86_TYPE_H, /* For AVX, VEX.vvvv selects an XMM/YMM register */
    X86_TYPE_I, /* Immediate */
    X86_TYPE_J, /* Relative offset for a jump */
    X86_TYPE_L, /* The upper 4 bits of the immediate select a 128-bit register */
    X86_TYPE_M, /* modrm byte selects a memory operand */
    X86_TYPE_N, /* R/M in the modrm byte selects an MMX register */
    X86_TYPE_O, /* Absolute address encoded in the instruction */
    X86_TYPE_P, /* reg in the modrm byte selects an MMX register */
    X86_TYPE_Q, /* MMX modrm operand */
    X86_TYPE_R, /* R/M in the modrm byte selects a register */
    X86_TYPE_S, /* reg selects a segment register */
    X86_TYPE_U, /* R/M in the modrm byte selects an XMM/YMM register */
    X86_TYPE_V, /* reg in the modrm byte selects an XMM/YMM register */
    X86_TYPE_W, /* XMM/YMM modrm operand */
    X86_TYPE_X, /* string source */
    X86_TYPE_Y, /* string destination */

    /* Custom */
    X86_TYPE_WM, /* modrm byte selects an XMM/YMM memory operand */
    X86_TYPE_2op, /* 2-operand RMW instruction */
    X86_TYPE_LoBits, /* encoded in bits 0-2 of the operand + REX.B */
    X86_TYPE_0, /* Hard-coded GPRs (RAX..RDI) */
    X86_TYPE_1,
    X86_TYPE_2,
    X86_TYPE_3,
    X86_TYPE_4,
    X86_TYPE_5,
    X86_TYPE_6,
    X86_TYPE_7,
    X86_TYPE_ES, /* Hard-coded segment registers */
    X86_TYPE_CS,
    X86_TYPE_SS,
    X86_TYPE_DS,
    X86_TYPE_FS,
    X86_TYPE_GS,
#if __Use_Original_Qemu != 1 /* ours (U129) */

    /* Opmask registers k0-k7 (VEX opmask instructions, exception types K20/K21) */
    X86_TYPE_K,  /* REG in the modrm byte selects an opmask register */
    X86_TYPE_KH, /* VEX.vvvv selects an opmask register */
    X86_TYPE_KU, /* R/M in the modrm byte selects an opmask register (mod = 11b) */
    X86_TYPE_KW, /* opmask register or memory operand (R/M) */
    X86_TYPE_KM, /* memory operand (R/M, mod != 11b) moved from/to an opmask register */
#endif /* __Use_Original_Qemu (U129) */
} X86OpType;

typedef enum X86OpSize {
    X86_SIZE_None,

    X86_SIZE_a,  /* BOUND operand */
    X86_SIZE_b,  /* byte */
    X86_SIZE_d,  /* 32-bit */
    X86_SIZE_dq, /* SSE/AVX 128-bit */
    X86_SIZE_p,  /* Far pointer */
    X86_SIZE_pd, /* SSE/AVX packed double precision */
    X86_SIZE_pi, /* MMX */
    X86_SIZE_ps, /* SSE/AVX packed single precision */
    X86_SIZE_q,  /* 64-bit */
    X86_SIZE_qq, /* AVX 256-bit */
    X86_SIZE_s,  /* Descriptor */
    X86_SIZE_sd, /* SSE/AVX scalar double precision */
    X86_SIZE_ss, /* SSE/AVX scalar single precision */
    X86_SIZE_si, /* 32-bit GPR */
    X86_SIZE_v,  /* 16/32/64-bit, based on operand size */
    X86_SIZE_w,  /* 16-bit */
    X86_SIZE_x,  /* 128/256-bit, based on operand size */
    X86_SIZE_y,  /* 32/64-bit, based on operand size */
    X86_SIZE_z,  /* 16-bit for 16-bit operand size, else 32-bit */

    /* Custom */
    X86_SIZE_d64,
    X86_SIZE_f64,
    X86_SIZE_xh, /* SSE/AVX packed half register */
#if __Use_Original_Qemu != 1 /* ours (U210) */
    /* EVEX Quarter Mem / Eighth Mem operands: VL/4 and VL/8 bits (SDM Vol2A Table 2-37) */
    X86_SIZE_xq, /* EVEX packed quarter register */
    X86_SIZE_xo, /* EVEX packed eighth register */
#endif /* __Use_Original_Qemu (U210) */
} X86OpSize;

typedef enum X86CPUIDFeature {
    X86_FEAT_None,
    X86_FEAT_3DNOW,
    X86_FEAT_ADX,
    X86_FEAT_AES,
    X86_FEAT_AVX,
    X86_FEAT_AVX2,
    X86_FEAT_BMI1,
    X86_FEAT_BMI2,
    X86_FEAT_CMPCCXADD,
    X86_FEAT_F16C,
    X86_FEAT_FMA,
    X86_FEAT_MOVBE,
    X86_FEAT_PCLMULQDQ,
    X86_FEAT_SHA_NI,
    X86_FEAT_SSE,
    X86_FEAT_SSE2,
    X86_FEAT_SSE3,
    X86_FEAT_SSSE3,
    X86_FEAT_SSE41,
    X86_FEAT_SSE42,
    X86_FEAT_SSE4A,
#if __Use_Original_Qemu != 1 /* ours (U70) */
    X86_FEAT_GFNI,      /* U70 */
#endif /* __Use_Original_Qemu (U70) */
#if __Use_Original_Qemu != 1 /* ours (U71) */
    X86_FEAT_AVX_VNNI,
#endif /* __Use_Original_Qemu (U71) */
#if __Use_Original_Qemu != 1 /* ours (U85) */
    X86_FEAT_AVX_VNNI_INT8,
#endif /* __Use_Original_Qemu (U85) */
#if __Use_Original_Qemu != 1 /* ours (U86) */
    X86_FEAT_AVX_VNNI_INT16,
#endif /* __Use_Original_Qemu (U86) */
#if __Use_Original_Qemu != 1 /* ours (U87) */
    X86_FEAT_AVX_IFMA,
#endif /* __Use_Original_Qemu (U87) */
#if __Use_Original_Qemu != 1 /* ours (U88) */
    X86_FEAT_AVX_NE_CONVERT,
#endif /* __Use_Original_Qemu (U88) */
#if __Use_Original_Qemu != 1 /* ours (U72) */
    X86_FEAT_MOVDIRI,
#endif /* __Use_Original_Qemu (U72) */
#if __Use_Original_Qemu != 1 /* ours (U73) */
    X86_FEAT_MOVDIR64B,
#endif /* __Use_Original_Qemu (U73) */
#if __Use_Original_Qemu != 1 /* ours (U82) */
    X86_FEAT_SHA512,
#endif /* __Use_Original_Qemu (U82) */
#if __Use_Original_Qemu != 1 /* ours (U83) */
    X86_FEAT_SM3,
#endif /* __Use_Original_Qemu (U83) */
#if __Use_Original_Qemu != 1 /* ours (U84) */
    X86_FEAT_SM4,
#endif /* __Use_Original_Qemu (U84) */
#if __Use_Original_Qemu != 1 /* ours (U100) */
    X86_FEAT_KL,            /* CPUID.(07H,0):ECX.KL (LOADIWKEY) */
    X86_FEAT_AESKLE,        /* + CPUID.19H:EBX.AESKLE */
    X86_FEAT_AESKLE_WIDE,   /* + CPUID.19H:EBX.AES_WIDE */
#endif /* __Use_Original_Qemu (U100) */
#if __Use_Original_Qemu != 1 /* ours (U101) */
    X86_FEAT_RAO_INT,
#endif /* __Use_Original_Qemu (U101) */
#if __Use_Original_Qemu != 1 /* ours (U102) */
    X86_FEAT_MOVRS,
#endif /* __Use_Original_Qemu (U102) */
#if __Use_Original_Qemu != 1 /* ours (U103) */
    X86_FEAT_USER_MSR,
#endif /* __Use_Original_Qemu (U103) */
#if __Use_Original_Qemu != 1 /* ours (U112) */
    X86_FEAT_ENQCMD,
#endif /* __Use_Original_Qemu (U112) */
#if __Use_Original_Qemu != 1 /* ours (U114) */
    X86_FEAT_CET_SS,
#endif /* __Use_Original_Qemu (U114) */
#if __Use_Original_Qemu != 1 /* ours (U129) */
    X86_FEAT_AVX512F,
    X86_FEAT_AVX512DQ,
    X86_FEAT_AVX512BW,
#endif /* __Use_Original_Qemu (U129) */
#if __Use_Original_Qemu != 1 /* ours (U175) */
    X86_FEAT_AMX_TILE,      /* CPUID.(07H,0):EDX[24] */
    X86_FEAT_AMX_INT8,      /* CPUID.(07H,0):EDX[25] */
    X86_FEAT_AMX_BF16,      /* CPUID.(07H,0):EDX[22] */
    X86_FEAT_AMX_FP16,      /* CPUID.(07H,1):EAX[21] */
    X86_FEAT_AMX_COMPLEX,   /* CPUID.(07H,1):EDX[8] */
#endif /* __Use_Original_Qemu (U175) */
#if __Use_Original_Qemu != 1 /* ours (U321) */
    X86_FEAT_AVX512CD,      /* CPUID.(07H,0):EBX[28] */
#endif /* __Use_Original_Qemu (U321) */
#if __Use_Original_Qemu != 1 /* ours (U322) */
    X86_FEAT_AVX512_IFMA,   /* CPUID.(07H,0):EBX[21] */
#endif /* __Use_Original_Qemu (U322) */
#if __Use_Original_Qemu != 1 /* ours (U323) */
    X86_FEAT_AVX512_VPOPCNTDQ, /* CPUID.(07H,0):ECX[14] */
#endif /* __Use_Original_Qemu (U323) */
#if __Use_Original_Qemu != 1 /* ours (U324) */
    X86_FEAT_AVX512_BITALG, /* CPUID.(07H,0):ECX[12] */
#endif /* __Use_Original_Qemu (U324) */
#if __Use_Original_Qemu != 1 /* ours (U325) */
    X86_FEAT_AVX512_VBMI,   /* CPUID.(07H,0):ECX[1] */
#endif /* __Use_Original_Qemu (U325) */
#if __Use_Original_Qemu != 1 /* ours (U330) */
    X86_FEAT_AVX512_FP16,   /* CPUID.(07H,0):EDX[23] */
#endif /* __Use_Original_Qemu (U330) */
#if __Use_Original_Qemu != 1 /* ours (U372) */
    X86_FEAT_AVX10_2,       /* CPUID.(07H,1):EDX.AVX10[19] and CPUID.(24H,0):EBX[7:0] >= 2 */
#endif /* __Use_Original_Qemu (U372) */
} X86CPUIDFeature;

/* Execution flags */

typedef enum X86OpUnit {
    X86_OP_SKIP,    /* not valid or managed by emission function */
    X86_OP_SEG,     /* segment selector */
    X86_OP_CR,      /* control register */
    X86_OP_DR,      /* debug register */
    X86_OP_INT,     /* loaded into/stored from s->T0/T1 */
    X86_OP_IMM,     /* immediate */
    X86_OP_SSE,     /* address in either s->ptrX or s->A0 depending on has_ea */
    X86_OP_MMX,     /* address in either s->ptrX or s->A0 depending on has_ea */
#if __Use_Original_Qemu != 1 /* ours (U129) */
    X86_OP_KREG,    /* opmask register env->opmask_regs[n], or memory at s->A0 if has_ea */
#endif /* __Use_Original_Qemu (U129) */
} X86OpUnit;

/*
 * Subset of upstream X86InsnCheck (QEMU >= 8.2).  Other upstream bits (i64,
 * o64, prot, VEX128, ...) are expressed with X86_SPECIAL_* / decode_op_size in
 * this tree; values kept identical to upstream to ease later backports.
 */
typedef enum X86InsnCheck {
    /* Fault if VEX.W=1 */
    X86_CHECK_W0 = 128,

    /* Fault if VEX.W=0 */
    X86_CHECK_W1 = 256,
#if __Use_Original_Qemu != 1 /* ours (U70) */
    /* As W0/W1, but only for the VEX encoding (legacy forms ignore REX.W) (U70) */
    X86_CHECK_VEX_W0 = 512,
    X86_CHECK_VEX_W1 = 1024,
#endif /* __Use_Original_Qemu (U70) */
#if __Use_Original_Qemu != 1 /* ours (U79) */
    /* Fault if VEX.L=1 (upstream QEMU 8.2 value) (U79) */
    X86_CHECK_VEX128 = 64,
#endif /* __Use_Original_Qemu (U79) */
#if __Use_Original_Qemu != 1 /* ours (U129) */
    /*
     * Fault if VEX.L=0 (the VEX.L1 opmask forms) (U129). 8192 is clear of the
     * upstream QEMU 11.1 values (up to X86_CHECK_o64_intel = 4096) and U70's 512/1024.
     */
    X86_CHECK_VEX256 = 8192,
#endif /* __Use_Original_Qemu (U129) */
} X86InsnCheck;

typedef enum X86InsnSpecial {
    X86_SPECIAL_None,

    /* Always locked if it has a memory operand (XCHG) */
    X86_SPECIAL_Locked,

    /* Fault outside protected mode */
    X86_SPECIAL_ProtMode,

    /*
     * Register operand 0/2 is zero extended to 32 bits.  Rd/Mb or Rd/Mw
     * in the manual.
     */
    X86_SPECIAL_ZExtOp0,
    X86_SPECIAL_ZExtOp2,

    /*
     * Register operand 2 is extended to full width, while a memory operand
     * is doubled in size if VEX.L=1.
     */
    X86_SPECIAL_AVXExtMov,

    /*
     * MMX instruction exists with no prefix; if there is no prefix, V/H/W/U operands
     * become P/P/Q/N, and size "x" becomes "q".
     */
    X86_SPECIAL_MMX,

    /* Illegal or exclusive to 64-bit mode */
    X86_SPECIAL_i64,
    X86_SPECIAL_o64,
} X86InsnSpecial;

/*
 * Special cases for instructions that operate on XMM/YMM registers.  Intel
 * retconned all of them to have VEX exception classes other than 0 and 13, so
 * all these only matter for instructions that have a VEX exception class.
 * Based on tables in the "AVX and SSE Instruction Exception Specification"
 * section of the manual.
 */
typedef enum X86VEXSpecial {
    X86_VEX_None,

    /* Legacy SSE instructions that allow unaligned operands */
    X86_VEX_SSEUnaligned,

    /*
     * Used for instructions that distinguish the XMM operand type with an
     * instruction prefix; legacy SSE encodings will allow unaligned operands
     * for scalar operands only (identified by a REP prefix).  In this case,
     * the decoding table uses "x" for the vector operands instead of specifying
     * pd/ps/sd/ss individually.
     */
    X86_VEX_REPScalar,

    /*
     * VEX instructions that only support 256-bit operands with AVX2 (Table 2-17
     * column 3).  Columns 2 and 4 (instructions limited to 256- and 127-bit
     * operands respectively) are implicit in the presence of dq and qq
     * operands, and thus handled by decode_op_size.
     */
    X86_VEX_AVX2_256,

    /*
     * Unicorn backport: legacy-SSE-only instruction (no VEX form, e.g. SHA-NI).
     * Lets the entry use an SSE exception class (OSFXSR/EM/TS checks and
     * Type 4 16-byte alignment) while still faulting on a VEX prefix.
     */
    X86_VEX_NoVEX,
} X86VEXSpecial;


#if __Use_Original_Qemu != 1 /* ours (U141) */
/*
 * NoVmp (ledger U141): attributes of an EVEX form (X86OpEntry.evex_*), SDM Vol2A 2.7 and
 * Tables 2-36..2-43. Generated data: Emulator/data/evex_forms.tsv (gen_evex_tables.py).
 */
typedef enum X86EvexTuple {
    X86_EVEX_TT_NONE,        /* register-only form */
    X86_EVEX_TT_FULL,        /* Full: N = VL, the element size with EVEX.b (Table 2-36) */
    X86_EVEX_TT_HALF,        /* Half: N = VL/2, the element size with EVEX.b */
    X86_EVEX_TT_FULL_MEM,    /* Full Mem: N = VL (Table 2-37) */
    X86_EVEX_TT_T1S,         /* Tuple1 Scalar: N = memory operand size */
    X86_EVEX_TT_T1F,         /* Tuple1 Fixed: N = memory operand size */
    X86_EVEX_TT_T2,          /* Tuple2: N = 2 elements */
    X86_EVEX_TT_T4,          /* Tuple4: N = 4 elements */
    X86_EVEX_TT_T8,          /* Tuple8: N = 8 elements */
    X86_EVEX_TT_HALF_MEM,    /* N = VL/2 */
    X86_EVEX_TT_QUARTER_MEM, /* N = VL/4 */
    X86_EVEX_TT_EIGHTH_MEM,  /* N = VL/8 */
    X86_EVEX_TT_MEM128,      /* N = 16 */
    X86_EVEX_TT_MOVDDUP,     /* N = 8 (VL 128), VL otherwise */
#if __Use_Original_Qemu != 1 /* ours (U332) */
    /*
     * AVX512-FP16 "Quarter" tuple (SDM Vol2A Table 2-34, VCVTPH2PD/QQ/UQQ): N = VL/4, the
     * element size with EVEX.b ({1toN} allowed, unlike Quarter Mem of U210)
     */
    X86_EVEX_TT_QUARTER,
#endif /* __Use_Original_Qemu (U332) */
} X86EvexTuple;

typedef enum X86EvexMask {
    X86_EVEX_MASK_MZ,        /* {k1}{z}; z with aaa = 0 or a memory destination #UD */
    X86_EVEX_MASK_M,         /* {k1} merging only, z #UD */
    X86_EVEX_MASK_KDEST,     /* k1 {k2}: opmask destination ANDed with k2, z #UD */
    X86_EVEX_MASK_KREQ,      /* aaa != 0 required, z #UD (gather/scatter) */
    X86_EVEX_MASK_NONE,      /* aaa and z must be 0 */
} X86EvexMask;

typedef enum X86EvexRC {
    X86_EVEX_RC_NONE,        /* EVEX.b on a register-register form #UD (Table 2-43) */
    X86_EVEX_RC_ER,          /* {er}: static rounding control in L'L, SAE implied, VL 512 */
    X86_EVEX_RC_SAE,         /* {sae}: suppress all exceptions, L'L ignored, VL 512 */
} X86EvexRC;

/* X86OpEntry.evex_w: EVEX.W requirement of the form */
#define X86_EVEX_WSEL 0      /* both; W0/W1 select the element size (X86_EVEX_ES_W) */
#define X86_EVEX_W0   1      /* W1 #UD */
#define X86_EVEX_W1   2      /* W0 #UD */
#define X86_EVEX_WIG  3      /* ignored */

/* X86OpEntry.evex_vl: vector lengths of the form (EVEX.L'L), 0 = no EVEX form */
#define X86_EVEX_VL128 1
#define X86_EVEX_VL256 2
#define X86_EVEX_VL512 4
#define X86_EVEX_LIG   8     /* L'L ignored (scalar) */

/* X86OpEntry.evex_es: element size for masking, broadcast and disp8*N */
#define X86_EVEX_ES_W  0     /* EVEX.W0: 32 bits, EVEX.W1: 64 bits */
#define X86_EVEX_ES_8  1
#define X86_EVEX_ES_16 2
#define X86_EVEX_ES_32 3
#define X86_EVEX_ES_64 4
#endif /* __Use_Original_Qemu (U141) */
#if __Use_Original_Qemu != 1 /* ours (U213) */
/* X86OpEntry.evex_cx: the source (expand) or destination (compress) is contiguous */
#define X86_EVEX_CX_EXPAND   1
#define X86_EVEX_CX_COMPRESS 2
#endif /* __Use_Original_Qemu (U213) */

typedef struct X86OpEntry  X86OpEntry;
typedef struct X86DecodedInsn X86DecodedInsn;

/* Decode function for multibyte opcodes.  */
typedef void (*X86DecodeFunc)(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b);

/* Code generation function.  */
typedef void (*X86GenFunc)(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode);

struct X86OpEntry {
    /* Based on the is_decode flags.  */
    union {
        X86GenFunc gen;
        X86DecodeFunc decode;
    };
    /* op0 is always written, op1 and op2 are always read.  */
    X86OpType    op0:8;
    X86OpSize    s0:8;
    X86OpType    op1:8;
    X86OpSize    s1:8;
    X86OpType    op2:8;
    X86OpSize    s2:8;
    /* Must be I and b respectively if present.  */
    X86OpType    op3:8;
    X86OpSize    s3:8;

    X86InsnSpecial special:8;
    X86CPUIDFeature cpuid:8;
    unsigned     vex_class:8;
    X86VEXSpecial vex_special:8;
    unsigned     check:16;
    uint16_t     valid_prefix:16;
    bool         is_decode:1;
#if __Use_Original_Qemu != 1 /* ours (U141) */
    /* EVEX forms only (tables opcodes_evex_*), see X86EvexTuple above */
    unsigned     evex_vl:4;     /* X86_EVEX_VL128/256/512 | X86_EVEX_LIG; 0 = none */
    unsigned     evex_tt:4;     /* X86EvexTuple (disp8*N, broadcast) */
    unsigned     evex_mask:3;   /* X86EvexMask (Table 2-42) */
    unsigned     evex_rc:2;     /* X86EvexRC (Table 2-43) */
    unsigned     evex_w:2;      /* X86_EVEX_WSEL/W0/W1/WIG */
    unsigned     evex_es:3;     /* X86_EVEX_ES_* */
    unsigned     evex_align:1;  /* exception class E1: memory operand VL-aligned, #GP(0) */
    unsigned     evex_fp:1;     /* SIMD floating point: MXCSR flags, #XM (E2/E3) */
    unsigned     evex_w32:1;    /* EVEX.W ignored outside 64-bit mode (behaves as W0) */
    unsigned     evex_nofs:1;   /* no memory fault suppression (E*NF classes) */
#if __Use_Original_Qemu != 1 /* ours (U190) */
    /*
     * the destination register is also a source (FMA, VPERMI2x/VPERMT2x, ...): gen_evex_insn
     * copies it to the result scratch register (op[0].offset) before the gen function runs,
     * with +1.0 in the masked-off lanes of an FP form; merging-masking keeps the old value
     */
    unsigned     evex_dsrc:1;
#endif /* __Use_Original_Qemu (U190) */
#if __Use_Original_Qemu != 1 /* ours (U191) */
    /* merging-masking takes SRC1 (EVEX.vvvv) for masked-off lanes (VPBLENDMx, VBLENDMPx) */
    unsigned     evex_msrc1:1;
#endif /* __Use_Original_Qemu (U191) */
#endif /* __Use_Original_Qemu (U141) */
#if __Use_Original_Qemu != 1 /* ours (U213) */
    /* X86_EVEX_CX_*: VPEXPAND/VEXPAND, VPCOMPRESS/VCOMPRESS (gen_evex_cx) */
    unsigned     evex_cx:2;
#endif /* __Use_Original_Qemu (U213) */
#if __Use_Original_Qemu != 1 /* ours (U230) */
    /*
     * NoVmp (ledger U230): element size of the destination (= of the opmask bits) when it
     * differs from the source element size evex_es (conversions such as VCVTPD2PS,
     * VCVTPS2PD, VCVTPH2PS, VCVTPS2PH; U260's evex_kes merged in: VPACKSSDW/VPACKUSDW,
     * m32bcst {1toN} and disp8*N by dwords, word writemask): X86_EVEX_ES_8..64, 0 = same as
     * evex_es; DisasContext.evex_dsz. A destination narrower than the vector length
     * (VPMOVWB, VCVTPD2PS: VL/2 bytes) is taken from the size of operand 0 (U210
     * evex_dest_bytes; U260's evex_narrow field merged into it).
     */
    unsigned     evex_ds:3;
#endif /* __Use_Original_Qemu (U230) */
};
typedef struct X86DecodedOp {
    int8_t n;
    MemOp ot;     /* For b/c/d/p/s/q/v/w/y/z */
    X86OpUnit unit;
    bool has_ea;
    int offset;   /* For MMX and SSE */

    /*
     * This field is used internally by macros OP0_PTR/OP1_PTR/OP2_PTR,
     * do not access directly!
     */
    TCGv_ptr v_ptr;
} X86DecodedOp;

struct X86DecodedInsn {
    X86OpEntry e;
    X86DecodedOp op[3];
    target_ulong immediate;
    AddressParts mem;

    uint8_t b;
};
