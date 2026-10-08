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
#include <string>
#include <functional>
#include <algorithm>
#include "bitwise.hpp"
#include "../util/intrinsics.hpp"
#include "../io/logger.hpp"

namespace vtil::math
{
    enum class operator_id : uint8_t
    {
        invalid,        // = <Invalid>

        // ------------------ Bitwise Operators ------------------ //

        // Bitwise modifiers:
        //
        bitwise_not,    // ~RHS

        // Basic bitwise operations:
        //
        bitwise_and,    // LHS&(RHS&...)
        bitwise_or,     // LHS|(RHS|...)
        bitwise_xor,    // LHS^(RHS^...)

        // Distributing bitwise operations:
        //
        shift_right,    // LHS>>(RHS+...)
        shift_left,     // LHS<<(RHS+...)
        rotate_right,   // LHS>](RHS+...)
        rotate_left,    // LHS[<(RHS+...)

        // ---------------- Arithmetic Operators ----------------- //

        // Arithmetic modifiers:
        //
        negate,         // -RHS

        // Basic arithmetic operations:
        //
        add,            // LHS+(RHS+...)
        subtract,       // LHS-(RHS+...)

        // Distributing arithmetic operations:
        //
        multiply_high,  // HI(LHS*RHS)
        multiply,       // LHS*(RHS*...)
        divide,         // LHS/(RHS*...)
        remainder,      // LHS%RHS

        umultiply_high, // < Unsigned variants of above >
        umultiply,      // 
        udivide,        // 
        uremainder,     // 

        // ----------------- Special Operators ----------------- //
        ucast,          // uintRHS_t(LHS)
        cast,           // intRHS_t(LHS)
        popcnt,         // POPCNT(RHS)
        bitscan_fwd,    // BitScanForward(RHS)
        bitscan_rev,    // BitScanReverse(RHS)
        bit_test,       // [LHS>>RHS]&1
        mask,           // RHS.mask()
        bit_count,      // RHS.bitcount()
        value_if,       // LHS&1 ? RHS : 0

        max_value,      // LHS>=RHS ? LHS : RHS
        min_value,      // LHS<=RHS ? LHS : RHS

        umax_value,     // < Unsigned variants of above >
        umin_value,     //

        greater,        // LHS > RHS
        greater_eq,     // LHS >= RHS
        equal,          // LHS == RHS
        not_equal,      // LHS != RHS
        less_eq,        // LHS <= RHS
        less,           // LHS < RHS
                  
        ugreater,       // < Unsigned variants of above > [Note: equal and not_equal are always unsigned.]
        ugreater_eq,    //
        uequal,         //
        unot_equal,     //
        uless_eq,       //
        uless,          //
    max,
    };

    // Basic properties of each operator.
    //
    struct operator_desc
    {
        // >0 if bitwise operations are preferred as operands, <0 if arithmetic, ==0 if neutral.
        //
        int hint_bitwise;

        // Whether it expects signed operands or not.
        //
        bool is_signed;

        // Number of operands it takes. Either 1 or 2.
        //
        size_t operand_count;

        // Whether the operation is commutative or not.
        //
        bool is_commutative;

        // Symbol of the operation.
        //
        const char* symbol;

        // Name of the function associated with the operation.
        //
        const char* function_name;

        // Coefficient of the expression complexity, will be multiplied with an additional x2 
        // in case bitwise/aritmethic mismatch is hit within child expressions.
        //
        double complexity_coeff;

