/* Copyright (c) 2015, Intel Corporation Redistribution and use in source and 
   binary forms, with or without modification, are permitted provided that the 
   following conditions are met:
 
  * Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer. 
  * Redistributions in binary form must reproduce the above copyright notice, 
    this list of conditions and the following disclaimer in the documentation 
    and/or other materials provided with the distribution. 
  * Neither the name of Intel Corporation nor the names of its contributors may 
    be used to endorse or promote products derived from this software without 
    specific prior written permission. 

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */ 

/*
 * NoVmp provenance (ledger U1245, user decision 2026-10-11): taken from Intel's reference implementation
 * "Reference Implementations for Intel(R) Architecture Approximation Instructions VRCP14, VRSQRT14,
 * VRCP28, VRSQRT28, and VEXP2", Intel article ID 671685 (2015-12-28, M. Cornea), file recip14.c,
 * SHA-256 4cb551b434f65928e97b062b1a4718809c955da6ab8d650f19593c72679b21e9.
 * Routines taken: RCP14S, RCP14D, RSQRT14S, RSQRT14D, the tables RCP14_Coeff / RSQRT14_Coeff and the
 * defines they use (lines 25-104, 150-297, 300-416, 465-746 of that file; the usage-example comments
 * are left out). NoVmp changes, each marked "NoVmp U1245": static linkage, const tables, and in RCP14D
 * the zero of an exact power of two 2^1023 under FTZ keeps the operand's sign (SDM 092 Vol2C VRCP14PD /
 * VRCP14SD: underflow results are flushed to zero "with the sign of the operand"; the reference masks
 * with the 32-bit FP32_SIGN_BIT and returns +0 for -2^1023). The caller (fpu_helper.c, U1245) runs these
 * routines with the host MXCSR set to 1F80H (round to nearest, DAZ/FTZ off, exceptions masked), as the
 * article requires, and passes the guest MXCSR as the routines' mxcsr argument (bits 6 DAZ, 15 FTZ).
 * Built only when __Use_Original_Qemu != 1 (it is #included from an "ours" block of fpu_helper.c).
 */

/*
    ****************************************************************************
    ****************************************************************************
    ** THIS FILE CONTAINS EMULATION ROUTINES FOR THE UNDERLYING ALGORITHMS OF:**
    **   VRCP14PD Compute Approximate Reciprocals of Packed Float64 Values    **
    **       with relative error of less than 2^(-14)                         **
    **   VRCP14SD Compute Approximate Reciprocal of Scalar Float64 Value      **
    **       with relative error of less than 2^(-14)                         **
    **   VRCP14PS Compute Approximate Reciprocals of Packed Float32 Values    **
    **       with relative error of less than 2^(-14)                         **
    **   VRCP14SS Compute Approximate Reciprocal of Scalar Float32 Value      **
    **       with relative error of less than 2^(-14)                         **
    **   VRSQRT14PD Compute Approximate Reciprocals of Square Roots           **
    **       of Packed Float64 Values with relative error of less than 2^(-14)**
    **   VRSQRT14SD Compute Approximate Reciprocal of Square Root             **
    **       of Scalar Float64 Value with relative error of less than 2^(-14) **
    **   VRSQRT14PS Compute Approximate Reciprocals of Square Roots           **
    **       of Packed Float32 Values with relative error of less than 2^(-14)**
    **   VRSQRT14SS Compute Approximate Reciprocal of Square Root             **
    **       of Scalar Float32 Value with relative error of less than 2^(-14) **
    **                                                                        **
    ** THE CORRESPONDING EMULATION ROUTINES (ONLY SCALAR VERSIONS) ARE:       **
    **   RCP14S - 14-BIT RECIPROCAL APPROXIMATION FOR Float32                 **
    **   RCP14D - 14-BIT RECIPROCAL APPROXIMATION FOR Float64                 **
    **   RSQRT14S - 14-BIT RECIPROCAL SQUARE ROOT APPROXIMATION FOR Float32   **
    **   RSQRT14D - 14-BIT RECIPROCAL SQUARE ROOT APPROXIMATION FOR Float64   **
    ****************************************************************************
    ****************************************************************************
*/

#include <math.h>

typedef union {
    unsigned int u;
    float f;
} type32;

typedef union {
    unsigned long long u;
    double f;
} type64;

#define FP32_SIGN_BIT                     0x80000000
#define FP32_EXP_MASK                     0x7f800000
#define FP32_SIGNIF_MASK                  0x007fffff
#define FP32_HIDDEN_BIT                   0x00800000
#define FP32_QNAN_BIT                     0x00400000
#define FP32_DEFAULT_NAN_AS_UINT32        0xffc00000
#define FP32_PLUS_ZERO_AS_UINT32          0x00000000
#define FP32_PLUS_ONE_AS_UINT32           0x3f800000
#define FP32_PLUS_TWO_AS_UINT32           0x40000000
#define FP32_SMALL_DEN_THRESHOLD          0x00200000
#define FP32_SIGNIF_SHIFT_THRESHOLD       0x00800001
#define FP32_LOW_31_BITS                  0x7fffffff
#define FP32_SIGN_AND_TOP_OF_SIGNIF       0x80700000
#define FP32_SIGN_AND_SIGNIF              0x807fffff
#define FP32_CLEAR_LOW_8_BITS             0xffffff00
#define FP32_CLEAR_LOW_7_BITS             0xffffff80
#define FP32_BIT_15                       0x00008000
#define FP32_BIT_6                        0x00000040

