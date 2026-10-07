// emu-alltest instruction universe (plan step 1.9.2): every x86-64 form Capstone decodes, found by
// sweeping the opcode space - legacy maps 1/0F/0F38/0F3A x {none,66,F2,F3} x {none,REX.W,REX.B},
// VEX maps 1-3 x W x L x pp x vvvv, EVEX maps 1/2/3/5/6 x W x L'L x pp x vvvv x masking/zeroing x
// broadcast - with every register ModRM plus [rsi] and SIB [rsi+rcx] memory forms, deduplicated by
// mnemonic + operand-class signature. Ported from the old difftest (emu_extentions/tests).
#pragma once
#include <capstone/capstone.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "at_engine.hpp"

namespace at
{
	struct form
	{
		std::vector<uint8_t> bytes;
		std::string text, key, cls, groups;   // cls: legacy / vex / evex / x87
		unsigned id = 0;
		uint64_t flag_mask = 0;                // RFLAGS bits the SDM defines
		bool hw_safe = true;                   // may run natively
		bool privileged = false;
		std::string skip_reason;
		int kind = 0;                          // 0 exact, 1 approximate float (RCP/RSQRT), 2 model-defined
		int shift_op = 0, op_bits = 0, count_kind = 0;
		uint8_t imm = 0;
		uint16_t fsw_mask = 0xFFFF;            // x87: C0/C2/C3 are undefined after most instructions
	};

	inline bool has_group( const cs_insn& i, uint8_t g )
	{
		for ( int k = 0; k < i.detail->groups_count; k++ ) if ( i.detail->groups[ k ] == g ) return true;
		return false;
	}

	class universe
	{
	public:
		universe()
		{
			cs_open( CS_ARCH_X86, CS_MODE_64, &cs_ );
			cs_option( cs_, CS_OPT_DETAIL, CS_OPT_ON );
		}
		~universe() { if ( cs_ ) cs_close( &cs_ ); }

		std::vector<form> forms;