        // Creates a string representation based on the operands passed.
        //
        std::string to_string( const std::string& lhs, const std::string& rhs ) const
        {
            // If unary function:
            //
            if ( operand_count == 1 )
            {
                // If it has a symbol, use it, else return in function format.
                //
                if ( symbol ) return symbol + rhs;
                else          return format::str( "%s(%s)", function_name, rhs );
            }
            // If binary function:
            //
            else if ( operand_count == 2 )
            {
                // If it has a symbol, use it, else return in function format.
                //
                if ( symbol ) return format::str( "(%s%s%s)", lhs, symbol, rhs );
                else          return format::str( "%s(%s, %s)", function_name, lhs, rhs );
            }
            unreachable();
        }
    };
    static constexpr operator_desc descriptors[] = 
    {
        // Skipping ::invalid.
        {},

        /*  [Bitwise] [Signed]  [#Op] [Commutative]   [Symbol]    [Name]         [Cost] */
        {   +1,       false,    1,    false,          "~",        "not",         1      },
        {   +1,       false,    2,    true,           "&",        "and",         1      },
        {   +1,       false,    2,    true,           "|",        "or",          1      },
        {   +1,       false,    2,    true,           "^",        "xor",         1      },
        {   +1,       false,    2,    false,          ">>",       "shr",         1.5    },
        {   +1,       false,    2,    false,          "<<",       "shl",         1.5    },
        {   +1,       false,    2,    false,          ">]",       "rotr",        0.5    },
        {   +1,       false,    2,    false,          "[<",       "rotl",        0.5    },
        {   -1,       true,     1,    false,          "-",        "neg",         1      },
        {   -1,       true,     2,    true,           "+",        "add",         1      },
        {   -1,       true,     2,    false,          "-",        "sub",         1      },
        {   -1,       true,     2,    true,           "h*",       "mulhi",       1.3    },
        {   -1,       true,     2,    true,           "*",        "mul",         1.3    },
        {   -1,       true,     2,    false,          "/",        "div",         1.3    },
        {   -1,       true,     2,    false,          "%",        "rem",         1.3    },
        {   -1,       false,    2,    true,           "uh*",      "umulhi",      1.3    },
        {   -1,       false,    2,    true,           "u*",       "umul",        1.3    },
        {   -1,       false,    2,    false,          "u/",       "udiv",        1.3    },
        {   -1,       false,    2,    false,          "u%",       "urem",        1.3    },
        {    0,       false,    2,    false,          nullptr,    "__ucast",     1      },
        {   -1,       true,     2,    false,          nullptr,    "__cast",      1      },
        {   +1,       false,    1,    false,          nullptr,    "__popcnt",    1      },
        {   +1,       false,    1,    false,          nullptr,    "__bsf",       1      },
        {   +1,       false,    1,    false,          nullptr,    "__bsr",       1      },
        {   +1,       false,    2,    false,          nullptr,    "__bt",        1      },
        {   +1,       false,    1,    false,          nullptr,    "__mask",      1      },
        {    0,       false,    1,    false,          nullptr,    "__bcnt",      1      },
        {    0,       false,    2,    false,          "?",        "if",          1      },
        {    0,       false,    2,    true,           nullptr,    "max",         1      },
        {    0,       false,    2,    true,           nullptr,    "min",         1      },
        {    0,       true,     2,    true,           nullptr,    "umax",        1      },
        {    0,       true,     2,    true,           nullptr,    "umin",        1      },
        {   -1,       true,     2,    false,          ">",        "greater",     1      },
        {   -1,       true,     2,    false,          ">=",       "greater_eq",  1.2    },
        {    0,       false,    2,    true,           "==",       "equal",       1      },
        {    0,       false,    2,    true,           "!=",       "not_equal",   1      },
        {   -1,       true,     2,    false,          "<=",       "less_eq",     1.2    },
        {   -1,       true,     2,    false,          "<",        "less",        1      },
        {   +1,       false,    2,    false,          "u>",       "ugreater",    1      },
        {   +1,       false,    2,    false,          "u>=",      "ugreater_eq", 1.2    },
        {    0,       false,    2,    true,           "u==",      "uequal",      1      },
        {    0,       false,    2,    true,           "u!=",      "unot_equal",  1      },
        {   +1,       false,    2,    false,          "u<=",      "uless_eq",    1.2    },
        {   +1,       false,    2,    false,          "u<",       "uless",       1      },
    };
    static_assert( std::size( descriptors ) == size_t( operator_id::max ), "Operator descriptor table is invalid." );
    static constexpr const operator_desc& descriptor_of( operator_id id ) 
    { 
        dassert( operator_id::invalid < id && id < operator_id::max );
        return descriptors[ ( size_t ) id ]; 
    }

