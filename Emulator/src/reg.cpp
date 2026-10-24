// emu::reg (plan 3.M2, ledger U1953): see include/emu/reg.hpp.
#include <emu/reg.hpp>

#include <cstdio>
#include <cstring>

namespace emu::reg
{
	namespace
	{
		std::vector<field> make_fields()
		{
			std::vector<field> f;
			// GPRs in architectural register-number order (ModRM / REX encoding 0-15)
			const struct { const char* n; int id; } g[] = {
				{ "rax", UC_X86_REG_RAX }, { "rcx", UC_X86_REG_RCX }, { "rdx", UC_X86_REG_RDX }, { "rbx", UC_X86_REG_RBX },
				{ "rsp", UC_X86_REG_RSP }, { "rbp", UC_X86_REG_RBP }, { "rsi", UC_X86_REG_RSI }, { "rdi", UC_X86_REG_RDI },
				{ "r8", UC_X86_REG_R8 }, { "r9", UC_X86_REG_R9 }, { "r10", UC_X86_REG_R10 }, { "r11", UC_X86_REG_R11 },
				{ "r12", UC_X86_REG_R12 }, { "r13", UC_X86_REG_R13 }, { "r14", UC_X86_REG_R14 }, { "r15", UC_X86_REG_R15 } };
			for ( auto& r : g ) f.push_back( { r.n, r.id, kind::u64, true } );
			f.push_back( { "rip", UC_X86_REG_RIP, kind::u64, true } );
			f.push_back( { "rflags", UC_X86_REG_RFLAGS, kind::u64, true } );
			// segment selectors in the Sreg encoding order ES CS SS DS FS GS
			f.push_back( { "es", UC_X86_REG_ES, kind::u16, false } );
			f.push_back( { "cs", UC_X86_REG_CS, kind::u16, false } );
			f.push_back( { "ss", UC_X86_REG_SS, kind::u16, false } );
			f.push_back( { "ds", UC_X86_REG_DS, kind::u16, false } );
			f.push_back( { "fs", UC_X86_REG_FS, kind::u16, false } );
			f.push_back( { "gs", UC_X86_REG_GS, kind::u16, false } );
			f.push_back( { "fs_base", UC_X86_REG_FS_BASE, kind::u64, true } );
			f.push_back( { "gs_base", UC_X86_REG_GS_BASE, kind::u64, true } );
			f.push_back( { "gdtr", UC_X86_REG_GDTR, kind::mmr, false } );
			f.push_back( { "idtr", UC_X86_REG_IDTR, kind::mmr, false } );
			f.push_back( { "ldtr", UC_X86_REG_LDTR, kind::mmr, false } );
			f.push_back( { "tr", UC_X86_REG_TR, kind::mmr, false } );
			f.push_back( { "cr0", UC_X86_REG_CR0, kind::u64, false } );
			f.push_back( { "cr4", UC_X86_REG_CR4, kind::u64, false } );
			f.push_back( { "xcr0", UC_X86_REG_XCR0, kind::u64, false } );
			f.push_back( { "fpcw", UC_X86_REG_FPCW, kind::u16, true } );
			f.push_back( { "fpsw", UC_X86_REG_FPSW, kind::u16, true } );
			f.push_back( { "fptag", UC_X86_REG_FPTAG, kind::u16, false } );   // derived from R0-R7 (abridged on write)
			f.push_back( { "fop", UC_X86_REG_FOP, kind::u16, false } );
			f.push_back( { "fip", UC_X86_REG_FIP, kind::u64, false } );
			f.push_back( { "fcs", UC_X86_REG_FCS, kind::u16, false } );
			f.push_back( { "fdp", UC_X86_REG_FDP, kind::u64, false } );
			f.push_back( { "fds", UC_X86_REG_FDS, kind::u16, false } );
			static const char* const fp[ 8 ] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7" };   // x87 physical registers
			for ( int i = 0; i < 8; i++ ) f.push_back( { fp[ i ], UC_X86_REG_FP0 + i, kind::fp80, true } );
			f.push_back( { "mxcsr", UC_X86_REG_MXCSR, kind::u32, true } );
			static const char* const zmm[ 32 ] = { "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7", "zmm8", "zmm9", "zmm10",
												   "zmm11", "zmm12", "zmm13", "zmm14", "zmm15", "zmm16", "zmm17", "zmm18", "zmm19", "zmm20",
												   "zmm21", "zmm22", "zmm23", "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29", "zmm30",
												   "zmm31" };
			for ( int i = 0; i < 32; i++ ) f.push_back( { zmm[ i ], UC_X86_REG_ZMM0 + i, kind::vec512, true } );
			static const char* const k[ 8 ] = { "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7" };
			for ( int i = 0; i < 8; i++ ) f.push_back( { k[ i ], UC_X86_REG_K0 + i, kind::u64, true } );
			return f;
		}