#define FP64_SIGN_BIT                     0x8000000000000000ull
#define FP64_EXP_MASK                     0x7ff0000000000000ull
#define FP64_SIGNIF_MASK                  0x000fffffffffffffull
#define FP64_HIDDEN_BIT                   0x0010000000000000ull
#define FP64_QNAN_BIT                     0x0008000000000000ull
#define FP64_DEFAULT_NAN_AS_UINT64        0xfff8000000000000ull
#define FP64_PLUS_ZERO_AS_UINT64          0x0000000000000000ull
#define FP64_PLUS_ONE_AS_UINT64           0x3ff0000000000000ull
#define FP64_PLUS_TWO_AS_UINT64           0x4000000000000000ull
#define FP64_SMALL_DEN_THRESHOLD          0x0004000000000000ull
#define FP64_SIGNIF_SHIFT_THRESHOLD       0x0010000000000001ull
#define FP64_EXPON_DELTA_18               0x0120000000000000ull
#define FP64_EXPON_DELTA_19               0x0130000000000000ull
#define FP64_LOW_63_BITS                  0x7fffffffffffffffull
#define FP64_SIGN_AND_TOP_OF_SIGNIF       0x800e000000000000ull
#define FP64_SIGN_AND_SIGNIF              0x800fffffffffffffull
#define FP64_CLEAR_LOW_37_BITS            0xffffffe000000000ull
#define FP64_CLEAR_LOW_36_BITS            0xfffffff000000000ull

// Note on the RCP14 algorithm, which calculates
//   rcp14(x)=reciprocal approximation of x:
// if (x is in (1.0, 2.0))
//   i = int_floor((x-1)*64)
//   y[i] = 1 + 1/128 + i/64
//   rcp14(x) = truncate_to_17_ms_bits(a[i]-b[i]*(x-y[i]))

// a[i] = RCP14_Coeff[2*i+1] 18 bits; b[i] = RCP14_Coeff[2*i] 10 bits (at most)
static const unsigned int RCP14_Coeff[128] = { /* NoVmp U1245: static const */
  1009, 260119, 977, 256148, 949, 252296, 921, 248558,
  893,  244929, 869, 241405, 843, 237981, 821, 234652,
  797,  231416, 777, 228266, 755, 225202, 735, 222220,
  717,  219314, 699, 216485, 681, 213727, 663, 211038,
  647,  208417, 631, 205859, 617, 203364, 601, 200929,
  587,  198551, 573, 196229, 561, 193960, 547, 191743,
  535,  189576, 523, 187458, 513, 185387, 501, 183360,
  491,  181377, 479, 179439, 469, 177540, 459, 175681,
  451,  173860, 441, 172077, 433, 170330, 423, 168618,
  415,  166940, 407, 165295, 399, 163682, 391, 162101,
  385,  160550, 377, 159027, 369, 157535, 363, 156069,
  357,  154631, 349, 153219, 343, 151832, 337, 150470,
  331,  149133, 325, 147819, 319, 146528, 315, 145260,
  309,  144012, 303, 142787, 299, 141582, 293, 140397,
  289,  139232, 285, 138085, 279, 136959, 275, 135853,
  271,  134763, 267, 133689, 263, 132631, 259, 131589
};