    // Operators that return bit-indices, always use the following size.
    //
    static constexpr bitcnt_t bit_index_size = 8;

    // Calculates the size of the result after after the application of the operator [id] on the operands.
    //
    static constexpr bitcnt_t result_size( operator_id id, bitcnt_t bcnt_lhs, bitcnt_t bcnt_rhs )
    {
        switch ( id )
        {
            // - Operators that work with bit-indices.
            //
            case operator_id::popcnt:
            case operator_id::bitscan_fwd:
            case operator_id::bitscan_rev:
            case operator_id::bit_count:      return bit_index_size;

            // - Unary and parameterized unary-like operators.
            //
            case operator_id::negate:
            case operator_id::bitwise_not:
            case operator_id::mask:
            case operator_id::value_if:       return bcnt_rhs;
            case operator_id::shift_right:
            case operator_id::shift_left:
            case operator_id::rotate_right:
            case operator_id::rotate_left:    return bcnt_lhs;

            // - Boolean operators.           
            //                                
            case operator_id::bit_test:
            case operator_id::greater:
            case operator_id::greater_eq:
            case operator_id::equal:
            case operator_id::not_equal:
            case operator_id::less_eq:
            case operator_id::less:
            case operator_id::ugreater:
            case operator_id::ugreater_eq:
            case operator_id::uless_eq:
            case operator_id::uless:          return 1;

            // - Resizing operators should not call into this helper.
            //
            case operator_id::cast:
            case operator_id::ucast:          unreachable();

            // - Rest default to maximum operand size.
            //
            default:                          return std::max( bcnt_lhs, bcnt_rhs );
        }
    }

