/*
 * x87 transcendental engine (NoVmp, ledger U53, plan 1.12), see x87_trans.c.
 */
#ifndef X87_TRANS_H
#define X87_TRANS_H

#include "fpu/softfloat.h"

/* result flags */
#define X87T_IE   0x01
#define X87T_DE   0x02
#define X87T_ZE   0x04
#define X87T_OE   0x08
#define X87T_UE   0x10
#define X87T_PE   0x20
#define X87T_TINY 0x100     /* internal: tiny result (after rounding), masked UE path */

/*
 * engine value: (v + tail * eps) * 2^scale (tail = sign of the part below
 * float128 precision; scale keeps results beyond float128's exponent range
 * exact). bias: a relative reduction of the magnitude by bias * 2^-73 (unused,
 * 0); trunc: keep only this many bits (unused, 0).
 */
typedef struct X87TVal {
    float128 v;
    int tail;
    int32_t scale;
    int32_t bias;
    int32_t trunc;          /* nonzero: the internal value is truncated to this many bits */
} X87TVal;

typedef struct X87TOut {
    unsigned flags;
    bool c1;                /* rounding direction of the (first) result */
    bool c1_second;         /* ... of the second result (FPTAN/FSINCOS)  */
} X87TOut;

/* round a high-precision value to the x87 result, see x87_trans.c */
floatx80 x87t_round(X87TVal v, int pbits, char pmode, int rc, bool ue_masked, bool oe_masked,
                    X87TOut *o, bool second);
floatx80 x87t_round_prec(X87TVal v, int pbits, char pmode, int prec, int rc, bool ue_masked,
                         bool oe_masked, X87TOut *o, bool second);

/* exact x87 arithmetic for finite nonzero operands (FADD..FDIV, FSQRT, FSCALE) */
#define X87T_OP_ADD   0
#define X87T_OP_SUB   1
#define X87T_OP_MUL   2
#define X87T_OP_DIV   3
#define X87T_OP_SQRT  4
#define X87T_OP_SCALE 5
X87TVal x87t_arith(int op, floatx80 a, floatx80 b);

/* exact model values (U56), finite in-domain operands */
X87TVal x87t_f2xm1(floatx80 x);
X87TVal x87t_fyl2x(floatx80 x, floatx80 y);
X87TVal x87t_fyl2xp1(floatx80 x, floatx80 y);
X87TVal x87t_fpatan(floatx80 y, floatx80 x);
/* sincos: the FSINCOS kernel (Horner); false: FSIN / FCOS (Estrin) */
void x87t_trig(floatx80 x, bool sincos, X87TVal *sn, X87TVal *cs);
X87TVal x87t_tan(floatx80 x);
X87TVal x87t_quot(floatx80 y, floatx80 x);

#endif
