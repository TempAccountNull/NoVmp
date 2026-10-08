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
#pragma once
#include <cstdint>

#ifndef __has_builtin
	#define __has_builtin(x) 0
#endif

// Determine RTTI support.
//
#if defined(_CPPRTTI)
	#define HAS_RTTI	_CPPRTTI
#elif defined(__GXX_RTTI)
	#define HAS_RTTI	__GXX_RTTI
#elif defined(__has_feature)
	#define HAS_RTTI	__has_feature(cxx_rtti)
#else
	#define HAS_RTTI	0
#endif

// Determine bitcast support.
//
#if (defined(_MSC_VER) && _MSC_VER >= 1926)
	#define HAS_BIT_CAST 1
#else
	#define HAS_BIT_CAST __has_builtin(__builtin_bit_cast)
#endif

#include <cstdio>
#include <cstdlib>

#ifdef _MSC_VER
    #include <intrin.h>
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #if defined(__i386__) || defined(__x86_64__)
        #include <emmintrin.h>
    #endif
#endif

namespace vtil::detail
{
    [[noreturn]] inline void unreachable_handler( const char* file, int line, const char* func )
    {
        // Print to stderr works for console apps and piped output.
        //
        fprintf( stderr, "\n[FATAL] unreachable code reached at %s:%d in %s\n", file, line, func );
        fflush( stderr );

#ifdef _MSC_VER
        // emit to Windows debug output so it shows in
        // VS / windbg even for gui / service processes
        //
        char buf[ 512 ];
        snprintf( buf, sizeof( buf ),
                  "[FATAL] unreachable code reached at %s:%d in %s\n", file, line, func );
        OutputDebugStringA( buf );

        // Break into debugger if attached, otherwise just terminate
        //
        if ( IsDebuggerPresent() ) __debugbreak();
#endif
        // _Exit skips atexit handlers and CRT cleanup avoids the
        // abort() dialog on Windows and any cascading destruction issues.
        //
        _Exit( 3 );
    }
}

#ifdef _MSC_VER
    #define unreachable() vtil::detail::unreachable_handler( __FILE__, __LINE__, __FUNCSIG__ )
    #define FUNCTION_NAME __FUNCSIG__
#else
    #define unreachable() vtil::detail::unreachable_handler( __FILE__, __LINE__, __PRETTY_FUNCTION__ )
    #define __forceinline __attribute__((always_inline))
    #define _AddressOfReturnAddress() ((void*)__builtin_frame_address(0))
    #define FUNCTION_NAME __PRETTY_FUNCTION__

    // _mm_pause is x86-specific; provide a lightweight fallback on other targets.
    #if !defined(__i386__) && !defined(__x86_64__)
        __forceinline static void _mm_pause()
        {
            #if defined(__aarch64__) || defined(__arm__)
                __asm__ __volatile__("yield");
            #else
                __asm__ __volatile__("" ::: "memory");
            #endif
        }
    #endif

    // Declare _?mul128
    //
    __forceinline static uint64_t _umul128( uint64_t _Multiplier, uint64_t _Multiplicand, uint64_t* _HighProduct )
    {
#if defined(__x86_64__)
        uint64_t LowProduct;
        uint64_t HighProduct;

        __asm__( "mulq  %[b]"
                 :"=d"( HighProduct ), "=a"( LowProduct )
                 : "1"( _Multiplier ), [ b ]"rm"( _Multiplicand ) );

        *_HighProduct = HighProduct;
        return LowProduct;
#else
        unsigned __int128 product = (unsigned __int128) _Multiplier * (unsigned __int128) _Multiplicand;
        *_HighProduct = (uint64_t) ( product >> 64 );
        return (uint64_t) product;
#endif
    }

    __forceinline static int64_t _mul128( int64_t _Multiplier, int64_t _Multiplicand, int64_t* _HighProduct )
    {
#if defined(__x86_64__)
        int64_t LowProduct;
        int64_t HighProduct;

        __asm__( "imulq  %[b]"
                 :"=d"( HighProduct ), "=a"( LowProduct )
                 : "1"( _Multiplier ), [ b ]"rm"( _Multiplicand ) );

        *_HighProduct = HighProduct;
        return LowProduct;
#else
        __int128 product = (__int128) _Multiplier * (__int128) _Multiplicand;
        *_HighProduct = (int64_t) ( product >> 64 );
        return (int64_t) product;
#endif
    }

    // Declare _?mulh
    //
    __forceinline static int64_t __mulh( int64_t _Multiplier, int64_t _Multiplicand )
    {
        int64_t HighProduct;
        _mul128( _Multiplier, _Multiplicand, &HighProduct );
        return HighProduct;
    }

    __forceinline static uint64_t __umulh( uint64_t _Multiplier, uint64_t _Multiplicand )
    {
        uint64_t HighProduct;
        _umul128( _Multiplier, _Multiplicand, &HighProduct );
        return HighProduct;
    }
#endif