		bool consider( const uint8_t* buf, size_t len, const char* cls )
		{
			cs_insn* insn = nullptr;
			size_t n = cs_disasm( cs_, buf, len, CODE, 1, &insn );
			if ( n == 0 ) return false;
			const cs_insn& i = insn[ 0 ];
			auto& x = i.detail->x86;

			std::string sig;
			for ( int k = 0; k < x.op_count; k++ )
			{
				auto& op = x.operands[ k ];
				if ( op.type == X86_OP_REG )
				{
					const char* rn = cs_reg_name( cs_, op.reg );
					std::string c = rn ? rn : "?";
					if ( c.rfind( "xmm", 0 ) == 0 ) c = "xmm"; else if ( c.rfind( "ymm", 0 ) == 0 ) c = "ymm";
					else if ( c.rfind( "zmm", 0 ) == 0 ) c = "zmm"; else if ( c[ 0 ] == 'k' && c.size() <= 2 ) c = "k";
					else if ( c.rfind( "st", 0 ) == 0 ) c = "st"; else if ( c.rfind( "mm", 0 ) == 0 ) c = "mm";
					else if ( c == "cs" || c == "ds" || c == "es" || c == "fs" || c == "gs" || c == "ss" ) c = "sreg";
					else if ( c.rfind( "cr", 0 ) == 0 ) c = "cr"; else if ( c.rfind( "dr", 0 ) == 0 ) c = "dr";
					else if ( c.rfind( "bnd", 0 ) == 0 ) c = "bnd"; else if ( c.rfind( "tmm", 0 ) == 0 ) c = "tmm";
					else c = "g" + std::to_string( op.size * 8 );
					sig += c + ",";
				}
				else if ( op.type == X86_OP_IMM ) sig += "i" + std::to_string( op.size * 8 ) + ",";
				else sig += "m" + std::to_string( op.size * 8 ) + ",";
			}
			bool is_evex = cls[ 0 ] == 'e' && i.size >= 4;
			std::string key = std::string( cls ) + ":" + i.mnemonic + ":" + sig +
				( is_evex && ( buf[ 3 ] & 7 ) ? "k" : "" ) + ( is_evex && ( buf[ 3 ] & 0x80 ) ? "z" : "" ) + ( is_evex && ( buf[ 3 ] & 0x10 ) ? "b" : "" );
			if ( seen_.count( key ) ) { cs_free( insn, n ); return false; }
			seen_.insert( key );

			form f;
			f.bytes.assign( buf, buf + i.size );
			f.text = std::string( i.mnemonic ) + ( i.op_str[ 0 ] ? " " : "" ) + i.op_str;
			f.key = key;
			bool x87 = ( buf[ 0 ] >= 0xD8 && buf[ 0 ] <= 0xDF ) || ( i.size > 1 && buf[ 0 ] >= 0x40 && buf[ 0 ] <= 0x4F && buf[ 1 ] >= 0xD8 && buf[ 1 ] <= 0xDF ) ||
				( i.size > 1 && ( buf[ 0 ] == 0x66 || buf[ 0 ] == 0xF2 || buf[ 0 ] == 0xF3 ) && buf[ 1 ] >= 0xD8 && buf[ 1 ] <= 0xDF );
			f.cls = x87 ? "x87" : cls;
			f.id = i.id;
			for ( int k = 0; k < i.detail->groups_count; k++ )
			{
				const char* g = cs_group_name( cs_, i.detail->groups[ k ] );
				if ( !g ) continue;
				std::string gs = g;
				if ( gs == "mode64" || gs == "mode32" || gs == "not64bitmode" || gs == "jump" || gs == "call" || gs == "ret" ||
					 gs == "int" || gs == "iret" || gs == "branch_relative" || gs == "privilege" ) continue;
				f.groups += ( f.groups.empty() ? "" : "+" ) + gs;
			}
			if ( f.groups.empty() ) f.groups = x87 ? "fpu" : "base";

			uint64_t mask = 0x8D5 | 0x400;   // CF PF AF ZF SF OF + DF
			if ( !x87 )
			{
				uint64_t ef = x.eflags;
				if ( ef & X86_EFLAGS_UNDEFINED_CF ) mask &= ~0x001ull;
				if ( ef & X86_EFLAGS_UNDEFINED_PF ) mask &= ~0x004ull;
				if ( ef & X86_EFLAGS_UNDEFINED_AF ) mask &= ~0x010ull;
				if ( ef & X86_EFLAGS_UNDEFINED_ZF ) mask &= ~0x040ull;
				if ( ef & X86_EFLAGS_UNDEFINED_SF ) mask &= ~0x080ull;
				if ( ef & X86_EFLAGS_UNDEFINED_OF ) mask &= ~0x800ull;
			}
			f.flag_mask = mask;

			if ( x87 )
			{
				// SDM: C0/C2/C3 are undefined after most x87 instructions; compares, FTST, FXAM and
				// FPREM define them, and FSIN/FCOS/FPTAN/FSINCOS define C2.
				const std::string mn = i.mnemonic;
				auto starts = [ & ]( const char* p ) { return mn.rfind( p, 0 ) == 0; };
				if ( starts( "fcom" ) || starts( "fucom" ) || starts( "ficom" ) || mn == "ftst" || mn == "fxam" || starts( "fprem" ) ||
					 mn == "fnstsw" || mn == "fstsw" || mn == "fnclex" || mn == "fclex" || mn == "fninit" || mn == "finit" || mn == "fldenv" ||
					 mn == "frstor" || mn == "fxrstor" || mn == "fxrstor64" || mn == "fnsave" || mn == "fsave" )
					f.fsw_mask = 0xFFFF;
				else if ( mn == "fsin" || mn == "fcos" || mn == "fptan" || mn == "fsincos" )
					f.fsw_mask = uint16_t( ~0x4100 );
				else
					f.fsw_mask = uint16_t( ~0x4500 );
			}

			switch ( i.id )
			{
				case X86_INS_SHL: case X86_INS_SAL: case X86_INS_SHR: f.shift_op = 1; break;
				case X86_INS_SAR: f.shift_op = 2; break;
				case X86_INS_ROL: case X86_INS_ROR: f.shift_op = 3; break;
				case X86_INS_RCL: case X86_INS_RCR: f.shift_op = 4; break;
				case X86_INS_SHLD: case X86_INS_SHRD: f.shift_op = 5; break;
				default: break;
			}
			if ( f.shift_op && x.op_count >= 1 )
			{
				f.op_bits = x.operands[ 0 ].size * 8;
				auto& last = x.operands[ x.op_count - 1 ];
				if ( x.op_count >= 2 && last.type == X86_OP_IMM ) { f.count_kind = 1; f.imm = uint8_t( last.imm ); }
				else if ( x.op_count >= 2 && last.type == X86_OP_REG && last.reg == X86_REG_CL ) f.count_kind = 2;
			}

			// ── native-execution safety ──
			auto bad = [ & ]( const char* why ) { f.hw_safe = false; if ( f.skip_reason.empty() ) f.skip_reason = why; };
			// privileged / IOPL-gated: the host (CPL3) faults, raw Unicorn runs at CPL0 - compared at
			// CPL3 once the emulator layer exists (Phase 2). Capstone's "privilege" group misses some.
			f.privileged = has_group( i, X86_GRP_PRIVILEGE ) || has_group( i, X86_GRP_VM );
			switch ( i.id )
			{
				case X86_INS_CLTS: case X86_INS_IN: case X86_INS_OUT: case X86_INS_INSB: case X86_INS_INSW: case X86_INS_INSD:
				case X86_INS_OUTSB: case X86_INS_OUTSW: case X86_INS_OUTSD: case X86_INS_HLT: case X86_INS_CLI: case X86_INS_STI:
				case X86_INS_LGDT: case X86_INS_LIDT: case X86_INS_LLDT: case X86_INS_LTR: case X86_INS_LMSW: case X86_INS_INVD: case X86_INS_WBNOINVD:
				case X86_INS_WBINVD: case X86_INS_INVLPG: case X86_INS_INVPCID: case X86_INS_RDMSR: case X86_INS_WRMSR:
				case X86_INS_SWAPGS: case X86_INS_MONITOR: case X86_INS_MWAIT: case X86_INS_XSETBV: case X86_INS_RDPMC:
				case X86_INS_CLAC: case X86_INS_STAC: case X86_INS_XRSTORS: case X86_INS_XRSTORS64: case X86_INS_XSAVES: case X86_INS_XSAVES64:
					f.privileged = true; break;
				default: break;
			}
			for ( int k = 0; k < x.op_count; k++ )
				if ( x.operands[ k ].type == X86_OP_REG )
				{
					unsigned r = x.operands[ k ].reg;
					if ( ( r >= X86_REG_CR0 && r <= X86_REG_CR15 ) || ( r >= X86_REG_DR0 && r <= X86_REG_DR15 ) ) f.privileged = true;
				}
			if ( has_group( i, X86_GRP_JUMP ) || has_group( i, X86_GRP_CALL ) || has_group( i, X86_GRP_RET ) ||
				 has_group( i, X86_GRP_INT ) || has_group( i, X86_GRP_IRET ) || has_group( i, X86_GRP_BRANCH_RELATIVE ) )
				bad( "control transfer" );
			switch ( i.id )
			{
				case X86_INS_SYSCALL: case X86_INS_SYSENTER: case X86_INS_SYSEXIT: case X86_INS_SYSRET:
				case X86_INS_INT: case X86_INS_INT1: case X86_INS_INT3: case X86_INS_INTO:
				case X86_INS_XBEGIN: case X86_INS_WRFSBASE: case X86_INS_WRGSBASE:
				case X86_INS_LFS: case X86_INS_LGS: case X86_INS_LSS:
				case X86_INS_PUSH: case X86_INS_POP: case X86_INS_PUSHF: case X86_INS_PUSHFQ: case X86_INS_POPF:
				case X86_INS_POPFQ: case X86_INS_ENTER: case X86_INS_LEAVE:
					bad( "control/stack/segment" ); break;
				default: break;
			}
			cs_regs rr, ww; uint8_t nr = 0, nw = 0;
			if ( cs_regs_access( cs_, &i, rr, &nr, ww, &nw ) == CS_ERR_OK )
				for ( int k = 0; k < nw; k++ )
				{
					unsigned r = ww[ k ];
					if ( r == X86_REG_RSP || r == X86_REG_ESP || r == X86_REG_SP || r == X86_REG_SPL || r == X86_REG_RIP ) bad( "writes rsp/rip" );
					if ( r == X86_REG_CS || r == X86_REG_SS || r == X86_REG_DS || r == X86_REG_ES || r == X86_REG_FS || r == X86_REG_GS ) bad( "writes a segment register" );
				}
			for ( int k = 0; k < x.op_count; k++ )
			{
				if ( x.operands[ k ].type != X86_OP_MEM ) continue;
				auto& m = x.operands[ k ].mem;
				if ( m.base == X86_REG_RIP ) { bad( "rip-relative" ); continue; }
				// only our operand pointers may address memory natively, so a random register value
				// can never reach the test's own process memory
				bool ok_base = m.base == X86_REG_RSI || m.base == X86_REG_R14 || m.base == X86_REG_RDI ||
							   m.base == X86_REG_ESI || m.base == X86_REG_R14D || m.base == X86_REG_EDI || m.base == X86_REG_INVALID;
				if ( !ok_base ) bad( "memory operand based on a random register" );
				if ( m.base == X86_REG_INVALID && m.index == X86_REG_INVALID ) bad( "absolute memory operand" );
			}

			switch ( i.id )
			{
				case X86_INS_RCPPS: case X86_INS_RCPSS: case X86_INS_RSQRTPS: case X86_INS_RSQRTSS:
				case X86_INS_VRCPPS: case X86_INS_VRCPSS: case X86_INS_VRSQRTPS: case X86_INS_VRSQRTSS:
					f.kind = 1; break;
				case X86_INS_CPUID: case X86_INS_RDTSC: case X86_INS_RDTSCP: case X86_INS_RDRAND: case X86_INS_RDSEED:
				case X86_INS_RDPID: case X86_INS_XGETBV: case X86_INS_SGDT: case X86_INS_SIDT: case X86_INS_SLDT:
				case X86_INS_STR: case X86_INS_SMSW: case X86_INS_LSL: case X86_INS_LAR: case X86_INS_VERR:
				case X86_INS_VERW: case X86_INS_RDPMC: case X86_INS_XSAVE: case X86_INS_XSAVE64: case X86_INS_XSAVEC:
				case X86_INS_XSAVEC64: case X86_INS_XSAVEOPT: case X86_INS_XSAVEOPT64: case X86_INS_XSAVES:
				case X86_INS_XSAVES64: case X86_INS_FXSAVE: case X86_INS_FXSAVE64: case X86_INS_RDSSPD: case X86_INS_RDSSPQ:
				case X86_INS_RDPKRU: case X86_INS_FNSAVE: case X86_INS_FNSTENV: case X86_INS_RDFSBASE: case X86_INS_RDGSBASE:
					f.kind = 2; break;
				default: break;
			}
			// reading a segment selector: the value is defined by the environment (Windows uses
			// 0x2B/0x33/0x53...), not by the instruction - compare the outcome only
			for ( int k = 1; k < x.op_count; k++ )
				if ( x.operands[ k ].type == X86_OP_REG )
				{
					unsigned r = x.operands[ k ].reg;
					if ( r == X86_REG_CS || r == X86_REG_DS || r == X86_REG_ES || r == X86_REG_FS || r == X86_REG_GS || r == X86_REG_SS ) f.kind = 2;
				}
			forms.push_back( std::move( f ) );
			cs_free( insn, n );
			return true;
		}