static void /* NoVmp U1245: static */
RCP14S (unsigned int mxcsr, type32 *dst, type32 arg) {
  unsigned int i, a, b; // i, a[i], b[i] - used when 1 < x < 2
  float x, y, xmy, rcp14; // x (input), y, x - y, rcp14 (result)
  double da, db, dxmy, drcp14, dpr; // double versions, for exact computations
  unsigned long long ui64; // used for bit manipulation of double values
  unsigned int ui32;// used for bit manipulation of float values
  unsigned int bexp; // biased exponent
  unsigned int sign; // sign bit
  unsigned int signif; // significand
  type32 dst1, arg1; // destination and argument for recuresive calls
  unsigned int ftz = mxcsr & FP32_BIT_15; // bit 15 of MXCSR
  unsigned int daz = mxcsr & FP32_BIT_6; // bit 6 of MXCSR
  // the following uses the IEEE 754-2008 encoding for 32-bit and 64-bit binary 
  //     floating-point values
  x = fabs (arg.f); // absolute value of the argument
  if (isnan (x)) { // if the argument is NaN
    dst->u = arg.u | FP32_QNAN_BIT; // rcp14 = quietized input NaN
  } else if (isinf (x)) { // if the argument is infinity
    dst->u = arg.u & FP32_SIGN_BIT; // rcp14 = zero of the same sign
  } else if ((arg.u & FP32_LOW_31_BITS) == 0x0) { // if the argument is zero
    dst->u = (arg.u & FP32_SIGN_BIT) | FP32_EXP_MASK; 
        // rcp14 = infinity of same sign
  } else if (!isnormal (x) && daz) {
      dst->u = (arg.u & FP32_SIGN_BIT) | FP32_EXP_MASK; 
          // rcp14 = infinity of same sign
  } else if (!isnormal (x) && (arg.u & FP32_LOW_31_BITS) <= FP32_SMALL_DEN_THRESHOLD) { 
        // if the argument is a 'small' denormal
    dst->u = (arg.u & FP32_SIGN_BIT) | FP32_EXP_MASK; 
        // rcp14 = infinity of same sign
  } else if (!isnormal (x)) { // 'large' denormal and at least 0x00200001 in 
        // magnitude; rcp14 does not overflow
    bexp = 0xfc; // start with 125 as the biased exponent for the reciprocal 
        // (one below 126)
    sign = arg.u & FP32_SIGN_BIT;
    arg1.u = arg.u & FP32_SIGNIF_MASK; // remove sign and exponent, to prepare 
        // for normalization
    while ((arg1.u & FP32_HIDDEN_BIT) == 0x0) {
      arg1.u = arg1.u << 1; 
      bexp++;
    }
    arg1.u = (arg1.u & FP32_SIGNIF_MASK) | FP32_PLUS_ONE_AS_UINT32; 
        // bring argument between 
        // 1.0 and 2.0
    RCP14S (mxcsr, &dst1, arg1);
    if (dst1.u == FP32_PLUS_ONE_AS_UINT32) bexp++; 
        // to account for the fact that the reciprocal is not < 1.0 in this case
    dst->u = sign | (bexp << 23) | (dst1.u & FP32_SIGNIF_MASK); // rcp14
  } else if ((arg.u & FP32_SIGNIF_MASK) == 0x0) { 
        // if it is an exact power of 2
    bexp = (arg.u & FP32_EXP_MASK) >> 23;
    if (bexp == 0xfe) { // denormal reciprocal
      dst->u = (arg.u & FP32_SIGN_BIT) | FP32_QNAN_BIT; // rcp14
      if (ftz) dst->u = dst->u & FP32_SIGN_BIT; // zero of the same sign
    } else { // rcp14 is not a denormal
      bexp = (0x7f - (bexp - 0x7f)) << 23;
      dst->u = (arg.u & FP32_SIGN_AND_TOP_OF_SIGNIF) | bexp; // rcp14
    }
  } else if ((arg.u & FP32_EXP_MASK) == FP32_PLUS_ONE_AS_UINT32) { 
        // if 1 < |x| < 2)
    sign = arg.u & FP32_SIGN_BIT;
    // a[i] = RCP14_Coeff[2*i+1]; b[i] = RCP14_Coeff[2*i]
    i = floor ((x - 1.0) * 64); // exact
    y = 1.0 + 1.0/128 + i/64.0; // 8 bits (exact)
    ui32 = *((unsigned int *)&x); // x viewed as an unsigned int
    ui32 = ui32 & FP32_CLEAR_LOW_7_BITS; // remove the 7 least significant bits
    x = *((float *)&ui32); // x with significand truncated to 17 bits
    xmy = x - y; // fits in 23 bits; exact
    a = RCP14_Coeff[2*i+1]; // b[i], up to 18 bits
    b = RCP14_Coeff[2*i]; // a[i], up to 10 bits
    da = (double)a; // double precision versions of a[i] and b[i], for exact 
        // computation
    db = 256 * (double)b; // multiply by 2^8 to make up for the difference 
        // between 18 bits in a and 10 bits in b
    dxmy = (double)xmy; // x - y as a double
    dpr = (db * dxmy); // product b[i] * (x - y), exact in double precision
    drcp14 = da - dpr; // rcp14 = a[i] - b[i] * (x - y), exact in 
        // double precision
    ui64 = *((unsigned long long *)&drcp14); // rcp14, viewed as a 
        // 64-bit integer 
    ui64 = ui64 & FP64_CLEAR_LOW_36_BITS; // truncate to 28 bits = 
        // 1-bit sign + 11-bit exp + 16-bit fraction
    ui64 = ui64 - FP64_EXPON_DELTA_18; // divide by 2^18
    drcp14 = *((double *)&ui64); // rcp14 in double precision format
    rcp14 = (float)drcp14; // rcp14 is single precision; exact conversion
    dst->f = rcp14;
    dst->u = sign | dst->u; // rcp14
  } else { // normal not between 1.0 and 2.0 in absolute value, 
        // and not a power of 2
    sign = arg.u & FP32_SIGN_BIT;
    bexp = (arg.u & FP32_EXP_MASK) >> 23;
    signif = FP32_HIDDEN_BIT | (arg.u & FP32_SIGNIF_MASK);
    if (bexp == 0xfe) { // denormal reciprocal, and the significand 
        // must be shifted by 2
      arg1.u = (arg.u & FP32_SIGN_AND_SIGNIF) | FP32_PLUS_ONE_AS_UINT32; 
        // bring argument between 1.0 and 2.0
      RCP14S (mxcsr, &dst1, arg1);
      signif = FP32_HIDDEN_BIT | (dst1.u & FP32_SIGNIF_MASK);
      dst->u = (dst1.u & FP32_SIGN_BIT) | (signif >> 2); // rcp14; the new 
        // biased exponent is 0x0
    } else if (bexp == 0xfd && signif >= FP32_SIGNIF_SHIFT_THRESHOLD) { 
        // denormal reciprocal, 
        // and the significand must be shifted by 1
      arg1.u = (arg.u & FP32_SIGN_AND_SIGNIF) | FP32_PLUS_ONE_AS_UINT32; 
        // bring argument between 1.0 and 2.0
      RCP14S (mxcsr, &dst1, arg1);
      signif = FP32_HIDDEN_BIT | (dst1.u & FP32_SIGNIF_MASK);
      dst->u = (dst1.u & FP32_SIGN_BIT) | (signif >> 1); // rcp14; the new 
        // biased exponent is 0x0
    } else {
      bexp = 0xfe - bexp;
      arg1.u = (arg.u & FP32_SIGN_AND_SIGNIF) | FP32_PLUS_ONE_AS_UINT32; 
          // bring argument between 1.0 and 2.0
      RCP14S (mxcsr, &dst1, arg1);
      dst->u = (dst1.u & FP32_SIGN_AND_SIGNIF) | ((bexp - 1) << 23); // rcp14
    }
    if (!isnormal (dst->f) && ftz) {
      dst->u = dst->u & FP32_SIGN_BIT; // zero of the same sign
    }
  }
}

