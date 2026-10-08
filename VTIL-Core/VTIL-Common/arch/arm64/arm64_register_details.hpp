// Copyright (c) 2020 Can Boluk and contributors of the VTIL Project   
// All rights reserved.   
//    
// Redistribution and use in source and binary forms, with or without   
// modification, are permitted provided that the following conditions are met: 
//    
// 1. Redistributions of source code must retain the above copyright notice,   
//    this list of conditions and the following disclaimer.   
// 2. Redistributions in binary form must reproduce the above copyright   
//    notice, this list of conditions and the following disclaimer in the   
//    documentation and/or other materials provided with the distribution.   
// 3. Neither the name of VTIL Project nor the names of its contributors
//    may be used to endorse or promote products derived from this software 
//    without specific prior written permission.   
//    
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE   
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE  
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE   
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR   
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF   
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN   
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)   
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE  
// POSSIBILITY OF SUCH DAMAGE.        
//

// Furthermore, the following pieces of software have additional copyrights
// licenses, and/or restrictions:
//
// |--------------------------------------------------------------------------|
// | File name               | Link for further information                   |
// |-------------------------|------------------------------------------------|
// | arm64/*                 | https://github.com/aquynh/capstone/            |
// |                         | https://github.com/keystone-engine/keystone/   |
// |--------------------------------------------------------------------------|
//
#pragma once
#include <map>
#include <tuple>
#include <string>
#include "../../io/asserts.hpp"
#include "arm64_disassembler.hpp"
#include "../register_mapping.hpp"

// Capstone 6 has no separate V0-V31 register ids: the 128-bit SIMD register is Q0-Q31.
#if CS_API_MAJOR >= 6
#define VTIL_ARM64_VBASE( n ) ARM64_REG_Q##n
#define VTIL_ARM64_VINST( n )
#else
#define VTIL_ARM64_VBASE( n ) ARM64_REG_V##n
#define VTIL_ARM64_VINST( n ) { ARM64_REG_V##n, { ARM64_REG_V##n, 0, 16 } },
#endif