		void sweep()
		{
			uint8_t b[ 32 ];
			const uint8_t tail[ 8 ] = { 0x5B, 0, 0, 0, 0, 0, 0, 0 };
			std::vector<uint8_t> modrms, evex_modrms;
			for ( int reg = 0; reg < 8; reg++ )
			{
				for ( int rm = 0; rm < 8; rm++ ) modrms.push_back( uint8_t( 0xC0 | ( reg << 3 ) | rm ) );
				modrms.push_back( uint8_t( 0x06 | ( reg << 3 ) ) );       // [rsi]
				modrms.push_back( uint8_t( 0x04 | ( reg << 3 ) ) );       // SIB [rsi+rcx*1]
				// EVEX: the reg field selects the opcode extension; the rm register never changes the
				// operand-class signature, so one register form per /reg suffices
				evex_modrms.push_back( uint8_t( 0xC0 | ( reg << 3 ) | 2 ) );
				evex_modrms.push_back( uint8_t( 0x06 | ( reg << 3 ) ) );
				evex_modrms.push_back( uint8_t( 0x04 | ( reg << 3 ) ) );
			}
			auto emit = [ & ]( const std::vector<uint8_t>& pre, uint8_t op, uint8_t modrm, const char* cls ) {
				size_t n = 0;
				for ( auto p : pre ) b[ n++ ] = p;
				b[ n++ ] = op; b[ n++ ] = modrm;
				if ( ( modrm >> 6 ) != 3 && ( modrm & 7 ) == 4 ) b[ n++ ] = 0x0E;
				std::memcpy( b + n, tail, 8 ); n += 8;
				consider( b, n, cls );
			};
			const std::vector<std::vector<uint8_t>> prefixes = { {}, { 0x66 }, { 0xF2 }, { 0xF3 } };
			const std::vector<std::vector<uint8_t>> rexes = { {}, { 0x48 }, { 0x41 } };
			const std::vector<std::vector<uint8_t>> maps = { {}, { 0x0F }, { 0x0F, 0x38 }, { 0x0F, 0x3A } };
			for ( auto& p : prefixes ) for ( auto& r : rexes ) for ( auto& m : maps )
				for ( int op = 0; op < 256; op++ )
				{
					if ( m.empty() && ( op == 0x0F || ( op >= 0x40 && op <= 0x4F ) || op == 0x66 || op == 0x67 || op == 0xF2 || op == 0xF3 ||
										op == 0xF0 || op == 0x2E || op == 0x3E || op == 0x26 || op == 0x36 || op == 0x64 || op == 0x65 ||
										op == 0xC4 || op == 0xC5 || op == 0x62 || op == 0x8F || op == 0xD5 ) ) continue;
					for ( auto mr : modrms )
					{
						std::vector<uint8_t> pre = p; pre.insert( pre.end(), r.begin(), r.end() ); pre.insert( pre.end(), m.begin(), m.end() );
						emit( pre, uint8_t( op ), mr, "legacy" );
					}
				}
			for ( int map = 1; map <= 3; map++ ) for ( int W = 0; W < 2; W++ ) for ( int vv : { 0xE, 0xF } )
				for ( int L = 0; L < 2; L++ ) for ( int pp = 0; pp < 4; pp++ ) for ( int op = 0; op < 256; op++ )
					for ( auto mr : modrms )
						emit( { 0xC4, uint8_t( 0xE0 | map ), uint8_t( ( W << 7 ) | ( vv << 3 ) | ( L << 2 ) | pp ) }, uint8_t( op ), mr, "vex" );
			for ( int map : { 1, 2, 3, 5, 6 } ) for ( int W = 0; W < 2; W++ ) for ( int LL = 0; LL < 3; LL++ )
				for ( int pp = 0; pp < 4; pp++ ) for ( int vv : { 0xE, 0xF } ) for ( int mz = 0; mz < 3; mz++ )
					for ( int bb = 0; bb < 2; bb++ ) for ( int op = 0; op < 256; op++ )
						for ( auto mr : evex_modrms )
						{
							int aaa = mz ? 1 : 0, z = mz == 2 ? 1 : 0;
							emit( { 0x62, uint8_t( 0xF0 | map ), uint8_t( ( W << 7 ) | ( vv << 3 ) | 0x04 | pp ),
									uint8_t( ( z << 7 ) | ( LL << 5 ) | ( bb << 4 ) | 0x08 | aaa ) }, uint8_t( op ), mr, "evex" );
						}
		}

		// cache: one line per form, "cls hexbytes" - re-decoding is fast, sweeping is not
		bool load_cache( const std::string& path )
		{
			std::ifstream in( path );
			if ( !in ) return false;
			std::string cls, hex;
			while ( in >> cls >> hex )
			{
				std::vector<uint8_t> b;
				for ( size_t k = 0; k + 1 < hex.size(); k += 2 ) b.push_back( uint8_t( std::stoi( hex.substr( k, 2 ), nullptr, 16 ) ) );
				b.insert( b.end(), { 0x5B, 0, 0, 0, 0, 0, 0, 0 } );
				consider( b.data(), b.size(), cls == "x87" ? "legacy" : cls.c_str() );
			}
			return !forms.empty();
		}
		void save_cache( const std::string& path ) const
		{
			std::ofstream out( path );
			for ( auto& f : forms )
			{
				out << ( f.cls == "x87" ? "legacy" : f.cls ) << ' ';
				for ( uint8_t c : f.bytes ) { char h[ 3 ]; std::snprintf( h, sizeof( h ), "%02X", c ); out << h; }
				out << '\n';
			}
		}

	private:
		csh cs_ = 0;
		std::set<std::string> seen_;
	};
}