static void /* NoVmp U1245: static */
RCP14D (unsigned int mxcsr, type64 *dst, type64 arg) {
  unsigned int i, a, b; // i, a[i], b[i] - used when 1 < x < 2
  double x, y, xmy, rcp14; // x (input), y, x - y, rcp14 (result)
  double da, db, dpr; // double versions for exact computations
  unsigned long long ui64; // used for bit manipulation of double values
  unsigned long long int bexp; // biased exponent
  unsigned long long int sign; // sign bit
  unsigned int long long signif; // significand
  type64 dst1, arg1; // destination and argument for recuresive calls
  unsigned int ftz = mxcsr & FP32_BIT_15; // bit 15 of MXCSR
  unsigned int daz = mxcsr & FP32_BIT_6; // bit 6 of MXCSR
  // the following uses the IEEE 754-2008 encoding for 32-bit and 64-bit 
  // binary floating-point values
  x = fabs (arg.f); // absolute value of the argument
  if (isnan (x)) { // if the argument is NaN
    dst->u = arg.u | FP64_QNAN_BIT; // rcp14 = quietized input NaN
  } else if (isinf (x)) { // if the argument is infinity
    dst->u = arg.u & FP64_SIGN_BIT; // rcp14 = zero of the same sign
  } else if ((arg.u & FP64_LOW_63_BITS) == FP64_PLUS_ZERO_AS_UINT64) {
        // if the arg. is zero
    dst->u = (arg.u & FP64_SIGN_BIT) | FP64_EXP_MASK; // rcp14 
        // = infinity of the same sign
  } else if (!isnormal (x) && daz) {
      dst->u = (arg.u & FP64_SIGN_BIT) | FP64_EXP_MASK;
          // rcp14 = infinity of same sign
  } else if (!isnormal (x) && (arg.u & FP64_LOW_63_BITS) <= 
      FP64_SMALL_DEN_THRESHOLD) { // if the argument is a 'small' denormal
	  dst->u = (arg.u & FP64_SIGN_BIT) | FP64_EXP_MASK; 
              // rcp14 = infinity of the same sign
  } else if (!isnormal (x)) { // 'large' denormal and at least 
        // 0x0004000000000001ull in magnitude; rcp14 does not overflow
    bexp = 0x7fc; // start with 1021 as the biased exponent for the reciprocal 
        // (one below 1022)
    sign = arg.u & FP64_SIGN_BIT;
    arg1.u = arg.u & FP64_SIGNIF_MASK; // remove sign and exponent, to 
        // prepare for normalization
    while ((arg1.u & FP64_HIDDEN_BIT) == 0x0) {
      arg1.u = arg1.u << 1; 
      bexp++;
    }
    arg1.u = (arg1.u & FP64_SIGNIF_MASK) | FP64_PLUS_ONE_AS_UINT64; 
        // bring argument between 1.0 and 2.0
    RCP14D (mxcsr, &dst1, arg1);
    if (dst1.u == FP64_PLUS_ONE_AS_UINT64) bexp++; // to account for the fact 
        // that the reciprocal is not < 1.0 in this case
    dst->u = sign | (bexp << 52) | (dst1.u & FP64_SIGNIF_MASK); // rcp14
  } else if ((arg.u & FP64_SIGNIF_MASK) == 0x0) { // if it is 
        // an exact power of 2
    bexp = (arg.u & FP64_EXP_MASK) >> 52;
    if (bexp == 0x7fe) { // denormal reciprocal
      dst->u = (arg.u & FP64_SIGN_BIT) | FP64_QNAN_BIT; // rcp14
      /* NoVmp U1245: SDM wins - the reference masks with FP32_SIGN_BIT here, which gives +0 for
         -2^1023; VRCP14PD/SD flush underflow results "with the sign of the operand" */
      if (ftz) dst->u = dst->u & FP64_SIGN_BIT; // zero of the same sign
    } else { // rcp14 is not a denormal
      bexp = (0x3ff - (bexp - 0x3ff)) << 52;
      dst->u = (arg.u & FP64_SIGN_AND_TOP_OF_SIGNIF) | bexp; // rcp14
    }
  } else if ((arg.u & FP64_EXP_MASK) == FP64_PLUS_ONE_AS_UINT64) { 
        // // if 1 < |x| < 2)
    sign = arg.u & FP64_SIGN_BIT;
    // a[i] = RCP14_Coeff[2*i+1]; b[i] = RCP14_Coeff[2*i]
    i = floor ((x - 1.0) * 64); // exact
    y = 1.0 + 1.0/128 + i/64.0; // 8 bits (exact)
    ui64 = *((unsigned long long int *)&x); // x viewed as an unsigned int
    ui64 = ui64 & FP64_CLEAR_LOW_36_BITS; 
        // remove the 36 least significant bits
    x = *((double *)&ui64); // x with significand truncated to 17 bits
    xmy = x - y; // fits in 23 bits; exact
    a = RCP14_Coeff[2*i+1]; // b[i], up to 18 bits
    b = RCP14_Coeff[2*i]; // a[i], up to 10 bits
    da = (double)a; // double precision versions of a[i] and b[i], for exact 
      // computation
    db = 256 * (double)b; // multiply by 2^8 to make up for the difference 
      // between 18 bits in a and 10 bits in b
    dpr = (db * xmy); // product b[i] * (x - y), exact in double precision
    rcp14 = da - dpr; 
        // rcp14 = a[i] - b[i] * (x - y), exact in double precision
    ui64 = *((unsigned long long *)&rcp14); // rcp14, viewed as a 64-bit integer 
    ui64 = ui64 & FP64_CLEAR_LOW_36_BITS; // truncate to 28 bits = 
      // 1-bit sign + 11-bit exp + 16-bit fraction
    ui64 = ui64 - FP64_EXPON_DELTA_18; // divide by 2^18
    rcp14 = *((double *)&ui64); // rcp14 in double precision format
    dst->f = rcp14;
    dst->u = sign | dst->u; // rcp14
  } else { 
      // normal not between 1.0 and 2.0 in absolute value, and not a power of 2
    sign = arg.u & FP64_SIGN_BIT;
    bexp = (arg.u & FP64_EXP_MASK) >> 52;
    signif = FP64_HIDDEN_BIT | (arg.u & FP64_SIGNIF_MASK);
    if (bexp == 0x7fe) { // denormal reciprocal, and the significand must be 
        //shifted by 2
      arg1.u = (arg.u & FP64_SIGN_AND_SIGNIF) | FP64_PLUS_ONE_AS_UINT64; 
          // bring argument between 1.0 and 2.0
      RCP14D (mxcsr, &dst1, arg1);
      signif = FP64_HIDDEN_BIT | (dst1.u & FP64_SIGNIF_MASK);
      dst->u = (dst1.u & FP64_SIGN_BIT) | (signif >> 2); 
          // rcp14; the new biased exponent is 0x0
    } else if (bexp == 0x7fd && signif >= FP64_SIGNIF_SHIFT_THRESHOLD) { 
          // denormal reciprocal, and the significand must be shifted by 1
      arg1.u = (arg.u & FP64_SIGN_AND_SIGNIF) | FP64_PLUS_ONE_AS_UINT64; 
          // bring argument between 1.0 and 2.0
      RCP14D (mxcsr, &dst1, arg1);
      signif = FP64_HIDDEN_BIT | (dst1.u & FP64_SIGNIF_MASK);
      dst->u = (dst1.u & FP64_SIGN_BIT) | (signif >> 1); 
          // rcp14; the new biased exponent is 0x0
    } else {
      bexp = 0x7fe - bexp;
      arg1.u = (arg.u & FP64_SIGN_AND_SIGNIF) | FP64_PLUS_ONE_AS_UINT64; 
          // bring argument between 1.0 and 2.0
      RCP14D (mxcsr, &dst1, arg1);
      dst->u = (dst1.u & FP64_SIGN_AND_SIGNIF) | ((bexp - 1) << 52); // rcp14
    }
    if (!isnormal (dst->f) && ftz) {
      dst->u = dst->u & FP64_SIGN_BIT; // zero of the same sign
    }
  }
}