		const field* find( const char* name, size_t* index = nullptr )
		{
			const auto& f = fields();
			for ( size_t i = 0; i < f.size(); i++ )
				if ( std::strcmp( f[ i ].name, name ) == 0 )
				{
					if ( index ) *index = i;
					return &f[ i ];
				}
			return nullptr;
		}

		uint64_t le( const uint8_t* p, size_t n )
		{
			uint64_t v = 0;
			for ( size_t i = 0; i < n && i < 8; i++ ) v |= uint64_t( p[ i ] ) << ( 8 * i );
			return v;
		}

		void put_le( uint8_t* p, size_t n, uint64_t v )
		{
			for ( size_t i = 0; i < n && i < 8; i++ ) p[ i ] = uint8_t( v >> ( 8 * i ) );
		}

		std::string hex( uint64_t v, int digits )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%0*llx", digits, (unsigned long long)v );
			return b;
		}

		// "0x..." with at most 16 digits (exactly as format writes it is not required: any 0x-hex up to the width)
		bool parse_hex( const std::string& t, uint64_t& out, uint64_t max )
		{
			if ( t.size() < 3 || t[ 0 ] != '0' || ( t[ 1 ] != 'x' && t[ 1 ] != 'X' ) || t.size() > 18 ) return false;
			uint64_t v = 0;
			for ( size_t i = 2; i < t.size(); i++ )
			{
				const char c = t[ i ];
				int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
				if ( d < 0 ) return false;
				v = ( v << 4 ) | uint64_t( d );
			}
			if ( v > max ) return false;
			out = v;
			return true;
		}

		std::vector<std::string> words( const std::string& s )
		{
			std::vector<std::string> w;
			size_t i = 0;
			while ( i < s.size() )
			{
				while ( i < s.size() && s[ i ] == ' ' ) i++;
				size_t j = i;
				while ( j < s.size() && s[ j ] != ' ' ) j++;
				if ( j > i ) w.push_back( s.substr( i, j - i ) );
				i = j;
			}
			return w;
		}

