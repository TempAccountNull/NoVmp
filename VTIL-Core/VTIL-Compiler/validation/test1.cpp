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
#include <sstream>
#include "test1.hpp"
#include "test1.vtil.hpp"
#include "pass_validation.hpp"


namespace vtil::optimizer::validation
{
	std::unique_ptr<routine> test1::generate() const
	{
		static const routine* cache = [ ] ()
		{
			routine* rtn;
			std::istringstream iss( std::string{ std::begin( serialized_routine ), std::end( serialized_routine ) } );
			deserialize( iss, rtn );
			return rtn;
		}();
		return cache->clone();
	}

	bool test1::validate( const routine* rtn ) const
	{
		/**/ std::vector<observable_action> log;

		uint64_t r, b;
		/**/ std::array args = make_random_n<uint64_t, 2>();
		/**/ r = args[ 0 ]; b = args[ 1 ];

		if ( b & 1 )
		{
			static volatile uint64_t _a = 2;
			volatile uint64_t x = r + b;
			x *= _a;
			/**/ log.emplace_back( memory_read{ .address = 0x3038, .fake_value = 2, .size = 64 } );
			x -= 42;
			x &= ~0b1;
			x <<= 1;
			x ^= 1;
			r = ( x << 3 );
		}

		for ( int i = 0x1111 & b; i < 8; i++ )
			r ^= ( b + i ) & ( i * 0x1b );

		/**/ // printf( "kekw: %d, %d\n", r, b );
		/**/ log.emplace_back( external_call { .address = 0x1010, .parameters = { 0x2230, r, b } } );

		/**/ //return r * b;
		/**/ log.emplace_back( vm_exit{ .register_state = { { operand( X86_REG_RAX ).reg(), r * b } } } );
		/**/ return verify_symbolic( rtn, { args.begin(), args.end() }, log );
	}
};