// Note on the RSQRT14 algorithm, which calculates
//   rsqrt14(x) = reciprocal approximation of x:
// if (x is in [1.0, 2.0))
//   i = int_floor((x-1)*32)
//   y[i] = 1 + 1/64 + i/32
//   rsqrt14(x) = truncate_to_17_ms_bits(c[i]-d[i]*(x-y[i]))
// else if (x is in [2.0, 4.0))
//   i = int_floor((x-2)*16)
//   y[i] = 2 + 1/32 + i/16
//   rsqrt14(x) = truncate_to_17_ms_bits(c[i]-d[i]*(x-y[i]))


// c[i] = RSQRT14_Coeff[2*i+1] 19 bits; d[i] = RSQRT14_Coeff[2*i] 10 bits;
//     coeff*1024 freeTerm*524288 
static const unsigned int RSQRT14_Coeff[128] = { /* NoVmp U1245: static const */
  1001, 520261, 707, 367881, 955, 512437, 675, 362349,
  915,  504953, 647, 357056, 877, 497790, 619, 351992,
  841,  490922, 595, 347136, 807, 484331, 571, 342475,
  775,  478001, 549, 337997, 747, 471909, 527, 333693,
  719,  466046, 509, 329545, 693, 460397, 491, 325551,
  669,  454947, 473, 321697, 647, 449688, 457, 317977,
  625,  444606, 441, 314385, 603, 439694, 427, 310910,
  585,  434939, 413, 307549, 567, 430335, 401, 304295,
  549,  425875, 389, 301139, 533, 421551, 377, 298079,
  517,  417355, 365, 295115, 501, 413284, 355, 292237,
  487,  409329, 345, 289439, 473, 405487, 335, 286722,
  461,  401748, 325, 284080, 449, 398111, 317, 281508,
  437,  394571, 309, 279006, 425, 391127, 301, 276569,
  415,  387770, 293, 274195, 403, 384498, 285, 271882,
  393,  381307, 279, 269625, 385, 378194, 271, 267425,
  375,  375155, 265, 265276, 367, 372190, 259, 263178
};