		template <class Reader>
		bool read_all( Reader rd, dump& out, std::string& error )
		{
			const auto& f = fields();
			out.v.assign( f.size(), {} );
			for ( size_t i = 0; i < f.size(); i++ )
			{
				uint8_t buf[ 64 ] = {};
				size_t size = sizeof( buf );
				const uc_err e = rd( f[ i ].regid, buf, &size );
				if ( e != UC_ERR_OK )
				{
					error = std::string( "reading " ) + f[ i ].name + ": " + uc_strerror( e );
					return false;
				}
				if ( size != byte_size( f[ i ].k ) )
				{
					// the fork's CHECK_REG_TYPE sets *size to the register's size; an unchanged size = not handled
					error = std::string( "reading " ) + f[ i ].name + ": the fork reported " + std::to_string( size ) + " bytes, want " +
							std::to_string( byte_size( f[ i ].k ) );
					return false;
				}
				std::memcpy( out.v[ i ].data(), buf, size );
			}
			return true;
		}
	}

	const std::vector<field>& fields()
	{
		static const std::vector<field> f = make_fields();
		return f;
	}

	size_t byte_size( kind k )
	{
		switch ( k )
		{
		case kind::u16: return 2;
		case kind::u32: return 4;
		case kind::u64: return 8;
		case kind::fp80: return 10;
		case kind::mmr: return sizeof( uc_x86_mmr );
		default: return 64;
		}
	}

	bool read( uc_engine* uc, dump& out, std::string& error )
	{
		if ( !uc )
		{
			error = "no engine";
			return false;
		}
		return read_all( [ uc ]( int id, void* v, size_t* s ) { return uc_reg_read2( uc, id, v, s ); }, out, error );
	}

	bool read( uc_context* ctx, dump& out, std::string& error )
	{
		if ( !ctx )
		{
			error = "no context";
			return false;
		}
		return read_all( [ ctx ]( int id, void* v, size_t* s ) { return uc_context_reg_read2( ctx, id, v, s ); }, out, error );
	}

	bool write_user_state( uc_engine* uc, const dump& d, std::string& error )
	{
		const auto& f = fields();
		if ( !uc || d.v.size() != f.size() )
		{
			error = "no engine or not a dump of this layout";
			return false;
		}
		for ( size_t i = 0; i < f.size(); i++ )
		{
			if ( !f[ i ].writable ) continue;
			size_t size = byte_size( f[ i ].k );
			const uc_err e = uc_reg_write2( uc, f[ i ].regid, d.v[ i ].data(), &size );
			if ( e != UC_ERR_OK )
			{
				error = std::string( "writing " ) + f[ i ].name + ": " + uc_strerror( e );
				return false;
			}
		}
		return true;
	}

	std::string decode_rflags( uint64_t r )
	{
		// 253665-093 Vol1 3.4.3 p. 3-16 Figure 3-8: CF 0, PF 2, AF 4, ZF 6, SF 7, TF 8, IF 9, DF 10, OF 11, IOPL 13:12,
		// NT 14, RF 16, VM 17, AC 18, VIF 19, VIP 20, ID 21; bit 1 reserved (1)
		const struct { const char* n; int b; } bits[] = { { "CF", 0 }, { "PF", 2 }, { "AF", 4 }, { "ZF", 6 }, { "SF", 7 }, { "TF", 8 },
														   { "IF", 9 }, { "DF", 10 }, { "OF", 11 }, { "NT", 14 }, { "RF", 16 }, { "VM", 17 },
														   { "AC", 18 }, { "VIF", 19 }, { "VIP", 20 }, { "ID", 21 } };
		std::string s;
		for ( auto& b : bits )
		{
			s += b.n;
			s += ( r >> b.b ) & 1 ? "=1 " : "=0 ";
			if ( b.b == 11 ) s += "IOPL=" + std::to_string( ( r >> 12 ) & 3 ) + " ";
		}
		s += "bit1=" + std::to_string( ( r >> 1 ) & 1 );
		return s;
	}

	std::string decode_fsw( uint16_t w )
	{
		// 253665-093 Vol1 8.1.3 p. 8-4 Figure 8-4 "x87 FPU Status Word": IE 0, DE 1, ZE 2, OE 3, UE 4, PE 5, SF 6, ES 7, C0 8, C1 9, C2 10,
		// TOP 13:11, C3 14, B 15
		const struct { const char* n; int b; } bits[] = { { "IE", 0 }, { "DE", 1 }, { "ZE", 2 }, { "OE", 3 }, { "UE", 4 }, { "PE", 5 },
														   { "SF", 6 }, { "ES", 7 }, { "C0", 8 }, { "C1", 9 }, { "C2", 10 }, { "C3", 14 }, { "B", 15 } };
		std::string s = "TOP=" + std::to_string( ( w >> 11 ) & 7 );
		for ( auto& b : bits ) s += std::string( " " ) + b.n + ( ( w >> b.b ) & 1 ? "=1" : "=0" );
		return s;
	}

	std::vector<std::string> format( const dump& d )
	{
		const auto& f = fields();
		std::vector<std::string> out;
		if ( d.v.size() != f.size() ) return out;
		for ( size_t i = 0; i < f.size(); i++ )
		{
			const uint8_t* p = d.v[ i ].data();
			std::string l = std::string( f[ i ].name ) + " = ";
			switch ( f[ i ].k )
			{
			case kind::u16: l += hex( le( p, 2 ), 4 ); break;
			case kind::u32: l += hex( le( p, 4 ), 8 ); break;
			case kind::u64: l += hex( le( p, 8 ), 16 ); break;
			case kind::fp80: l += hex( le( p, 8 ), 16 ) + " " + hex( le( p + 8, 2 ), 4 ); break;   // significand, sign + exponent
			case kind::mmr:
			{
				uc_x86_mmr m;
				std::memcpy( &m, p, sizeof( m ) );
				l += hex( m.selector, 4 ) + " " + hex( m.base, 16 ) + " " + hex( m.limit, 8 ) + " " + hex( m.flags, 8 );   // selector base limit flags
				break;
			}
			default:
				for ( int q = 0; q < 8; q++ ) l += ( q ? " " : "" ) + hex( le( p + 8 * q, 8 ), 16 );   // qword 0 (bits 63:0) first
				break;
			}
			if ( std::strcmp( f[ i ].name, "rflags" ) == 0 ) l += " ; " + decode_rflags( le( p, 8 ) );
			if ( std::strcmp( f[ i ].name, "fpsw" ) == 0 ) l += " ; " + decode_fsw( uint16_t( le( p, 2 ) ) );
			out.push_back( l );
		}
		return out;
	}

	bool parse( const std::vector<std::string>& lines, dump& out, std::string& error )
	{
		const auto& f = fields();
		out.v.assign( f.size(), {} );
		std::vector<bool> got( f.size(), false );
		size_t n = 0;
		for ( const auto& raw : lines )
		{
			n++;
			std::string l = raw;
			const size_t sc = l.find( ';' );
			if ( sc != std::string::npos ) l.resize( sc );
			const auto w = words( l );
			if ( w.empty() ) continue;
			const std::string where = "line " + std::to_string( n ) + ": ";
			size_t idx = 0;
			const field* fd = w.size() >= 3 && w[ 1 ] == "=" ? find( w[ 0 ].c_str(), &idx ) : nullptr;
			if ( !fd )
			{
				error = where + "want \"name = value\" with a dump field name";
				return false;
			}
			if ( got[ idx ] )
			{
				error = where + "field " + w[ 0 ] + " twice";
				return false;
			}
			const size_t want = fd->k == kind::fp80 ? 2 : fd->k == kind::mmr ? 4 : fd->k == kind::vec512 ? 8 : 1;
			if ( w.size() != 2 + want )
			{
				error = where + w[ 0 ] + " wants " + std::to_string( want ) + " value(s)";
				return false;
			}
			uint8_t* p = out.v[ idx ].data();
			uint64_t v[ 8 ] = {};
			const uint64_t lim = fd->k == kind::u16 ? 0xFFFF : fd->k == kind::u32 ? 0xFFFFFFFF : ~0ull;
			for ( size_t k = 0; k < want; k++ )
			{
				uint64_t m = lim;
				if ( fd->k == kind::fp80 && k == 1 ) m = 0xFFFF;
				if ( fd->k == kind::mmr ) m = k == 0 ? 0xFFFF : k == 1 ? ~0ull : 0xFFFFFFFF;
				if ( !parse_hex( w[ 2 + k ], v[ k ], m ) )
				{
					error = where + w[ 0 ] + ": value " + std::to_string( k + 1 ) + " is not 0x-hex in range";
					return false;
				}
			}
			switch ( fd->k )
			{
			case kind::u16: put_le( p, 2, v[ 0 ] ); break;
			case kind::u32: put_le( p, 4, v[ 0 ] ); break;
			case kind::u64: put_le( p, 8, v[ 0 ] ); break;
			case kind::fp80: put_le( p, 8, v[ 0 ] ); put_le( p + 8, 2, v[ 1 ] ); break;
			case kind::mmr:
			{
				uc_x86_mmr m{};
				m.selector = uint16_t( v[ 0 ] );
				m.base = v[ 1 ];
				m.limit = uint32_t( v[ 2 ] );
				m.flags = uint32_t( v[ 3 ] );
				std::memcpy( p, &m, sizeof( m ) );
				break;
			}
			default:
				for ( int q = 0; q < 8; q++ ) put_le( p + 8 * q, 8, v[ q ] );
				break;
			}
			got[ idx ] = true;
		}
		for ( size_t i = 0; i < f.size(); i++ )
			if ( !got[ i ] )
			{
				error = std::string( "field " ) + f[ i ].name + " missing";
				return false;
			}
		return true;
	}

	std::vector<std::string> diff( const dump& a, const dump& b )
	{
		const auto& f = fields();
		std::vector<std::string> out;
		if ( a.v.size() != f.size() || b.v.size() != f.size() )
		{
			out.push_back( "(layout)" );
			return out;
		}
		for ( size_t i = 0; i < f.size(); i++ )
			if ( std::memcmp( a.v[ i ].data(), b.v[ i ].data(), byte_size( f[ i ].k ) ) != 0 ) out.push_back( f[ i ].name );
		return out;
	}

	bool get_u64( const dump& d, const char* name, uint64_t& out )
	{
		size_t i = 0;
		const field* fd = find( name, &i );
		if ( !fd || i >= d.v.size() || ( fd->k != kind::u16 && fd->k != kind::u32 && fd->k != kind::u64 ) ) return false;
		out = le( d.v[ i ].data(), byte_size( fd->k ) );
		return true;
	}

	bool set_u64( dump& d, const char* name, uint64_t value )
	{
		size_t i = 0;
		const field* fd = find( name, &i );
		if ( !fd || i >= d.v.size() || ( fd->k != kind::u16 && fd->k != kind::u32 && fd->k != kind::u64 ) ) return false;
		put_le( d.v[ i ].data(), byte_size( fd->k ), value );
		return true;
	}

	bool get_bytes( const dump& d, const char* name, uint8_t* out, size_t n )
	{
		size_t i = 0;
		const field* fd = find( name, &i );
		if ( !fd || i >= d.v.size() || n != byte_size( fd->k ) ) return false;
		std::memcpy( out, d.v[ i ].data(), n );
		return true;
	}

	bool set_bytes( dump& d, const char* name, const uint8_t* in, size_t n )
	{
		size_t i = 0;
		const field* fd = find( name, &i );
		if ( !fd || i >= d.v.size() || n != byte_size( fd->k ) ) return false;
		std::memcpy( d.v[ i ].data(), in, n );
		return true;
	}
}