namespace vtil::arm64
{
	// List of all physical registers and the base registers they map to <0> at offset <1> of size <2>.
	//
	static constexpr register_map<arm64_reg, ARM64_REG_ENDING> registers =
	{
		{
			/* [Instance]           [Base]       [Offset] [Size]  */
			/*                   General Purpose                  */
			{ ARM64_REG_X0,		{ ARM64_REG_X0,		0,		8	} },
			{ ARM64_REG_W0,		{ ARM64_REG_X0,		0,		4	} },

			{ ARM64_REG_X1,		{ ARM64_REG_X1,		0,		8	} },
			{ ARM64_REG_W1,		{ ARM64_REG_X1,		0,		4	} },

			{ ARM64_REG_X2,		{ ARM64_REG_X2,		0,		8	} },
			{ ARM64_REG_W2,		{ ARM64_REG_X2,		0,		4	} },

			{ ARM64_REG_X3,		{ ARM64_REG_X3,		0,		8	} },
			{ ARM64_REG_W3,		{ ARM64_REG_X3,		0,		4	} },

			{ ARM64_REG_X4,		{ ARM64_REG_X4,		0,		8	} },
			{ ARM64_REG_W4,		{ ARM64_REG_X4,		0,		4	} },

			{ ARM64_REG_X5,		{ ARM64_REG_X5,		0,		8	} },
			{ ARM64_REG_W5,		{ ARM64_REG_X5,		0,		4	} },

			{ ARM64_REG_X6,		{ ARM64_REG_X6,		0,		8	} },
			{ ARM64_REG_W6,		{ ARM64_REG_X6,		0,		4	} },

			{ ARM64_REG_X7,		{ ARM64_REG_X7,		0,		8	} },
			{ ARM64_REG_W7,		{ ARM64_REG_X7,		0,		4	} },

			{ ARM64_REG_X8,		{ ARM64_REG_X8,		0,		8	} },
			{ ARM64_REG_W8,		{ ARM64_REG_X8,		0,		4	} },

			{ ARM64_REG_X9,		{ ARM64_REG_X9,		0,		8	} },
			{ ARM64_REG_W9,		{ ARM64_REG_X9,		0,		4	} },

			{ ARM64_REG_X10,	{ ARM64_REG_X10,	0,		8	} },
			{ ARM64_REG_W10,	{ ARM64_REG_X10,	0,		4	} },

			{ ARM64_REG_X11,	{ ARM64_REG_X11,	0,		8	} },
			{ ARM64_REG_W11,	{ ARM64_REG_X11,	0,		4	} },

			{ ARM64_REG_X12,	{ ARM64_REG_X12,	0,		8	} },
			{ ARM64_REG_W12,	{ ARM64_REG_X12,	0,		4	} },

			{ ARM64_REG_X13,	{ ARM64_REG_X13,	0,		8	} },
			{ ARM64_REG_W13,	{ ARM64_REG_X13,	0,		4	} },

			{ ARM64_REG_X14,	{ ARM64_REG_X14,	0,		8	} },
			{ ARM64_REG_W14,	{ ARM64_REG_X14,	0,		4	} },

			{ ARM64_REG_X15,	{ ARM64_REG_X15,	0,		8	} },
			{ ARM64_REG_W15,	{ ARM64_REG_X15,	0,		4	} },

			{ ARM64_REG_X16,	{ ARM64_REG_X16,	0,		8	} },
			//{ ARM64_REG_IP0,	{ ARM64_REG_X16,	0,		8	} },  // alias x16 = ip0
			{ ARM64_REG_W16,	{ ARM64_REG_X16,	0,		4	} },

			{ ARM64_REG_X17,	{ ARM64_REG_X17,	0,		8	} },
			//{ ARM64_REG_IP1,	{ ARM64_REG_X17,	0,		8	} },  // alias x17 = ip1
			{ ARM64_REG_W17,	{ ARM64_REG_X17,	0,		4	} },

			{ ARM64_REG_X18,	{ ARM64_REG_X18,	0,		8	} },
			{ ARM64_REG_W18,	{ ARM64_REG_X18,	0,		4	} },

			{ ARM64_REG_X19,	{ ARM64_REG_X19,	0,		8	} },
			{ ARM64_REG_W19,	{ ARM64_REG_X19,	0,		4	} },

			{ ARM64_REG_X20,	{ ARM64_REG_X20,	0,		8	} },
			{ ARM64_REG_W20,	{ ARM64_REG_X20,	0,		4	} },

			{ ARM64_REG_X21,	{ ARM64_REG_X21,	0,		8	} },
			{ ARM64_REG_W21,	{ ARM64_REG_X21,	0,		4	} },

			{ ARM64_REG_X22,	{ ARM64_REG_X22,	0,		8	} },
			{ ARM64_REG_W22,	{ ARM64_REG_X22,	0,		4	} },

			{ ARM64_REG_X23,	{ ARM64_REG_X23,	0,		8	} },
			{ ARM64_REG_W23,	{ ARM64_REG_X23,	0,		4	} },

			{ ARM64_REG_X24,	{ ARM64_REG_X24,	0,		8	} },
			{ ARM64_REG_W24,	{ ARM64_REG_X24,	0,		4	} },

			{ ARM64_REG_X25,	{ ARM64_REG_X25,	0,		8	} },
			{ ARM64_REG_W25,	{ ARM64_REG_X25,	0,		4	} },

			{ ARM64_REG_X26,	{ ARM64_REG_X26,	0,		8	} },
			{ ARM64_REG_W26,	{ ARM64_REG_X26,	0,		4	} },

			{ ARM64_REG_X27,	{ ARM64_REG_X27,	0,		8	} },
			{ ARM64_REG_W27,	{ ARM64_REG_X27,	0,		4	} },

			{ ARM64_REG_X28,	{ ARM64_REG_X28,	0,		8	} },
			{ ARM64_REG_W28,	{ ARM64_REG_X28,	0,		4	} },

			/*                      Special                       */
			{ ARM64_REG_X29,	{ ARM64_REG_X29,	0,		8	} },
			//{ ARM64_REG_FP,		{ ARM64_REG_X29,	0,		8	} },  // alias x29 = fp
			{ ARM64_REG_W29,	{ ARM64_REG_X29,	0,		4	} },

			{ ARM64_REG_X30,	{ ARM64_REG_X30,	0,		8	} },
			//{ ARM64_REG_LR,		{ ARM64_REG_X30,	0,		8	} },  // alias x30 = lr
			{ ARM64_REG_W30,	{ ARM64_REG_X30,	0,		4	} },

			{ ARM64_REG_XZR,	{ ARM64_REG_XZR,	0,		8	} },
			{ ARM64_REG_WZR,	{ ARM64_REG_XZR,	0,		4	} },

			{ ARM64_REG_SP,		{ ARM64_REG_SP,		0,		8	} },
			{ ARM64_REG_WSP,	{ ARM64_REG_SP,		0,		4	} },

			{ ARM64_REG_NZCV,	{ ARM64_REG_NZCV,	0,		8	} },

			/*                      SIMD/FP                       */
			VTIL_ARM64_VINST( 0 )
			{ ARM64_REG_Q0,		{ VTIL_ARM64_VBASE( 0 ),		0,		16	} },  // alias v0 = q0
			{ ARM64_REG_D0,		{ VTIL_ARM64_VBASE( 0 ),		0,		8	} },
			{ ARM64_REG_S0,		{ VTIL_ARM64_VBASE( 0 ),		0,		4	} },
			{ ARM64_REG_H0,		{ VTIL_ARM64_VBASE( 0 ),		0,		2	} },
			{ ARM64_REG_B0,		{ VTIL_ARM64_VBASE( 0 ),		0,		1	} },

			VTIL_ARM64_VINST( 1 )
			{ ARM64_REG_Q1,		{ VTIL_ARM64_VBASE( 1 ),		0,		16	} },  // alias v1 = q1
			{ ARM64_REG_D1,		{ VTIL_ARM64_VBASE( 1 ),		0,		8	} },
			{ ARM64_REG_S1,		{ VTIL_ARM64_VBASE( 1 ),		0,		4	} },
			{ ARM64_REG_H1,		{ VTIL_ARM64_VBASE( 1 ),		0,		2	} },
			{ ARM64_REG_B1,		{ VTIL_ARM64_VBASE( 1 ),		0,		1	} },

			VTIL_ARM64_VINST( 2 )
			{ ARM64_REG_Q2,		{ VTIL_ARM64_VBASE( 2 ),		0,		16	} },  // alias v2 = q2
			{ ARM64_REG_D2,		{ VTIL_ARM64_VBASE( 2 ),		0,		8	} },
			{ ARM64_REG_S2,		{ VTIL_ARM64_VBASE( 2 ),		0,		4	} },
			{ ARM64_REG_H2,		{ VTIL_ARM64_VBASE( 2 ),		0,		2	} },
			{ ARM64_REG_B2,		{ VTIL_ARM64_VBASE( 2 ),		0,		1	} },

			VTIL_ARM64_VINST( 3 )
			{ ARM64_REG_Q3,		{ VTIL_ARM64_VBASE( 3 ),		0,		16	} },  // alias v3 = q3
			{ ARM64_REG_D3,		{ VTIL_ARM64_VBASE( 3 ),		0,		8	} },
			{ ARM64_REG_S3,		{ VTIL_ARM64_VBASE( 3 ),		0,		4	} },
			{ ARM64_REG_H3,		{ VTIL_ARM64_VBASE( 3 ),		0,		2	} },
			{ ARM64_REG_B3,		{ VTIL_ARM64_VBASE( 3 ),		0,		1	} },

			VTIL_ARM64_VINST( 4 )
			{ ARM64_REG_Q4,		{ VTIL_ARM64_VBASE( 4 ),		0,		16	} },  // alias v4 = q4
			{ ARM64_REG_D4,		{ VTIL_ARM64_VBASE( 4 ),		0,		8	} },
			{ ARM64_REG_S4,		{ VTIL_ARM64_VBASE( 4 ),		0,		4	} },
			{ ARM64_REG_H4,		{ VTIL_ARM64_VBASE( 4 ),		0,		2	} },
			{ ARM64_REG_B4,		{ VTIL_ARM64_VBASE( 4 ),		0,		1	} },

			VTIL_ARM64_VINST( 5 )
			{ ARM64_REG_Q5,		{ VTIL_ARM64_VBASE( 5 ),		0,		16	} },  // alias v5 = q5
			{ ARM64_REG_D5,		{ VTIL_ARM64_VBASE( 5 ),		0,		8	} },
			{ ARM64_REG_S5,		{ VTIL_ARM64_VBASE( 5 ),		0,		4	} },
			{ ARM64_REG_H5,		{ VTIL_ARM64_VBASE( 5 ),		0,		2	} },
			{ ARM64_REG_B5,		{ VTIL_ARM64_VBASE( 5 ),		0,		1	} },

			VTIL_ARM64_VINST( 6 )
			{ ARM64_REG_Q6,		{ VTIL_ARM64_VBASE( 6 ),		0,		16	} },  // alias v6 = q6
			{ ARM64_REG_D6,		{ VTIL_ARM64_VBASE( 6 ),		0,		8	} },
			{ ARM64_REG_S6,		{ VTIL_ARM64_VBASE( 6 ),		0,		4	} },
			{ ARM64_REG_H6,		{ VTIL_ARM64_VBASE( 6 ),		0,		2	} },
			{ ARM64_REG_B6,		{ VTIL_ARM64_VBASE( 6 ),		0,		1	} },

			VTIL_ARM64_VINST( 7 )
			{ ARM64_REG_Q7,		{ VTIL_ARM64_VBASE( 7 ),		0,		16	} },  // alias v7 = q7
			{ ARM64_REG_D7,		{ VTIL_ARM64_VBASE( 7 ),		0,		8	} },
			{ ARM64_REG_S7,		{ VTIL_ARM64_VBASE( 7 ),		0,		4	} },
			{ ARM64_REG_H7,		{ VTIL_ARM64_VBASE( 7 ),		0,		2	} },
			{ ARM64_REG_B7,		{ VTIL_ARM64_VBASE( 7 ),		0,		1	} },

			VTIL_ARM64_VINST( 8 )
			{ ARM64_REG_Q8,		{ VTIL_ARM64_VBASE( 8 ),		0,		16	} },  // alias v8 = q8
			{ ARM64_REG_D8,		{ VTIL_ARM64_VBASE( 8 ),		0,		8	} },
			{ ARM64_REG_S8,		{ VTIL_ARM64_VBASE( 8 ),		0,		4	} },
			{ ARM64_REG_H8,		{ VTIL_ARM64_VBASE( 8 ),		0,		2	} },
			{ ARM64_REG_B8,		{ VTIL_ARM64_VBASE( 8 ),		0,		1	} },

			VTIL_ARM64_VINST( 9 )
			{ ARM64_REG_Q9,		{ VTIL_ARM64_VBASE( 9 ),		0,		16	} },  // alias v9 = q9
			{ ARM64_REG_D9,		{ VTIL_ARM64_VBASE( 9 ),		0,		8	} },
			{ ARM64_REG_S9,		{ VTIL_ARM64_VBASE( 9 ),		0,		4	} },
			{ ARM64_REG_H9,		{ VTIL_ARM64_VBASE( 9 ),		0,		2	} },
			{ ARM64_REG_B9,		{ VTIL_ARM64_VBASE( 9 ),		0,		1	} },

			VTIL_ARM64_VINST( 10 )
			{ ARM64_REG_Q10,	{ VTIL_ARM64_VBASE( 10 ),	0,		16	} },  // alias v10 = q10
			{ ARM64_REG_D10,	{ VTIL_ARM64_VBASE( 10 ),	0,		8	} },
			{ ARM64_REG_S10,	{ VTIL_ARM64_VBASE( 10 ),	0,		4	} },
			{ ARM64_REG_H10,	{ VTIL_ARM64_VBASE( 10 ),	0,		2	} },
			{ ARM64_REG_B10,	{ VTIL_ARM64_VBASE( 10 ),	0,		1	} },

			VTIL_ARM64_VINST( 11 )
			{ ARM64_REG_Q11,	{ VTIL_ARM64_VBASE( 11 ),	0,		16	} },  // alias v11 = q11
			{ ARM64_REG_D11,	{ VTIL_ARM64_VBASE( 11 ),	0,		8	} },
			{ ARM64_REG_S11,	{ VTIL_ARM64_VBASE( 11 ),	0,		4	} },
			{ ARM64_REG_H11,	{ VTIL_ARM64_VBASE( 11 ),	0,		2	} },
			{ ARM64_REG_B11,	{ VTIL_ARM64_VBASE( 11 ),	0,		1	} },

			VTIL_ARM64_VINST( 12 )
			{ ARM64_REG_Q12,	{ VTIL_ARM64_VBASE( 12 ),	0,		16	} },  // alias v12 = q12
			{ ARM64_REG_D12,	{ VTIL_ARM64_VBASE( 12 ),	0,		8	} },
			{ ARM64_REG_S12,	{ VTIL_ARM64_VBASE( 12 ),	0,		4	} },
			{ ARM64_REG_H12,	{ VTIL_ARM64_VBASE( 12 ),	0,		2	} },
			{ ARM64_REG_B12,	{ VTIL_ARM64_VBASE( 12 ),	0,		1	} },

			VTIL_ARM64_VINST( 13 )
			{ ARM64_REG_Q13,	{ VTIL_ARM64_VBASE( 13 ),	0,		16	} },  // alias v13 = q13
			{ ARM64_REG_D13,	{ VTIL_ARM64_VBASE( 13 ),	0,		8	} },
			{ ARM64_REG_S13,	{ VTIL_ARM64_VBASE( 13 ),	0,		4	} },
			{ ARM64_REG_H13,	{ VTIL_ARM64_VBASE( 13 ),	0,		2	} },
			{ ARM64_REG_B13,	{ VTIL_ARM64_VBASE( 13 ),	0,		1	} },

			VTIL_ARM64_VINST( 14 )
			{ ARM64_REG_Q14,	{ VTIL_ARM64_VBASE( 14 ),	0,		16	} },  // alias v14 = q14
			{ ARM64_REG_D14,	{ VTIL_ARM64_VBASE( 14 ),	0,		8	} },
			{ ARM64_REG_S14,	{ VTIL_ARM64_VBASE( 14 ),	0,		4	} },
			{ ARM64_REG_H14,	{ VTIL_ARM64_VBASE( 14 ),	0,		2	} },
			{ ARM64_REG_B14,	{ VTIL_ARM64_VBASE( 14 ),	0,		1	} },

			VTIL_ARM64_VINST( 15 )
			{ ARM64_REG_Q15,	{ VTIL_ARM64_VBASE( 15 ),	0,		16	} },  // alias v15 = q15
			{ ARM64_REG_D15,	{ VTIL_ARM64_VBASE( 15 ),	0,		8	} },
			{ ARM64_REG_S15,	{ VTIL_ARM64_VBASE( 15 ),	0,		4	} },
			{ ARM64_REG_H15,	{ VTIL_ARM64_VBASE( 15 ),	0,		2	} },
			{ ARM64_REG_B15,	{ VTIL_ARM64_VBASE( 15 ),	0,		1	} },

			VTIL_ARM64_VINST( 16 )
			{ ARM64_REG_Q16,	{ VTIL_ARM64_VBASE( 16 ),	0,		16	} },  // alias v16 = q16
			{ ARM64_REG_D16,	{ VTIL_ARM64_VBASE( 16 ),	0,		8	} },
			{ ARM64_REG_S16,	{ VTIL_ARM64_VBASE( 16 ),	0,		4	} },
			{ ARM64_REG_H16,	{ VTIL_ARM64_VBASE( 16 ),	0,		2	} },
			{ ARM64_REG_B16,	{ VTIL_ARM64_VBASE( 16 ),	0,		1	} },

			VTIL_ARM64_VINST( 17 )
			{ ARM64_REG_Q17,	{ VTIL_ARM64_VBASE( 17 ),	0,		16	} },  // alias v17 = q17
			{ ARM64_REG_D17,	{ VTIL_ARM64_VBASE( 17 ),	0,		8	} },
			{ ARM64_REG_S17,	{ VTIL_ARM64_VBASE( 17 ),	0,		4	} },
			{ ARM64_REG_H17,	{ VTIL_ARM64_VBASE( 17 ),	0,		2	} },
			{ ARM64_REG_B17,	{ VTIL_ARM64_VBASE( 17 ),	0,		1	} },

			VTIL_ARM64_VINST( 18 )
			{ ARM64_REG_Q18,	{ VTIL_ARM64_VBASE( 18 ),	0,		16	} },  // alias v18 = q18
			{ ARM64_REG_D18,	{ VTIL_ARM64_VBASE( 18 ),	0,		8	} },
			{ ARM64_REG_S18,	{ VTIL_ARM64_VBASE( 18 ),	0,		4	} },
			{ ARM64_REG_H18,	{ VTIL_ARM64_VBASE( 18 ),	0,		2	} },
			{ ARM64_REG_B18,	{ VTIL_ARM64_VBASE( 18 ),	0,		1	} },

			VTIL_ARM64_VINST( 19 )
			{ ARM64_REG_Q19,	{ VTIL_ARM64_VBASE( 19 ),	0,		16	} },  // alias v19 = q19
			{ ARM64_REG_D19,	{ VTIL_ARM64_VBASE( 19 ),	0,		8	} },
			{ ARM64_REG_S19,	{ VTIL_ARM64_VBASE( 19 ),	0,		4	} },
			{ ARM64_REG_H19,	{ VTIL_ARM64_VBASE( 19 ),	0,		2	} },
			{ ARM64_REG_B19,	{ VTIL_ARM64_VBASE( 19 ),	0,		1	} },

			VTIL_ARM64_VINST( 20 )
			{ ARM64_REG_Q20,	{ VTIL_ARM64_VBASE( 20 ),	0,		16	} },  // alias v20 = q20
			{ ARM64_REG_D20,	{ VTIL_ARM64_VBASE( 20 ),	0,		8	} },
			{ ARM64_REG_S20,	{ VTIL_ARM64_VBASE( 20 ),	0,		4	} },
			{ ARM64_REG_H20,	{ VTIL_ARM64_VBASE( 20 ),	0,		2	} },
			{ ARM64_REG_B20,	{ VTIL_ARM64_VBASE( 20 ),	0,		1	} },

			VTIL_ARM64_VINST( 21 )
			{ ARM64_REG_Q21,	{ VTIL_ARM64_VBASE( 21 ),	0,		16	} },  // alias v21 = q21
			{ ARM64_REG_D21,	{ VTIL_ARM64_VBASE( 21 ),	0,		8	} },
			{ ARM64_REG_S21,	{ VTIL_ARM64_VBASE( 21 ),	0,		4	} },
			{ ARM64_REG_H21,	{ VTIL_ARM64_VBASE( 21 ),	0,		2	} },
			{ ARM64_REG_B21,	{ VTIL_ARM64_VBASE( 21 ),	0,		1	} },

			VTIL_ARM64_VINST( 22 )
			{ ARM64_REG_Q22,	{ VTIL_ARM64_VBASE( 22 ),	0,		16	} },  // alias v22 = q22
			{ ARM64_REG_D22,	{ VTIL_ARM64_VBASE( 22 ),	0,		8	} },
			{ ARM64_REG_S22,	{ VTIL_ARM64_VBASE( 22 ),	0,		4	} },
			{ ARM64_REG_H22,	{ VTIL_ARM64_VBASE( 22 ),	0,		2	} },
			{ ARM64_REG_B22,	{ VTIL_ARM64_VBASE( 22 ),	0,		1	} },

			VTIL_ARM64_VINST( 23 )
			{ ARM64_REG_Q23,	{ VTIL_ARM64_VBASE( 23 ),	0,		16	} },  // alias v23 = q23
			{ ARM64_REG_D23,	{ VTIL_ARM64_VBASE( 23 ),	0,		8	} },
			{ ARM64_REG_S23,	{ VTIL_ARM64_VBASE( 23 ),	0,		4	} },
			{ ARM64_REG_H23,	{ VTIL_ARM64_VBASE( 23 ),	0,		2	} },
			{ ARM64_REG_B23,	{ VTIL_ARM64_VBASE( 23 ),	0,		1	} },

			VTIL_ARM64_VINST( 24 )
			{ ARM64_REG_Q24,	{ VTIL_ARM64_VBASE( 24 ),	0,		16	} },  // alias v24 = q24
			{ ARM64_REG_D24,	{ VTIL_ARM64_VBASE( 24 ),	0,		8	} },
			{ ARM64_REG_S24,	{ VTIL_ARM64_VBASE( 24 ),	0,		4	} },
			{ ARM64_REG_H24,	{ VTIL_ARM64_VBASE( 24 ),	0,		2	} },
			{ ARM64_REG_B24,	{ VTIL_ARM64_VBASE( 24 ),	0,		1	} },

			VTIL_ARM64_VINST( 25 )
			{ ARM64_REG_Q25,	{ VTIL_ARM64_VBASE( 25 ),	0,		16	} },  // alias v25 = q25
			{ ARM64_REG_D25,	{ VTIL_ARM64_VBASE( 25 ),	0,		8	} },
			{ ARM64_REG_S25,	{ VTIL_ARM64_VBASE( 25 ),	0,		4	} },
			{ ARM64_REG_H25,	{ VTIL_ARM64_VBASE( 25 ),	0,		2	} },
			{ ARM64_REG_B25,	{ VTIL_ARM64_VBASE( 25 ),	0,		1	} },

			VTIL_ARM64_VINST( 26 )
			{ ARM64_REG_Q26,	{ VTIL_ARM64_VBASE( 26 ),	0,		16	} },  // alias v26 = q26
			{ ARM64_REG_D26,	{ VTIL_ARM64_VBASE( 26 ),	0,		8	} },
			{ ARM64_REG_S26,	{ VTIL_ARM64_VBASE( 26 ),	0,		4	} },
			{ ARM64_REG_H26,	{ VTIL_ARM64_VBASE( 26 ),	0,		2	} },
			{ ARM64_REG_B26,	{ VTIL_ARM64_VBASE( 26 ),	0,		1	} },

			VTIL_ARM64_VINST( 27 )
			{ ARM64_REG_Q27,	{ VTIL_ARM64_VBASE( 27 ),	0,		16	} },  // alias v27 = q27
			{ ARM64_REG_D27,	{ VTIL_ARM64_VBASE( 27 ),	0,		8	} },
			{ ARM64_REG_S27,	{ VTIL_ARM64_VBASE( 27 ),	0,		4	} },
			{ ARM64_REG_H27,	{ VTIL_ARM64_VBASE( 27 ),	0,		2	} },
			{ ARM64_REG_B27,	{ VTIL_ARM64_VBASE( 27 ),	0,		1	} },

			VTIL_ARM64_VINST( 28 )
			{ ARM64_REG_Q28,	{ VTIL_ARM64_VBASE( 28 ),	0,		16	} },  // alias v28 = q28
			{ ARM64_REG_D28,	{ VTIL_ARM64_VBASE( 28 ),	0,		8	} },
			{ ARM64_REG_S28,	{ VTIL_ARM64_VBASE( 28 ),	0,		4	} },
			{ ARM64_REG_H28,	{ VTIL_ARM64_VBASE( 28 ),	0,		2	} },
			{ ARM64_REG_B28,	{ VTIL_ARM64_VBASE( 28 ),	0,		1	} },

			VTIL_ARM64_VINST( 29 )
			{ ARM64_REG_Q29,	{ VTIL_ARM64_VBASE( 29 ),	0,		16	} },  // alias v29 = q29
			{ ARM64_REG_D29,	{ VTIL_ARM64_VBASE( 29 ),	0,		8	} },
			{ ARM64_REG_S29,	{ VTIL_ARM64_VBASE( 29 ),	0,		4	} },
			{ ARM64_REG_H29,	{ VTIL_ARM64_VBASE( 29 ),	0,		2	} },
			{ ARM64_REG_B29,	{ VTIL_ARM64_VBASE( 29 ),	0,		1	} },

			VTIL_ARM64_VINST( 30 )
			{ ARM64_REG_Q30,	{ VTIL_ARM64_VBASE( 30 ),	0,		16	} },  // alias v30 = q30
			{ ARM64_REG_D30,	{ VTIL_ARM64_VBASE( 30 ),	0,		8	} },
			{ ARM64_REG_S30,	{ VTIL_ARM64_VBASE( 30 ),	0,		4	} },
			{ ARM64_REG_H30,	{ VTIL_ARM64_VBASE( 30 ),	0,		2	} },
			{ ARM64_REG_B30,	{ VTIL_ARM64_VBASE( 30 ),	0,		1	} },

			VTIL_ARM64_VINST( 31 )
			{ ARM64_REG_Q31,	{ VTIL_ARM64_VBASE( 31 ),	0,		16	} },  // alias v31 = q31
			{ ARM64_REG_D31,	{ VTIL_ARM64_VBASE( 31 ),	0,		8	} },
			{ ARM64_REG_S31,	{ VTIL_ARM64_VBASE( 31 ),	0,		4	} },
			{ ARM64_REG_H31,	{ VTIL_ARM64_VBASE( 31 ),	0,		2	} },
			{ ARM64_REG_B31,	{ VTIL_ARM64_VBASE( 31 ),	0,		1	} }
		}
	};

	// Converts the enum into human-readable format.
	//
	static const char* name( uint32_t _reg ) { return cs_reg_name( get_cs_handle(), _reg ); }
}