static void /* NoVmp U1245: static */
RSQRT14S (unsigned int mxcsr, type32 *dst, type32 arg) {
  unsigned int i, c, d; // i, c[i], d[i] - used when 1 <= x < 4
  float x, y, xmy, rsqrt14; // x (input), y, x - y, recp14 (result)
  double dc, dd, dxmy, drsqrt14, dpr; // double versions for exact computations
  unsigned long long ui64; // used for bit manipulation of double values
  unsigned int ui32;// used for bit manipulation of float values
  int expon; // unbiased exponent
  int n; // scaling factor
  unsigned int signif; // significand
  unsigned int daz = mxcsr & FP32_BIT_6; // bit 6 of MXCSR
  unsigned int sign = arg.u & FP32_SIGN_BIT;  // the sign bit, also 0.0 with the sign of arg.f

  // apply daz if necessary
  if (daz && ((arg.u & FP32_LOW_31_BITS) < FP32_HIDDEN_BIT)) {
    arg.u = sign;  // zero with sign of arg
  }

  // the following makes use of the IEEE 754-2008 encoding for 32-bit and 64-bit
  //     binary floating-point values
  x = arg.f;
  if (isnan (x)) {
    dst->u = arg.u | FP32_QNAN_BIT; // rsqrt14 = quietized NaN
  } else if (x < 0.0) {
    dst->u = FP32_DEFAULT_NAN_AS_UINT32; // rsqrt14 = QNaN Indefinite
  } else if (isinf (x)) {
    dst->u = 0; // rsqrt14(+inf) = +zero
  } else if ((arg.u & FP32_LOW_31_BITS) == 0x0) { // zero
    dst->u = sign | FP32_EXP_MASK; 
        // rsqrt14 = infinity of same sign
  } else if (arg.u  == FP32_PLUS_ONE_AS_UINT32) { // if x is 1.0
	  dst->u = arg.u; // rsqrt14 = 1.0
  } else if ((arg.u & FP32_EXP_MASK) == FP32_PLUS_ONE_AS_UINT32) { 
      // if 1 < |x| < 2) c[i] = RSQRT14_Coeff[4*i+1]; d[i] = RSQRT14_Coeff[4*i]
    i = floor ((x - 1.0) * 32); // exact; 32 different values
    y = 1.0 + 1.0/64 + i/32.0; // 8 bits (exact)
    ui32 = *((unsigned int *)&x); // x viewed as an unsigned int
    ui32 = ui32 & FP32_CLEAR_LOW_8_BITS; // remove the 8 least significant bits
    x = *((float *)&ui32); // x with significand truncated to 17 bits
    xmy = x - y; // fits in 23 bits; exact
    c = RSQRT14_Coeff[4*i+1]; // c[i], up to 18 bits
    d = RSQRT14_Coeff[4*i]; // d[i], up to 10 bits
    dc = (double)c; // double precision versions of c[i] and d[i], 
        // for exact computation
    dd = 256 * (double)d; // multiply by 2^8 to make up for the difference 
        // between 18 bits in a and 10 bits in b
    dxmy = (double)xmy; // x - y as a double
    dpr = (dd * dxmy); // product d[i] * (x - y), exact in double precision
    drsqrt14 = dc - dpr; // rsqrt14 = c[i] - d[i] * (x - y), exact in 
        // double precision
    ui64 = *((unsigned long long *)&drsqrt14); // rsqrt14, viewed as 
        // a 64-bit integer 
    ui64 = ui64 & FP64_CLEAR_LOW_36_BITS; // truncate to 28 bits = 
        // 1-bit sign + 11-bit exp + 16-bit fraction
    ui64 = ui64 - FP64_EXPON_DELTA_19; // divide by 2^19
    drsqrt14 = *((double *)&ui64); // rsqrt14 in double precision format
    rsqrt14 = (float)drsqrt14; // rsqrt14 is single precision; exact conversion
    dst->f = rsqrt14;
  } else if ((arg.u & FP32_EXP_MASK) == FP32_PLUS_TWO_AS_UINT32) { 
      // if 2 <= |x| < 4) c[i]=RSQRT14_Coeff[4*i+3]; d[i]=RSQRT14_Coeff[4*i+2]
    i = floor ((x - 2.0) * 16); // exact; 32 different values
    y = 2.0 + 1.0/32 + i/16.0; // 8 bits (exact)
    ui32 = *((unsigned int *)&x); // x viewed as an unsigned int
    ui32 = ui32 & FP32_CLEAR_LOW_8_BITS; // remove the 8 least significant bits
    x = *((float *)&ui32); // x with significand truncated to 17 bits
    xmy = x - y; // fits in 23 bits; exact
    c = RSQRT14_Coeff[4*i+3]; // c[i], up to 18 bits
    d = RSQRT14_Coeff[4*i+2]; // d[i], up to 10 bits
    dc = (double)c; // double precision versions of c[i] and d[i], 
        // for exact computation
    dd = 128 * (double)d; // multiply by 2^7 to make up for the difference 
        // between 18 bits in a and 11 bits in b
    dxmy = (double)xmy; // x - y as a double
    dpr = (dd * dxmy); // product d[i] * (x - y), exact in double precision
    drsqrt14 = dc - dpr; // rsqrt14 = c[i] - d[i] * (x - y), 
        // exact in double precision
    ui64 = *((unsigned long long *)&drsqrt14); // rsqrt14, viewed as 
        // a 64-bit integer 
    ui64 = ui64 & FP64_CLEAR_LOW_36_BITS; // truncate to 28 bits 
        // = 1-bit sign + 11-bit exp + 16-bit fraction
    ui64 = ui64 - FP64_EXPON_DELTA_19; // divide by 2^19
    drsqrt14 = *((double *)&ui64); // rsqrt14 in double precision format
    rsqrt14 = (float)drsqrt14; // rsqrt14 is single precision; exact conversion
    dst->f = rsqrt14;
  } else if ((arg.u & FP32_EXP_MASK) == FP32_PLUS_ZERO_AS_UINT32) { 
      // denormal; find n such that 1<=2^(2n)*x<4
    expon = -126;
    signif = arg.u & FP32_SIGNIF_MASK;
    while ((signif & FP32_HIDDEN_BIT) == 0x0) { // normalize significand and 
        // adjust exponent
      signif = signif << 1;
      expon--;
    }
    if (expon & 0x01) { // odd exponent; scaled x will be between 2.0 and 4.0
      n = (expon - 1) / 2; // odd expon in -149, -127
      arg.u = FP32_PLUS_TWO_AS_UINT32 | (signif & FP32_SIGNIF_MASK); 
          // scaled x between 2.0 and 4.0
      RSQRT14S (mxcsr, dst, arg);
      dst->u = dst->u - (n << 23); // rsqrt14
    } else { // even exponent, scaled x between 1.0 and 2.0
      n = expon / 2; // even expon in -149, -128
      arg.u = FP32_PLUS_ONE_AS_UINT32 | (signif & FP32_SIGNIF_MASK); 
          // scaled x between 1.0 and 2.0
      RSQRT14S (mxcsr, dst, arg);
      dst->u = dst->u - (n << 23); // rsqrt14
    }
  } else { // normal not between 1.0 and 4.0; find n such that 1<=2^(2n)*x<4
    expon = ((arg.u & FP32_EXP_MASK) >> 23) - 0x7f;
    signif = (arg.u & FP32_SIGNIF_MASK) | FP32_HIDDEN_BIT;
    if (expon & 0x01) { // odd exponent, scaled x between 2.0 and 4.0
      n = (expon - 1) / 2; // odd expon in -126, 127
      arg.u = FP32_PLUS_TWO_AS_UINT32 | (signif & FP32_SIGNIF_MASK); 
          // scaled x between 2.0 and 4.0
      RSQRT14S (mxcsr, dst, arg);
      dst->u = dst->u - (n << 23); // rsqrt14
    } else { // even exponent, scaled x between 1.0 and 2.0
      n = expon / 2; // even expon in -126, 127
      arg.u = FP32_PLUS_ONE_AS_UINT32 | (signif & FP32_SIGNIF_MASK); 
          // scaled x between 1.0 and 2.0
      RSQRT14S (mxcsr, dst, arg);
      dst->u = dst->u - (n << 23); // rsqrt14
    }
  }
}


