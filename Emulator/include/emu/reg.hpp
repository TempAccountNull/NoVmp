// emu::reg (plan 3.M2, ledger U1953; start plan section 2 M2, 1.D 4.1 "emu::reg wrapper" minimal): a register / flag dump
// of an engine or of a saved uc_context - the minimal set of the plan: GPRs, RIP, RFLAGS (bits decoded), segment selectors
// with the FS / GS bases and the descriptor-table registers, CR0 / CR4 / XCR0, the x87 state (FCW, FSW with TOP, abridged
// tag, FOP / FIP / FDP / FCS / FDS, the physical registers R0-R7), MXCSR, ZMM0-31 (XMMn / YMMn are their low 128 / 256 bits)
// and K0-7. Every value is read through the fork's own API (uc_reg_read2 / uc_context_reg_read2 with the exact buffer size,
// so the fork checks the size), never from host memory.
//
//   read( engine )      live registers                      read( context )   the values a uc_context_save captured
//   write_user_state    writes the user-writable subset (GPRs, RIP, RFLAGS, FS / GS base, FCW, FSW, R0-R7, MXCSR, ZMM,
//                       K); selectors, descriptor tables, control registers and the derived x87 tag / last-instruction
//                       pointers are machine state the environment sets (milestone M4), not written here
//   format / parse      "name = value" text lines, one per field, round-trip exact ("; ..." comments are ignored)
//   diff                names of the fields that differ
//
// Citations (Citation rule): RFLAGS bit positions 253665-093 Vol1 3.4.3 p. 3-16 Figure 3-8 "EFLAGS Register"; FSW layout
// 253665-093 Vol1 8.1.3 p. 8-4 Figure 8-4 "x87 FPU Status Word"; TOP (bits 13:11) and ST(i) = the i-th register from TOP:
// 253665-093 Vol1 8.1.2 p. 8-3 ("ST(i) to specify the ith register from TOP in the stack"); QEMU 11.1
// target/i386/tcg/fpu_helper.c:35 ST(n) = fpregs[(fpstt + n) & 7]. The dump holds the PHYSICAL registers R0-R7
// (UC_X86_REG_FP0-7) so it does not depend on TOP.
#pragma once

#include <unicorn/unicorn.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace emu::reg
{
	enum class kind { u16, u32, u64, fp80, mmr, vec512 };

	struct field
	{
		const char* name;
		int regid;          // UC_X86_REG_*
		kind k;
		bool writable;      // part of write_user_state
	};

	// the fields of a dump, in dump order
	const std::vector<field>& fields();

	// one value per field (little-endian bytes as the fork returns them; unused bytes 0)
	struct dump
	{
		std::vector<std::array<uint8_t, 64>> v;
	};

	size_t byte_size( kind k );

	bool read( uc_engine* uc, dump& out, std::string& error );
	bool read( uc_context* ctx, dump& out, std::string& error );
	bool write_user_state( uc_engine* uc, const dump& d, std::string& error );

	std::vector<std::string> format( const dump& d );
	bool parse( const std::vector<std::string>& lines, dump& out, std::string& error );
	std::vector<std::string> diff( const dump& a, const dump& b );

	// typed access by field name (false: no such field / wrong kind)
	bool get_u64( const dump& d, const char* name, uint64_t& out );
	bool set_u64( dump& d, const char* name, uint64_t value );
	bool get_bytes( const dump& d, const char* name, uint8_t* out, size_t n );
	bool set_bytes( dump& d, const char* name, const uint8_t* in, size_t n );

	// RFLAGS / FSW decoded ("CF=1 PF=0 ..."), for the verbose log
	std::string decode_rflags( uint64_t rflags );
	std::string decode_fsw( uint16_t fsw );
}