    // Applies the specified operator [id] on left hand side [lhs] and right hand side [rhs]
    // and returns the output as a masked unsigned 64-bit integer <0> and the final size <1>.
    //
    static constexpr std::pair<uint64_t, bitcnt_t> evaluate( operator_id id, bitcnt_t bcnt_lhs, uint64_t lhs, bitcnt_t bcnt_rhs, uint64_t rhs )
    {
        using namespace logger;

        // Normalize the input.
        //
        const operator_desc& desc = descriptor_of( id );
        if ( bcnt_lhs != arch::bit_count && desc.operand_count != 1 )
            lhs = desc.is_signed ? sign_extend( lhs, bcnt_lhs ) : zero_extend( lhs, bcnt_lhs );
        if ( bcnt_rhs != arch::bit_count )
            rhs = desc.is_signed ? sign_extend( rhs, bcnt_rhs ) : zero_extend( rhs, bcnt_rhs );

        // Create aliases for signed values to avoid ugly casts.
        //
        int64_t& ilhs = ( int64_t& ) lhs;
        int64_t& irhs = ( int64_t& ) rhs;
        
        // Handle __cast and __ucast.
        //
        if ( id == operator_id::ucast )
            return { zero_extend( lhs, narrow_cast<bitcnt_t>( rhs ) ), narrow_cast<bitcnt_t>( rhs ) };
        if ( id == operator_id::cast )
            return { sign_extend( lhs, narrow_cast<bitcnt_t>( rhs ) ), narrow_cast<bitcnt_t>( rhs ) };

        // Calculate the result of the operation.
        //
        uint64_t result = 0;
        bitcnt_t bcnt_res = result_size( id, bcnt_lhs, bcnt_rhs );
        switch ( id )
        {
            // - Bitwise operators.
            //
            case operator_id::bitwise_not:      result = ~rhs;                                                      break;
            case operator_id::bitwise_and:      result = lhs & rhs;                                                 break;
            case operator_id::bitwise_or:       result = lhs | rhs;                                                 break;
            case operator_id::bitwise_xor:      result = lhs ^ rhs;                                                 break;
            case operator_id::shift_right:      result = rhs >= bcnt_lhs ? 0 : lhs >> rhs;                          break;
            case operator_id::shift_left:       result = rhs >= bcnt_lhs ? 0 : lhs << rhs;                          break;
            case operator_id::rotate_right:     result = ( lhs >> ( rhs % bcnt_lhs ) )
                                                       | ( lhs << ( bcnt_lhs - ( rhs % bcnt_lhs ) ) );              break;
            case operator_id::rotate_left:      result = ( lhs << ( rhs % bcnt_lhs ) )
                                                       | ( lhs >> ( bcnt_lhs - ( rhs % bcnt_lhs ) ) );              break;
            // - Arithmetic operators.                                                          
            //                                                                                  
            case operator_id::negate:           result = -irhs;                                                     break;
            case operator_id::add:              result = ilhs + irhs;                                               break;
            case operator_id::subtract:         result = ilhs - irhs;                                               break;

            // if bcnt_res == 64, use __mulh, otherwise using >> bcnt_res(for example: 32)
            case operator_id::multiply_high:    result = bcnt_res == 64
                                                        ? mulh64( ilhs, irhs )
                                                        : uint64_t( ilhs * irhs ) >> bcnt_res;                      break;
            case operator_id::umultiply_high:   result = bcnt_res == 64
                                                        ? umulh64( lhs, rhs )
                                                        : ( lhs * rhs ) >> bcnt_res;                                break;
            case operator_id::multiply:         result = ilhs * irhs;                                               break;
            case operator_id::umultiply:        result = lhs * rhs;                                                 break;

            case operator_id::divide:           if( irhs == 0 ) result = INT64_MAX, warning("Division by immediate zero (IDIV).");
                                                else            result = ilhs / irhs;                               break;
            case operator_id::udivide:          if( rhs == 0 )  result = UINT64_MAX, warning("Division by immediate zero (DIV).");
                                                else            result = lhs / rhs;                                 break;
            case operator_id::remainder:        if( irhs == 0 ) result = 0, warning("Division by immediate zero (IREM).");
                                                else            result = ilhs % irhs;                               break;
            case operator_id::uremainder:       if( rhs == 0 )  result = 0, warning("Division by immediate zero (REM).");
                                                else            result = lhs % rhs;                                 break;
            // - Special operators.                                                          
            //                                                                                  
            case operator_id::popcnt:           result = popcnt( rhs );                                             break;
            case operator_id::bitscan_fwd:      result = lsb( rhs );                                                break;
            case operator_id::bitscan_rev:      result = msb( rhs );                                                break;
            case operator_id::bit_test:         result = ( lhs >> rhs ) & 1;                                        break;
            case operator_id::mask:             result = fill( bcnt_rhs );                                          break;
            case operator_id::bit_count:        result = bcnt_rhs;                                                  break;
            case operator_id::value_if:         result = ( lhs & 1 ) ? rhs : 0;                                     break;

            // - MinMax operators
            //
            case operator_id::umin_value:       result = std::min( lhs, rhs );                                      break;
            case operator_id::umax_value:       result = std::max( lhs, rhs );                                      break;
            case operator_id::min_value:        result = std::min( ilhs, irhs );                                    break;
            case operator_id::max_value:        result = std::max( ilhs, irhs );                                    break;

            // - Comparison operators
            //
            case operator_id::greater:          result = ilhs > irhs;                                               break;
            case operator_id::greater_eq:       result = ilhs >= irhs;                                              break;
            case operator_id::equal:            result = ilhs == irhs;                                              break;
            case operator_id::not_equal:        result = ilhs != irhs;                                              break;
            case operator_id::uequal:           result = lhs == rhs;                                                break;
            case operator_id::unot_equal:       result = lhs != rhs;                                                break;
            case operator_id::less_eq:          result = ilhs <= irhs;                                              break;
            case operator_id::less:             result = ilhs < irhs;                                               break;
            case operator_id::ugreater:         result = lhs > rhs;                                                 break;
            case operator_id::ugreater_eq:      result = lhs >= rhs;                                                break;
            case operator_id::uless_eq:         result = lhs <= rhs;                                                break;
            case operator_id::uless:            result = lhs < rhs;                                                 break;
            default:                            unreachable();
        }

        // Mask and return.
        //
        return { result & fill( bcnt_res ), bcnt_res };
    }

};

// evaluate_partial is split into operators_partial.hpp for build-time reasons.
//
#include "operators_partial.hpp"