static void /* NoVmp U1245: static */
RSQRT14D (unsigned int mxcsr, type64 *dst, type64 arg) {
  unsigned int i, c, d; // i, c[i], d[i] - used when 1 <= x < 4
  double x, y, xmy, rsqrt14; // x (input), y, x - y, recp14 (result)
  double dc, dd, dpr; // double versions for exact computations
  unsigned long long ui64; // used for bit manipulation of double values
  long long int expon; // unbiased exponent
  long long int n; // scaling factor
  unsigned long long int signif; // significand
  unsigned int daz = mxcsr & FP32_BIT_6; // bit 6 of MXCSR
  unsigned long long sign = arg.u & FP64_SIGN_BIT;  // the sign bit

  // apply daz if necessary
  if (daz && ((arg.u & FP64_LOW_63_BITS) < FP64_HIDDEN_BIT)) {
    arg.u = sign;  // zero with sign of arg
  }

  // the following makes use of the IEEE 754-2008 encoding for 32-bit and 64-bit
  //     binary floating-point values
  x = arg.f;
  if (isnan (x)) {
    dst->u = arg.u | FP64_QNAN_BIT; // rsqrt14 = quietized NaN
  } else if (x < 0.0) {
    dst->u = FP64_DEFAULT_NAN_AS_UINT64; // rsqrt14 = QNaN Indefinite
  } else if (isinf (x)) {
    dst->u = 0; // rsqrt14(+inf) = +zero
  } else if ((arg.u & FP64_LOW_63_BITS) == 0x0) { // zero
    dst->u = sign | FP64_EXP_MASK; 
        // rsqrt14 = infinity of same sign
  } else if (arg.u  == FP64_PLUS_ONE_AS_UINT64) { // if x is 1.0
	  dst->u = arg.u; // rsqrt14 = 1.0
  } else if ((arg.u & FP64_EXP_MASK) == FP64_PLUS_ONE_AS_UINT64) { 
        // if 1 < |x| < 2)
    // c[i] = RSQRT14_Coeff[4*i+1]; d[i] = RSQRT14_Coeff[4*i]
    i = floor ((x - 1.0) * 32); // exact; 32 different values
    y = 1.0 + 1.0/64 + i/32.0; // 8 bits (exact)
    ui64 = *((unsigned long long int *)&x); // x viewed as an unsigned int
    ui64 = ui64 & FP64_CLEAR_LOW_37_BITS ; 
        // remove the 37 least significant bits
    x = *((double *)&ui64); // x with significand truncated to 17 bits
    xmy = x - y; // fits in 23 bits; exact
    c = RSQRT14_Coeff[4*i+1]; // c[i], up to 18 bits
    d = RSQRT14_Coeff[4*i]; // d[i], up to 10 bits
    dc = (double)c; // double precision versions of c[i] and d[i], 
        // for exact computation
    dd = 256 * (double)d; // multiply by 2^8 to make up for the difference 
        // between 18 bits in a and 10 bits in b
    dpr = (dd * xmy); // product d[i] * (x - y), exact in double precision
    rsqrt14 = dc - dpr; // rsqrt14 = c[i] - d[i] * (x - y), exact in 
        // double precision
    ui64 = *((unsigned long long int *)&rsqrt14); // rsqrt14, viewed as 
        // a 64-bit integer 
    ui64 = ui64 & FP64_CLEAR_LOW_36_BITS; // truncate to 28 bits = 
        // 1-bit sign + 11-bit exp + 16-bit fraction
    ui64 = ui64 - FP64_EXPON_DELTA_19; // divide by 2^19
    rsqrt14 = *((double *)&ui64); // rsqrt14 in double precision format
    dst->f = rsqrt14;
  } else if ((arg.u & FP64_EXP_MASK) == FP64_PLUS_TWO_AS_UINT64) { 
        // if 2 <= |x| < 4)
    // c[i] = RSQRT14_Coeff[4*i+3]; d[i] = RSQRT14_Coeff[4*i+2]
    i = floor ((x - 2.0) * 16); // exact; 32 different values
    y = 2.0 + 1.0/32 + i/16.0; // 8 bits (exact)
    ui64 = *((unsigned long long int *)&x); // x viewed as an unsigned int
    ui64 = ui64 & FP64_CLEAR_LOW_37_BITS ; 
        // remove the 8 least significant bits
    x = *((double *)&ui64); // x with significand truncated to 17 bits
    xmy = x - y; // fits in 23 bits; exact
    c = RSQRT14_Coeff[4*i+3]; // c[i], up to 18 bits
    d = RSQRT14_Coeff[4*i+2]; // d[i], up to 10 bits
    dc = (double)c; // double precision versions of c[i] and d[i], 
        // for exact computation
    dd = 128 * (double)d; // multiply by 2^7 to make up for the difference 
        // between 18 bits in a and 11 bits in b
    dpr = (dd * xmy); // product d[i] * (x - y), exact in double precision
    rsqrt14 = dc - dpr; // rsqrt14 = c[i] - d[i] * (x - y), 
        // exact in double precision
    ui64 = *((unsigned long long *)&rsqrt14); // rsqrt14, viewed as 
        // a 64-bit integer 
    ui64 = ui64 & FP64_CLEAR_LOW_36_BITS; // truncate to 28 bits = 
        // 1-bit sign + 11-bit exp + 16-bit fraction
    ui64 = ui64 - FP64_EXPON_DELTA_19; // divide by 2^19
    rsqrt14 = *((double *)&ui64); // rsqrt14 in double precision format
    dst->f = rsqrt14;
  } else if ((arg.u & FP64_EXP_MASK) == FP64_PLUS_ZERO_AS_UINT64) { 
        // denormal; find n such that 1<=2^(2n)*x<4
    expon = -1022;
    signif = arg.u & FP64_SIGNIF_MASK;
    while ((signif & FP64_HIDDEN_BIT) == FP64_PLUS_ZERO_AS_UINT64) { 
        // normalize significand and adjust exponent
      signif = signif << 1;
      expon--;
    }
    if (expon & 0x01) { // odd exponent; scaled x will be between 2.0 and 4.0
      n = (expon - 1) / 2; // odd expon in -1074 -1023
      arg.u = FP64_PLUS_TWO_AS_UINT64 | (signif & FP64_SIGNIF_MASK); 
        // scaled x between 2.0 and 4.0
      RSQRT14D (mxcsr, dst, arg);
      dst->u = dst->u - (n << 52); // rsqrt14
    } else { // even exponent, scaled x between 1.0 and 2.0
      n = expon / 2; // even expon in -149, -128
      arg.u = FP64_PLUS_ONE_AS_UINT64 | (signif & FP64_SIGNIF_MASK); 
        // scaled x between 1.0 and 2.0
      RSQRT14D (mxcsr, dst, arg);
      dst->u = dst->u - (n << 52); // rsqrt14
    }
  } else { // normal not between 1.0 and 4.0; find n such that 1<=2^(2n)*x<4
    expon = ((arg.u & FP64_EXP_MASK) >> 52) - 0x3ff;
    signif = (arg.u & FP64_SIGNIF_MASK) | FP64_HIDDEN_BIT;
    if (expon & 0x01) { // odd exponent, scaled x between 2.0 and 4.0
      n = (expon - 1) / 2; // odd expon in -1022, 1023
      arg.u = FP64_PLUS_TWO_AS_UINT64 | (signif & FP64_SIGNIF_MASK); 
        // scaled x between 2.0 and 4.0
      RSQRT14D (mxcsr, dst, arg);
      dst->u = dst->u - (n << 52); // rsqrt14
    } else { // even exponent, scaled x between 1.0 and 2.0
      n = expon / 2; // even expon in -126, 127
      arg.u = FP64_PLUS_ONE_AS_UINT64 | (signif & FP64_SIGNIF_MASK); 
        // scaled x between 1.0 and 2.0
      RSQRT14D (mxcsr, dst, arg);
      dst->u = dst->u - (n << 52); // rsqrt14
    }
  }
}

/* NoVmp U1245: the reference's macros stay local to this file */
#undef FP32_SIGN_BIT
#undef FP32_EXP_MASK
#undef FP32_SIGNIF_MASK
#undef FP32_HIDDEN_BIT
#undef FP32_QNAN_BIT
#undef FP32_DEFAULT_NAN_AS_UINT32
#undef FP32_PLUS_ZERO_AS_UINT32
#undef FP32_PLUS_ONE_AS_UINT32
#undef FP32_PLUS_TWO_AS_UINT32
#undef FP32_SMALL_DEN_THRESHOLD
#undef FP32_SIGNIF_SHIFT_THRESHOLD
#undef FP32_LOW_31_BITS
#undef FP32_SIGN_AND_TOP_OF_SIGNIF
#undef FP32_SIGN_AND_SIGNIF
#undef FP32_CLEAR_LOW_8_BITS
#undef FP32_CLEAR_LOW_7_BITS
#undef FP32_BIT_15
#undef FP32_BIT_6
#undef FP64_SIGN_BIT
#undef FP64_EXP_MASK
#undef FP64_SIGNIF_MASK
#undef FP64_HIDDEN_BIT
#undef FP64_QNAN_BIT
#undef FP64_DEFAULT_NAN_AS_UINT64
#undef FP64_PLUS_ZERO_AS_UINT64
#undef FP64_PLUS_ONE_AS_UINT64
#undef FP64_PLUS_TWO_AS_UINT64
#undef FP64_SMALL_DEN_THRESHOLD
#undef FP64_SIGNIF_SHIFT_THRESHOLD
#undef FP64_EXPON_DELTA_18
#undef FP64_EXPON_DELTA_19
#undef FP64_LOW_63_BITS
#undef FP64_SIGN_AND_TOP_OF_SIGNIF
#undef FP64_SIGN_AND_SIGNIF
#undef FP64_CLEAR_LOW_37_BITS
#undef FP64_CLEAR_LOW_36_BITS
