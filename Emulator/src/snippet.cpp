// emu::snippet (plan 3.M3, ledger U1974): see include/emu/snippet.hpp.
//
// Citations (Citation rule: single-volume 093 PDFs, text through the project's PyMuPDF extracts):
//   XSAVE area layout - 253665-093 Vol1 13.4 p. 13-7: "The legacy region of an XSAVE area comprises the 512 bytes starting at
//   the area's base address", "The XSAVE header ... comprises the 64 bytes starting at an offset of 512 bytes"; 13.4.3 p. 13-9
//   standard format: "Each state component i (i>= 2) is located at the byte offset from the base address of the XSAVE area
//   enumerated in CPUID.0DH.i:EBX" (the loader reads that CPUID value in the engine before it builds the image).
//   XRSTOR64 = REX.W 0F AE /5, FXRSTOR64 = REX.W 0F AE /1, IRETQ = REX.W CF (253666-093 Vol2A / 253667-093 Vol2B opcode tables).
#include <emu/assemble.hpp>
#include <emu/options.hpp>
#include <emu/snippet.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <set>

namespace emu::snippet
{
	namespace
	{
		const char* const gpr_names[ 16 ] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
		const int gpr_ids[ 16 ] = { UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RBX, UC_X86_REG_RSP, UC_X86_REG_RBP,
									UC_X86_REG_RSI, UC_X86_REG_RDI, UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11,
									UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15 };
		const int dr_index[ 6 ] = { 0, 1, 2, 3, 6, 7 };

		std::string hex( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}
		std::string hexbytes( const std::vector<uint8_t>& b, size_t n = SIZE_MAX )
		{
			static const char* d = "0123456789abcdef";
			std::string s;
			for ( size_t i = 0; i < b.size() && i < n; i++ ) { s += d[ b[ i ] >> 4 ]; s += d[ b[ i ] & 15 ]; }
			return s;
		}
		uint64_t le( const std::vector<uint8_t>& b )
		{
			uint64_t v = 0;
			for ( size_t i = 0; i < b.size() && i < 8; i++ ) v |= uint64_t( b[ i ] ) << ( 8 * i );
			return v;
		}
		std::vector<uint8_t> le_bytes( uint64_t v, size_t n )
		{
			std::vector<uint8_t> b( n );
			for ( size_t i = 0; i < n && i < 8; i++ ) b[ i ] = uint8_t( v >> ( 8 * i ) );
			return b;
		}
		std::string lower( std::string s )
		{
			for ( auto& c : s )
				if ( c >= 'A' && c <= 'Z' ) c = char( c - 'A' + 'a' );
			return s;
		}
		int hexd( char c )
		{
			if ( c >= '0' && c <= '9' ) return c - '0';
			if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
			if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
			return -1;
		}
		bool parse_hexbytes( const std::string& t, size_t max, std::vector<uint8_t>& out, std::string& error )
		{
			out.clear();
			if ( t.empty() || t.size() % 2 ) { error = "hex bytes need an even, non-zero number of digits"; return false; }
			if ( t.size() / 2 > max ) { error = "more than " + std::to_string( max ) + " bytes"; return false; }
			for ( size_t i = 0; i < t.size(); i += 2 )
			{
				const int a = hexd( t[ i ] ), b = hexd( t[ i + 1 ] );
				if ( a < 0 || b < 0 ) { error = "not a hex digit in \"" + t + "\""; return false; }
				out.push_back( uint8_t( a * 16 + b ) );
			}
			return true;
		}

		// ---------------------------------------------------------------------------------------------- the field table
		enum class fkind { scalar, bytes };
		struct fdef
		{
			std::string name;
			size_t size;
			fkind k;
		};

		const std::vector<fdef>& all_fields()
		{
			static const std::vector<fdef> t = [] {
				std::vector<fdef> v;
				for ( auto n : gpr_names ) v.push_back( { n, 8, fkind::scalar } );
				for ( int i = 16; i < 32; i++ ) v.push_back( { "r" + std::to_string( i ), 8, fkind::scalar } );
				v.push_back( { "rip", 8, fkind::scalar } );
				v.push_back( { "rflags", 8, fkind::scalar } );
				for ( auto n : { "cs", "ss", "ds", "es", "fs", "gs" } ) v.push_back( { n, 2, fkind::scalar } );
				v.push_back( { "fs_base", 8, fkind::scalar } );
				v.push_back( { "gs_base", 8, fkind::scalar } );
				v.push_back( { "gdtr_base", 8, fkind::scalar } );
				v.push_back( { "gdtr_limit", 4, fkind::scalar } );
				v.push_back( { "cr0", 8, fkind::scalar } );
				v.push_back( { "cr4", 8, fkind::scalar } );
				v.push_back( { "xcr0", 8, fkind::scalar } );
				v.push_back( { "fpcw", 2, fkind::scalar } );
				v.push_back( { "fpsw", 2, fkind::scalar } );
				v.push_back( { "ftw", 1, fkind::scalar } );
				v.push_back( { "fptag", 2, fkind::scalar } );
				v.push_back( { "fop", 2, fkind::scalar } );
				v.push_back( { "fip", 8, fkind::scalar } );
				v.push_back( { "fdp", 8, fkind::scalar } );
				v.push_back( { "fcs", 2, fkind::scalar } );
				v.push_back( { "fds", 2, fkind::scalar } );
				for ( int i = 0; i < 8; i++ ) v.push_back( { "st" + std::to_string( i ), 10, fkind::bytes } );
				v.push_back( { "mxcsr", 4, fkind::scalar } );
				for ( int i = 0; i < 32; i++ ) v.push_back( { "zmm" + std::to_string( i ), 64, fkind::bytes } );
				for ( int i = 0; i < 8; i++ ) v.push_back( { "k" + std::to_string( i ), 8, fkind::scalar } );
				for ( int i : dr_index ) v.push_back( { "dr" + std::to_string( i ), 8, fkind::scalar } );
				return v;
			}();
			return t;
		}
		const fdef* find_field( const std::string& n )
		{
			for ( auto& f : all_fields() )
				if ( f.name == n ) return &f;
			return nullptr;
		}
		bool is_apx_name( const std::string& n )
		{
			return n.size() == 3 && n[ 0 ] == 'r' && n[ 1 ] >= '1' && n[ 1 ] <= '3' && std::atoi( n.c_str() + 1 ) >= 16 && std::atoi( n.c_str() + 1 ) <= 31;
		}

		// the fields emu-alltest's "=>" lines check strictly (at_cases.hpp strict_care), in snippet names
		std::vector<std::string> strict_set( bool apx )
		{
			std::vector<std::string> v( gpr_names, gpr_names + 16 );
			if ( apx )
				for ( int i = 16; i < 32; i++ ) v.push_back( "r" + std::to_string( i ) );
			for ( auto n : { "rflags", "fpcw", "fpsw", "ftw", "mxcsr" } ) v.push_back( n );
			for ( int i = 0; i < 8; i++ ) v.push_back( "st" + std::to_string( i ) );
			for ( int i = 0; i < 32; i++ ) v.push_back( "zmm" + std::to_string( i ) );
			for ( int i = 0; i < 8; i++ ) v.push_back( "k" + std::to_string( i ) );
			return v;
		}

		// canonical name and the byte range an alias covers: xmmN / ymmN = the low 16 / 32 bytes of zmmN
		bool canonical( const std::string& raw, std::string& name, size_t& covered, std::string& error )
		{
			const std::string n = lower( raw );
			for ( auto [ pre, cov ] : std::initializer_list<std::pair<const char*, size_t>>{ { "xmm", 16 }, { "ymm", 32 }, { "zmm", 64 } } )
			{
				const size_t pl = std::strlen( pre );
				if ( n.size() > pl && n.compare( 0, pl, pre ) == 0 && n.find_first_not_of( "0123456789", pl ) == std::string::npos && n.size() <= pl + 2 )
				{
					const int i = std::atoi( n.c_str() + pl );
					if ( i > 31 ) { error = "register " + raw + " does not exist (0..31)"; return false; }
					name = "zmm" + std::to_string( i );
					covered = cov;
					return true;
				}
			}
			const fdef* f = find_field( n );
			if ( !f ) { error = "unknown register \"" + raw + "\""; return false; }
			name = n;
			covered = f->size;
			return true;
		}

		bool value_into( const fdef& f, size_t covered, const std::string& text, field_value& out, std::string& error )
		{
			out.bytes.assign( f.size, 0 );
			out.mask.assign( f.size, 0 );
			if ( f.k == fkind::scalar )
			{
				uint64_t v = 0;
				if ( !parse_u64( text, v ) ) { error = "\"" + text + "\" is not a number (decimal or 0x hex, 64-bit)"; return false; }
				if ( f.size < 8 && ( v >> ( 8 * f.size ) ) ) { error = text + " does not fit " + f.name + " (" + std::to_string( f.size ) + " bytes)"; return false; }
				out.bytes = le_bytes( v, f.size );
				std::fill( out.mask.begin(), out.mask.end(), uint8_t( 0xFF ) );
				return true;
			}
			// st: SEXP:MANT (hex) as in the emu-alltest case files, or hex bytes
			const size_t colon = text.find( ':' );
			if ( f.name.rfind( "st", 0 ) == 0 && colon != std::string::npos )
			{
				uint64_t se = 0, m = 0;
				if ( !parse_u64( "0x" + text.substr( 0, colon ), se ) || se > 0xFFFF || !parse_u64( "0x" + text.substr( colon + 1 ), m ) )
				{
					error = f.name + " wants SEXP:MANT (hex) or 10 hex bytes";
					return false;
				}
				auto mb = le_bytes( m, 8 ), sb = le_bytes( se, 2 );
				std::copy( mb.begin(), mb.end(), out.bytes.begin() );
				std::copy( sb.begin(), sb.end(), out.bytes.begin() + 8 );
				std::fill( out.mask.begin(), out.mask.end(), uint8_t( 0xFF ) );
				return true;
			}
			std::vector<uint8_t> b;
			if ( !parse_hexbytes( text, covered, b, error ) ) { error = f.name + ": " + error; return false; }
			std::copy( b.begin(), b.end(), out.bytes.begin() );
			std::fill( out.mask.begin(), out.mask.begin() + std::ptrdiff_t( b.size() ), uint8_t( 0xFF ) );
			return true;
		}

		void merge( std::vector<uint8_t>& dst, const field_value& v )
		{
			for ( size_t i = 0; i < dst.size() && i < v.bytes.size(); i++ )
			{
				const uint8_t m = v.mask.empty() ? 0xFF : v.mask[ i ];
				dst[ i ] = uint8_t( ( dst[ i ] & ~m ) | ( v.bytes[ i ] & m ) );
			}
		}

		// ---------------------------------------------------------------------------------------------- engine access
		bool read_fields( uc_engine* uc, bool apx, std::map<std::string, std::vector<uint8_t>>& out, std::string& error )
		{
			out.clear();
			reg::dump d;
			if ( !reg::read( uc, d, error ) ) return false;
			for ( auto& f : all_fields() )
			{
				if ( is_apx_name( f.name ) || f.name.rfind( "st", 0 ) == 0 || f.name.rfind( "dr", 0 ) == 0 || f.name == "ftw" || f.name.rfind( "gdtr", 0 ) == 0 )
					continue;
				std::vector<uint8_t> b( f.size );
				if ( !reg::get_bytes( d, f.name.c_str(), b.data(), b.size() ) ) { error = "emu::reg has no field " + f.name; return false; }
				out[ f.name ] = b;
			}
			uc_x86_mmr g{};
			if ( !reg::get_bytes( d, "gdtr", reinterpret_cast<uint8_t*>( &g ), sizeof( g ) ) ) { error = "emu::reg has no field gdtr"; return false; }
			out[ "gdtr_base" ] = le_bytes( g.base, 8 );
			out[ "gdtr_limit" ] = le_bytes( g.limit, 4 );
			const uint64_t tag = le( out[ "fptag" ] );
			uint8_t ftw = 0;
			for ( int i = 0; i < 8; i++ )
				if ( ( ( tag >> ( 2 * i ) ) & 3 ) != 3 ) ftw |= uint8_t( 1u << i );
			out[ "ftw" ] = { ftw };
			auto rd = [ & ]( int id, size_t n, const std::string& name ) {
				std::vector<uint8_t> b( 16 );
				size_t sz = b.size();
				const uc_err e = uc_reg_read2( uc, id, b.data(), &sz );
				if ( e != UC_ERR_OK || sz != n )
				{
					error = "reading " + name + ": " + ( e != UC_ERR_OK ? std::string( uc_strerror( e ) ) : "size " + std::to_string( sz ) );
					return false;
				}
				b.resize( n );
				out[ name ] = b;
				return true;
			};
			for ( int i = 0; i < 8; i++ )
				if ( !rd( UC_X86_REG_ST0 + i, 10, "st" + std::to_string( i ) ) ) return false;
			for ( int i : dr_index )
				if ( !rd( UC_X86_REG_DR0 + i, 8, "dr" + std::to_string( i ) ) ) return false;
			if ( apx )
				for ( int i = 0; i < 16; i++ )
					if ( !rd( UC_X86_REG_R16 + i, 8, "r" + std::to_string( 16 + i ) ) ) return false;
			return true;
		}

		bool write_scalar( uc_engine* uc, int id, uint64_t v, size_t n, const std::string& name, std::string& error )
		{
			uint8_t b[ 8 ] = {};
			std::memcpy( b, &v, n );
			size_t sz = n;
			const uc_err e = uc_reg_write2( uc, id, b, &sz );
			if ( e != UC_ERR_OK ) { error = "writing " + name + ": " + uc_strerror( e ); return false; }
			return true;
		}
		bool write_bytes( uc_engine* uc, int id, const std::vector<uint8_t>& v, const std::string& name, std::string& error )
		{
			size_t sz = v.size();
			const uc_err e = uc_reg_write2( uc, id, v.data(), &sz );
			if ( e != UC_ERR_OK ) { error = "writing " + name + ": " + uc_strerror( e ); return false; }
			return true;
		}

		const char* vector_name( int v )
		{
			static const char* n[ 22 ] = { "#DE", "#DB", "#NMI", "#BP", "#OF", "#BR", "#UD", "#NM", "#DF", "#9", "#TS", "#NP", "#SS", "#GP", "#PF", "#15",
										   "#MF", "#AC", "#MC", "#XM", "#VE", "#CP" };
			return v >= 0 && v < 22 ? n[ v ] : nullptr;
		}
		std::string fault_text( int v, int64_t ec )
		{
			if ( v < 0 ) return "no fault";
			const char* n = vector_name( v );
			std::string s = n ? n : "#" + std::to_string( v );
			if ( ec >= 0 ) s += "(" + hex( uint64_t( ec ) ) + ")";
			return s;
		}

		// ---------------------------------------------------------------------------------------------- the run
		struct ctx
		{
			uc_engine* uc = nullptr;
			const spec* s = nullptr;
			result* r = nullptr;
			disassembler* dis = nullptr;
			bool apx = false;
			bool per_insn = false;
			int64_t depth = 0;
			bool ret_pending = false;
			bool have_cur = false;
			trace_entry cur;
			std::map<std::string, std::vector<uint8_t>> prev;
		};

		void set_fault( ctx& c, int vector, int64_t ec, uint64_t rip )
		{
			if ( c.r->reason != stop_reason::none ) return;
			c.r->reason = stop_reason::fault;
			c.r->faulted = true;
			c.r->vector = vector;
			c.r->error_code = ec;
			c.r->rip = rip;
		}

		std::string describe_change( const std::string& name, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b )
		{
			std::string s = name + ": " + format_field( name, a ) + " -> " + format_field( name, b );
			if ( name == "rflags" )
			{
				const uint64_t x = le( a ), y = le( b );
				const struct { const char* n; int bit; } fl[] = { { "CF", 0 }, { "PF", 2 }, { "AF", 4 }, { "ZF", 6 }, { "SF", 7 }, { "TF", 8 }, { "IF", 9 },
																   { "DF", 10 }, { "OF", 11 }, { "RF", 16 }, { "AC", 18 } };
				std::string d;
				for ( auto& f : fl )
					if ( ( ( x ^ y ) >> f.bit ) & 1 ) d += std::string( d.empty() ? "" : " " ) + f.n + " " + std::to_string( ( x >> f.bit ) & 1 ) + "->" + std::to_string( ( y >> f.bit ) & 1 );
				if ( !d.empty() ) s += " (" + d + ")";
			}
			return s;
		}

		void finish_trace_entry( ctx& c )
		{
			if ( !c.have_cur ) return;
			std::map<std::string, std::vector<uint8_t>> now;
			std::string err;
			if ( read_fields( c.uc, c.apx, now, err ) )
			{
				for ( auto& n : field_names( c.apx ) )
				{
					auto a = c.prev.find( n );
					auto b = now.find( n );
					if ( a != c.prev.end() && b != now.end() && a->second != b->second && n != "rip" ) c.cur.changes.push_back( describe_change( n, a->second, b->second ) );
				}
				c.prev = std::move( now );
			}
			else
				c.cur.changes.push_back( "(state not read: " + err + ")" );
			c.r->trace.push_back( std::move( c.cur ) );
			c.cur = trace_entry{};
			c.have_cur = false;
		}

		void on_code( uc_engine* uc, uint64_t address, uint32_t size, void* user )
		{
			ctx& c = *static_cast<ctx*>( user );
			if ( c.r->reason != stop_reason::none ) { uc_emu_stop( uc ); return; }
			if ( c.ret_pending )
			{
				// the RET that reached the requested depth has executed: stop before the instruction it returned to
				if ( c.s->trace ) finish_trace_entry( c );
				c.r->reason = stop_reason::ret_depth;
				c.r->rip = address;
				uc_emu_stop( uc );
				return;
			}
			uint8_t b[ 16 ] = {};
			const size_t n = std::min<size_t>( size ? size : 15, 15 );
			uc_mem_read( uc, address, b, n );
			const insn_info in = c.dis->decode( b, n, address );
			if ( c.s->syscall == syscall_policy::stop && in.is_system )
			{
				if ( c.s->trace ) finish_trace_entry( c );
				uint64_t rax = 0;
				uc_reg_read( uc, UC_X86_REG_RAX, &rax );
				c.r->reason = stop_reason::syscall;
				c.r->rip = address;
				c.r->syscall_kind = in.system_kind;
				c.r->syscall_number = rax;
				uc_emu_stop( uc );
				return;
			}
			if ( c.s->trace )
			{
				finish_trace_entry( c );
				c.cur.address = address;
				c.cur.bytes.assign( b, b + n );
				c.cur.text = in.text;
				c.have_cur = true;
			}
			if ( in.is_call ) c.depth++;
			if ( in.is_ret )
			{
				c.depth--;
				if ( c.s->ret_depth_given && c.depth <= -int64_t( c.s->ret_depth ) ) c.ret_pending = true;
			}
			c.r->insns++;
		}

		void on_mem( uc_engine* uc, uc_mem_type type, uint64_t address, int size, int64_t value, void* user )
		{
			ctx& c = *static_cast<ctx*>( user );
			if ( !c.have_cur ) return;
			char b[ 96 ];
			if ( type == UC_MEM_WRITE )
			{
				if ( size <= 8 ) std::snprintf( b, sizeof( b ), "write [0x%llx] %d = 0x%llx", (unsigned long long)address, size, (unsigned long long)value );
				else std::snprintf( b, sizeof( b ), "write [0x%llx] %d bytes", (unsigned long long)address, size );
				c.cur.mem.push_back( b );
			}
			else
			{
				std::vector<uint8_t> v( size_t( size > 0 && size <= 64 ? size : 0 ) );
				if ( !v.empty() ) uc_mem_read( uc, address, v.data(), v.size() );
				std::snprintf( b, sizeof( b ), "read  [0x%llx] %d = ", (unsigned long long)address, size );
				std::vector<uint8_t> rev( v.rbegin(), v.rend() );   // print as a little-endian number (most significant byte first)
				c.cur.mem.push_back( std::string( b ) + ( v.size() <= 8 ? "0x" + hexbytes( rev ) : "bytes " + hexbytes( v ) ) );
			}
		}

		void on_intr( uc_engine* uc, uint32_t intno, void* user )
		{
			ctx& c = *static_cast<ctx*>( user );
			uint64_t rip = 0;
			uc_reg_read( uc, UC_X86_REG_RIP, &rip );
			uc_x86_exception e{};
			int64_t ec = -1;
			if ( uc_ctl_get_x86_exception( uc, &e ) == UC_ERR_OK && e.vector == int32_t( intno ) && e.has_error_code ) ec = e.error_code;
			if ( c.s->trace ) finish_trace_entry( c );
			set_fault( c, int( intno ), ec, rip );
			uc_emu_stop( uc );
		}

		bool on_invalid( uc_engine* uc, void* user )
		{
			ctx& c = *static_cast<ctx*>( user );
			uint64_t rip = 0;
			uc_reg_read( uc, UC_X86_REG_RIP, &rip );
			if ( c.s->trace ) finish_trace_entry( c );
			set_fault( c, 6, -1, rip );
			return false;
		}

		bool on_bad_mem( uc_engine* uc, uc_mem_type type, uint64_t address, int, int64_t, void* user )
		{
			ctx& c = *static_cast<ctx*>( user );
			if ( c.r->reason != stop_reason::none ) return false;
			uint64_t rip = 0;
			uc_reg_read( uc, UC_X86_REG_RIP, &rip );
			const bool fetch = type == UC_MEM_FETCH_UNMAPPED || type == UC_MEM_FETCH_PROT;
			if ( c.s->trace ) finish_trace_entry( c );
			if ( fetch && c.ret_pending )
			{
				c.r->reason = stop_reason::ret_depth;
				c.r->rip = address;
				return false;
			}
			if ( fetch && c.s->auto_stack && address == return_sentinel )
			{
				c.r->reason = stop_reason::returned;
				c.r->rip = address;
				return false;
			}
			if ( fetch && c.s->external == external_policy::stop )
			{
				c.r->reason = stop_reason::external;
				c.r->external_target = address;
				c.r->rip = address;
				return false;
			}
			set_fault( c, 14, -1, rip );
			c.r->pf_access = fetch ? 8 : ( type == UC_MEM_WRITE_UNMAPPED || type == UC_MEM_WRITE_PROT ) ? 1 : 0;
			c.r->pf_address = address;
			return false;
		}

		// a free page for the state loader (not in a mapped region)
		bool loader_page( const engine& e, uint64_t& out )
		{
			const uint64_t cand[] = { 0x00007FFFFFFE0000ull, 0x00000000FFFF0000ull, 0x0000000000F00000ull, 0x00007FF000000000ull };
			for ( uint64_t a : cand )
			{
				bool free = true;
				for ( auto& r : e.regions() )
					if ( a <= r.base + ( r.size - 1 ) && r.base <= a + 0xFFF ) free = false;
				if ( free ) { out = a; return true; }
			}
			return false;
		}

		// the x87 / SSE / AVX state through XRSTOR64 (or FXRSTOR64) of an image built from DESIRED, optionally followed by IRETQ
		// to CPL3; every step logged
		bool run_loader( engine& e, const spec& s, const std::map<std::string, std::vector<uint8_t>>& desired, uint64_t start_rip, result& r,
						 std::string& error )
		{
			uc_engine* uc = e.handle();
			const auto& cur = desired;
			const uint64_t cr4 = le( cur.at( "cr4" ) ), xcr0 = le( cur.at( "xcr0" ) );
			const bool osxsave = ( cr4 >> 18 ) & 1;
			uint64_t page = 0;
			if ( !loader_page( e, page ) ) { error = "no free page for the state loader"; return false; }
			if ( !e.map( page, 0x1000, UC_PROT_ALL, "state loader (unmapped before the snippet)", error ) ) return false;
			auto done = [ & ]( bool ok ) {
				std::string uerr;
				if ( !e.unmap( page, 0x1000, uerr ) && ok ) { error = uerr; return false; }
				return ok;
			};
			const uint64_t rfbm = osxsave ? ( xcr0 & 7 ) | 1 : 3;
			uint64_t avx_off = 0;
			if ( osxsave && ( rfbm & 4 ) )
			{
				// CPUID.(EAX=0DH, ECX=2):EBX = the standard-format offset of the AVX state component (Vol1 13.4.3)
				const uint8_t probe[] = { 0xB8, 0x0D, 0x00, 0x00, 0x00, 0xB9, 0x02, 0x00, 0x00, 0x00, 0x0F, 0xA2 };
				if ( !e.write( page, probe, sizeof( probe ), error ) ) return done( false );
				const uc_err ue = e.emulate( page, page + sizeof( probe ), 3 );
				uint64_t rbx = 0;
				uc_reg_read( uc, UC_X86_REG_RBX, &rbx );
				if ( ue != UC_ERR_OK || rbx < 576 || rbx + 256 > 0x1000 - 0x400 )
				{
					error = "state loader: CPUID.(0DH,2):EBX probe " + std::string( uc_strerror( ue ) ) + ", offset " + hex( rbx );
					return done( false );
				}
				avx_off = rbx;
				r.log.push_back( "state loader: CPUID.(0DH,2):EBX = " + hex( avx_off ) + " (standard-format AVX offset, 253665-093 Vol1 13.4.3 p. 13-9)" );
			}
			// the image at page + 0x400 (64-byte aligned)
			std::vector<uint8_t> img( 0x1000 - 0x400, 0 );
			auto put = [ & ]( size_t off, const std::vector<uint8_t>& b, size_t n ) { std::memcpy( img.data() + off, b.data(), std::min( n, b.size() ) ); };
			put( 0, cur.at( "fpcw" ), 2 );
			put( 2, cur.at( "fpsw" ), 2 );
			img[ 4 ] = cur.at( "ftw" )[ 0 ];
			put( 6, cur.at( "fop" ), 2 );
			put( 8, cur.at( "fip" ), 8 );
			put( 16, cur.at( "fdp" ), 8 );
			put( 24, cur.at( "mxcsr" ), 4 );
			for ( int i = 0; i < 8; i++ ) put( size_t( 32 + 16 * i ), cur.at( "st" + std::to_string( i ) ), 10 );
			for ( int i = 0; i < 16; i++ ) put( size_t( 160 + 16 * i ), cur.at( "zmm" + std::to_string( i ) ), 16 );
			if ( osxsave )
			{
				for ( int k = 0; k < 8; k++ ) img[ 512 + k ] = uint8_t( rfbm >> ( 8 * k ) );   // XSTATE_BV = RFBM; XCOMP_BV = 0 (standard form)
				if ( avx_off )
					for ( int i = 0; i < 16; i++ ) std::memcpy( img.data() + avx_off + 16 * i, cur.at( "zmm" + std::to_string( i ) ).data() + 16, 16 );
			}
			if ( !e.write( page + 0x400, img.data(), img.size(), error ) ) return done( false );
			std::vector<uint8_t> code;
			if ( osxsave )
			{
				code = { 0xB8, uint8_t( rfbm ), 0, 0, 0, 0x31, 0xD2, 0x48, 0x0F, 0xAE, 0x2D };   // mov eax, RFBM; xor edx, edx; xrstor64 [rip + disp32]
			}
			else
				code = { 0x48, 0x0F, 0xAE, 0x0D };   // fxrstor64 [rip + disp32]
			const uint32_t disp = uint32_t( 0x400 - ( code.size() + 4 ) );
			for ( int k = 0; k < 4; k++ ) code.push_back( uint8_t( disp >> ( 8 * k ) ) );
			const uint64_t after_restore = page + code.size();
			if ( s.cpl3 )
			{
				code.push_back( 0x48 );
				code.push_back( 0xCF );   // iretq
				// the frame at page + 0x380: RIP, CS, RFLAGS, RSP, SS
				const uint64_t frame[ 5 ] = { start_rip, le( cur.at( "cs" ) ), le( cur.at( "rflags" ) ), le( cur.at( "rsp" ) ), le( cur.at( "ss" ) ) };
				if ( !e.write( page + 0x380, frame, sizeof( frame ), error ) ) return done( false );
				if ( !write_scalar( uc, UC_X86_REG_RSP, page + 0x380, 8, "rsp (loader)", error ) ) return done( false );
			}
			if ( !e.write( page, code.data(), code.size(), error ) ) return done( false );
			const uint64_t until = s.cpl3 ? start_rip : after_restore;
			const uc_err ue = e.emulate( page, until, s.cpl3 ? 4 : 3 );
			uint64_t rip = 0;
			uc_reg_read( uc, UC_X86_REG_RIP, &rip );
			if ( ue != UC_ERR_OK || rip != until )
			{
				error = std::string( "state loader: " ) + ( osxsave ? "XRSTOR64" : "FXRSTOR64" ) + ( s.cpl3 ? " + IRETQ" : "" ) + " failed (" + uc_strerror( ue ) +
						", rip " + hex( rip ) + ") - an MXCSR with reserved bits set, or a CS / SS / GDT the IRETQ refuses?";
				return done( false );
			}
			r.log.push_back( std::string( "state loader: " ) + ( osxsave ? "XRSTOR64 with RFBM = XSTATE_BV = " + hex( rfbm ) : std::string( "FXRSTOR64 (no CR4.OSXSAVE)" ) ) +
							 " at " + hex( page ) + ( s.cpl3 ? ", then IRETQ to CS " + hex( le( cur.at( "cs" ) ) ) + " / SS " + hex( le( cur.at( "ss" ) ) ) : std::string() ) +
							 "; the page is unmapped before the snippet" );
			return done( true );
		}

		void compare_bytes( const std::string& what, const std::vector<uint8_t>& actual, const std::vector<uint8_t>& want, const std::vector<uint8_t>& mask,
							std::vector<std::string>& diffs, bool scalar )
		{
			bool differ = false;
			for ( size_t i = 0; i < want.size(); i++ )
			{
				const uint8_t m = mask.empty() ? 0xFF : mask[ i ];
				const uint8_t a = i < actual.size() ? actual[ i ] : 0;
				if ( ( a ^ want[ i ] ) & m ) differ = true;
			}
			if ( !differ ) return;
			std::string s = what + ": " + ( scalar ? hex( le( actual ) ) : hexbytes( actual ) ) + ", expected " + ( scalar ? hex( le( want ) ) : hexbytes( want ) );
			if ( !mask.empty() && std::any_of( mask.begin(), mask.end(), []( uint8_t m ) { return m != 0xFF; } ) )
				s += " under mask " + ( scalar ? hex( le( mask ) ) : hexbytes( mask ) );
			diffs.push_back( s );
		}

		void run_checks( const checks& c, const spec& s, engine& e, const result& r, const std::vector<std::pair<range, std::vector<uint8_t>>>& start_mem,
						 bool apx, std::vector<std::string>& diffs )
		{
			// the fault outcome
			if ( c.fault_given )
			{
				if ( c.vector < 0 )
				{
					if ( r.faulted ) diffs.push_back( "fault: " + fault_text( r.vector, r.error_code ) + ", expected no fault" );
					else if ( r.reason != stop_reason::end && r.reason != stop_reason::returned && r.reason != stop_reason::ret_depth )
						diffs.push_back( std::string( "stop: " ) + name_of( r.reason ) + ", expected a normal end (no fault)" );
				}
				else
				{
					if ( !r.faulted || r.vector != c.vector ) diffs.push_back( "fault: " + ( r.faulted ? fault_text( r.vector, r.error_code ) : std::string( "none (stop " ) + name_of( r.reason ) + ")" ) +
																			  ", expected " + fault_text( c.vector, c.error_code ) );
					else if ( c.error_code >= 0 && r.error_code != c.error_code )
						diffs.push_back( "error code: " + ( r.error_code >= 0 ? hex( uint64_t( r.error_code ) ) : std::string( "none" ) ) + ", expected " + hex( uint64_t( c.error_code ) ) );
					if ( r.faulted && r.vector == 14 && c.vector == 14 && c.pf_access >= 0 && r.pf_access != c.pf_access )
						diffs.push_back( "#PF access " + std::to_string( r.pf_access ) + ", expected " + std::to_string( c.pf_access ) );
				}
			}
			// registers
			std::set<std::string> listed;
			for ( auto& f : c.regs )
			{
				listed.insert( f.name );
				auto a = r.final.find( f.name );
				if ( a == r.final.end() ) { diffs.push_back( f.name + ": not read (APX state needs the --apx opt-in)" ); continue; }
				const fdef* d = find_field( f.name );
				std::vector<uint8_t> want = f.bytes, mask = f.mask;
				if ( c.strict && !mask.empty() )
				{
					// strict: the bits the expectation does not list must keep their start value
					auto st = r.start.find( f.name );
					if ( st != r.start.end() )
					{
						for ( size_t i = 0; i < want.size(); i++ ) want[ i ] = uint8_t( ( st->second[ i ] & ~mask[ i ] ) | ( want[ i ] & mask[ i ] ) );
						if ( f.name != "rflags" ) mask.clear();   // rflags keeps its mask (the PUSHFQ rule of the replay)
					}
				}
				compare_bytes( f.name, a->second, want, mask, diffs, d && d->k == fkind::scalar );
			}
			if ( c.strict )
				for ( auto& n : strict_set( apx ) )
					if ( !listed.count( n ) )
					{
						auto a = r.final.find( n ), st = r.start.find( n );
						if ( a != r.final.end() && st != r.start.end() && a->second != st->second )
						{
							const fdef* d = find_field( n );
							compare_bytes( n + " (not listed: must be unchanged)", a->second, st->second, {}, diffs, d && d->k == fkind::scalar );
						}
					}
			// memory
			auto read_now = [ & ]( uint64_t addr, size_t n, std::vector<uint8_t>& out ) {
				out.assign( n, 0 );
				std::string err;
				return e.read( addr, out.data(), n, err );
			};
			for ( auto& m : c.mem )
			{
				bool covered = false;
				for ( auto& fr : c.full_mem )
					if ( m.address >= fr.address && m.address - fr.address < fr.size ) covered = true;
				if ( covered ) continue;   // compared with its full range below
				std::vector<uint8_t> now;
				if ( !read_now( m.address, m.bytes.size(), now ) ) { diffs.push_back( "memory [" + hex( m.address ) + ", +" + std::to_string( m.bytes.size() ) + ") not mapped" ); continue; }
				compare_bytes( "memory [" + hex( m.address ) + "]", now, m.bytes, m.mask, diffs, false );
			}
			auto range_diff = [ & ]( const std::string& what, uint64_t base, const std::vector<uint8_t>& now, const std::vector<uint8_t>& want ) {
				size_t n = 0, first = SIZE_MAX;
				for ( size_t i = 0; i < want.size(); i++ )
					if ( now[ i ] != want[ i ] ) { n++; if ( first == SIZE_MAX ) first = i; }
				if ( !n ) return;
				char b[ 160 ];
				std::snprintf( b, sizeof( b ), "%s [0x%llx, +0x%llx): %zu byte(s) differ, first at 0x%llx: 0x%02x, expected 0x%02x", what.c_str(), (unsigned long long)base,
							   (unsigned long long)want.size(), n, (unsigned long long)( base + first ), now[ first ], want[ first ] );
				diffs.push_back( b );
			};
			if ( c.strict )
				for ( auto& cr : c.checked_mem )
				{
					std::vector<uint8_t> want, now;
					for ( auto& sm : start_mem )
						if ( sm.first.address == cr.address && sm.first.size == cr.size ) want = sm.second;
					if ( want.size() != cr.size || !read_now( cr.address, size_t( cr.size ), now ) ) { diffs.push_back( "checked memory [" + hex( cr.address ) + "] not readable" ); continue; }
					for ( auto& m : c.mem )
						for ( size_t i = 0; i < m.bytes.size(); i++ )
							if ( m.address + i >= cr.address && m.address + i - cr.address < cr.size )
							{
								const uint8_t mk = m.mask.empty() ? 0xFF : m.mask[ i ];
								uint8_t& w = want[ size_t( m.address + i - cr.address ) ];
								w = uint8_t( ( w & ~mk ) | ( m.bytes[ i ] & mk ) );
							}
					range_diff( "memory", cr.address, now, want );
				}
			for ( auto& fr : c.full_mem )
			{
				std::vector<uint8_t> want( size_t( fr.size ), 0 ), now;
				for ( auto& m : c.mem )
					for ( size_t i = 0; i < m.bytes.size(); i++ )
						if ( m.address + i >= fr.address && m.address + i - fr.address < fr.size ) want[ size_t( m.address + i - fr.address ) ] = m.bytes[ i ];
				if ( !read_now( fr.address, size_t( fr.size ), now ) ) { diffs.push_back( "memory [" + hex( fr.address ) + "] not readable" ); continue; }
				range_diff( "memory", fr.address, now, want );
			}
			(void)s;
		}
	}

	const char* name_of( stop_reason r )
	{
		switch ( r )
		{
		case stop_reason::none: return "none";
		case stop_reason::end: return "end";
		case stop_reason::count: return "instruction bound";
		case stop_reason::returned: return "returned";
		case stop_reason::ret_depth: return "ret depth";
		case stop_reason::fault: return "fault";
		case stop_reason::syscall: return "system call";
		case stop_reason::external: return "external transfer";
		case stop_reason::error: return "error";
		}
		return "?";
	}

	std::vector<std::string> field_names( bool apx )
	{
		std::vector<std::string> v;
		for ( auto& f : all_fields() )
			if ( apx || !is_apx_name( f.name ) ) v.push_back( f.name );
		return v;
	}

	std::string format_field( const std::string& name, const std::vector<uint8_t>& b )
	{
		const fdef* f = find_field( name );
		if ( f && f->k == fkind::scalar )
		{
			char t[ 40 ];
			std::snprintf( t, sizeof( t ), "0x%0*llx", int( 2 * f->size ), (unsigned long long)le( b ) );
			return t;
		}
		if ( name.rfind( "st", 0 ) == 0 && b.size() == 10 )
		{
			char t[ 40 ];
			std::vector<uint8_t> m( b.begin(), b.begin() + 8 ), se( b.begin() + 8, b.end() );
			std::snprintf( t, sizeof( t ), "%04llx:%016llx", (unsigned long long)le( se ), (unsigned long long)le( m ) );
			return t;
		}
		return hexbytes( b );
	}

	// ------------------------------------------------------------------------------------------------- parsing
	bool parse_assignment( const std::string& text, field_value& out, std::string& error )
	{
		const size_t eq = text.find( '=' );
		if ( eq == std::string::npos || eq == 0 ) { error = "want NAME=VALUE, got \"" + text + "\""; return false; }
		std::string name;
		size_t covered = 0;
		if ( !canonical( text.substr( 0, eq ), name, covered, error ) ) return false;
		const fdef* f = find_field( name );
		out.name = name;
		return value_into( *f, covered, text.substr( eq + 1 ), out, error );
	}

	bool parse_mem_assignment( const std::string& text, mem_bytes& out, std::string& error )
	{
		const size_t eq = text.find( '=' );
		if ( eq == std::string::npos ) { error = "want ADDR=HEXBYTES, got \"" + text + "\""; return false; }
		if ( !parse_u64( text.substr( 0, eq ), out.address ) ) { error = "bad address in \"" + text + "\""; return false; }
		if ( !parse_hexbytes( text.substr( eq + 1 ), max_io_bytes, out.bytes, error ) ) return false;
		if ( out.address + ( out.bytes.size() - 1 ) < out.address ) { error = "range wraps around 2^64"; return false; }
		out.mask.clear();
		return true;
	}

	bool parse_map( const std::string& text, region& out, std::string& error )
	{
		const size_t c1 = text.find( ':' );
		if ( c1 == std::string::npos ) { error = "want ADDR:SIZE[:PROT], got \"" + text + "\""; return false; }
		const size_t c2 = text.find( ':', c1 + 1 );
		if ( !parse_u64( text.substr( 0, c1 ), out.base ) || !parse_u64( text.substr( c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1 ), out.size ) )
		{
			error = "bad number in \"" + text + "\"";
			return false;
		}
		out.prot = UC_PROT_READ | UC_PROT_WRITE;
		if ( c2 != std::string::npos )
		{
			out.prot = 0;
			for ( char ch : text.substr( c2 + 1 ) )
			{
				if ( ch == 'r' || ch == 'R' ) out.prot |= UC_PROT_READ;
				else if ( ch == 'w' || ch == 'W' ) out.prot |= UC_PROT_WRITE;
				else if ( ch == 'x' || ch == 'X' ) out.prot |= UC_PROT_EXEC;
				else if ( ch != '-' ) { error = "protection letters are r w x"; return false; }
			}
		}
		out.name = "map " + text;
		return true;
	}

	bool parse_fault( const std::string& text, int& vector, int64_t& error_code, std::string& error )
	{
		error_code = -1;
		std::string t = text;
		if ( lower( t ) == "none" ) { vector = -1; return true; }
		const size_t p = t.find( '(' );
		if ( p != std::string::npos )
		{
			if ( t.back() != ')' ) { error = "bad fault \"" + text + "\""; return false; }
			uint64_t ec = 0;
			if ( !parse_u64( t.substr( p + 1, t.size() - p - 2 ), ec ) || ec > 0xFFFFFFFFull ) { error = "bad error code in \"" + text + "\""; return false; }
			error_code = int64_t( ec );
			t = t.substr( 0, p );
		}
		for ( int v = 0; v < 22; v++ )
			if ( vector_name( v ) && lower( t ) == lower( vector_name( v ) ) ) { vector = v; return true; }
		uint64_t n = 0;
		if ( ( t[ 0 ] == '#' ? parse_u64( t.substr( 1 ), n ) : parse_u64( t, n ) ) && n < 256 ) { vector = int( n ); return true; }
		error = "unknown fault \"" + text + "\" (none, #DE #DB #BP #OF #BR #UD #NM #DF #TS #NP #SS #GP #PF #MF #AC #MC #XM #VE #CP, a vector number, \"#GP(0)\")";
		return false;
	}

	namespace
	{
		bool get_str( const json::value& v, std::string& out, const std::string& what, std::string& error )
		{
			if ( !v.is( json::kind::string ) ) { error = what + ": a string expected"; return false; }
			out = v.text;
			return true;
		}
		bool get_u( const json::value& v, uint64_t& out, const std::string& what, std::string& error )
		{
			std::string e;
			if ( !json::get_u64( v, out, e ) ) { error = what + ": " + e; return false; }
			return true;
		}
		bool get_i( const json::value& v, int64_t& out, const std::string& what, std::string& error )
		{
			std::string e;
			if ( !json::get_i64( v, out, e ) ) { error = what + ": " + e; return false; }
			return true;
		}
		bool only_keys( const json::value& v, std::initializer_list<const char*> allowed, const std::string& what, std::string& error )
		{
			if ( !v.is( json::kind::object ) ) { error = what + ": an object expected"; return false; }
			for ( auto& k : v.keys )
				if ( std::none_of( allowed.begin(), allowed.end(), [ & ]( const char* a ) { return k == a; } ) )
				{
					error = what + ": unknown key \"" + k + "\"";
					return false;
				}
			return true;
		}
		// a field value: "0x..." / number / "hexbytes" / {"hex": .., "mask": ..} / {"value": .., "mask": ..}
		bool field_from_json( const std::string& key, const json::value& v, field_value& out, std::string& error )
		{
			std::string name;
			size_t covered = 0;
			if ( !canonical( key, name, covered, error ) ) return false;
			const fdef* f = find_field( name );
			out.name = name;
			const json::value* val = &v;
			const json::value* mask = nullptr;
			if ( v.is( json::kind::object ) )
			{
				if ( !only_keys( v, { "hex", "value", "mask" }, key, error ) ) return false;
				val = v.find( f->k == fkind::scalar ? "value" : "hex" );
				mask = v.find( "mask" );
				if ( !val ) { error = key + ": needs \"" + std::string( f->k == fkind::scalar ? "value" : "hex" ) + "\""; return false; }
			}
			std::string text;
			if ( val->is( json::kind::number ) ) text = val->text;
			else if ( !get_str( *val, text, key, error ) ) return false;
			if ( f->k == fkind::scalar && val->is( json::kind::number ) && !text.empty() && text[ 0 ] == '-' ) { error = key + ": negative"; return false; }
			if ( !value_into( *f, covered, text, out, error ) ) { error = key + ": " + error; return false; }
			if ( mask )
			{
				std::string mt;
				if ( mask->is( json::kind::number ) ) mt = mask->text;
				else if ( !get_str( *mask, mt, key + ".mask", error ) ) return false;
				field_value m;
				if ( !value_into( *f, f->size, mt, m, error ) ) { error = key + ".mask: " + error; return false; }
				for ( size_t i = 0; i < out.mask.size(); i++ ) out.mask[ i ] = uint8_t( out.mask[ i ] & m.bytes[ i ] );
			}
			return true;
		}
		bool mem_from_json( const json::value& v, mem_bytes& out, const std::map<std::string, std::vector<uint8_t>>* blobs, uint64_t base, const std::string& what,
							std::string& error )
		{
			if ( !only_keys( v, { "address", "offset", "hex", "blob", "mask" }, what, error ) ) return false;
			const json::value* a = v.find( "address" );
			const json::value* o = v.find( "offset" );
			if ( ( a != nullptr ) == ( o != nullptr ) ) { error = what + ": exactly one of \"address\" / \"offset\""; return false; }
			uint64_t x = 0;
			if ( !get_u( a ? *a : *o, x, what, error ) ) return false;
			out.address = a ? x : base + x;
			if ( !a && out.address < base ) { error = what + ": offset wraps"; return false; }
			const json::value* h = v.find( "hex" );
			const json::value* b = v.find( "blob" );
			if ( ( h != nullptr ) == ( b != nullptr ) ) { error = what + ": exactly one of \"hex\" / \"blob\""; return false; }
			if ( h )
			{
				std::string t;
				if ( !get_str( *h, t, what, error ) || !parse_hexbytes( t, max_io_bytes, out.bytes, error ) ) { error = what + ": " + error; return false; }
			}
			else
			{
				std::string n;
				if ( !get_str( *b, n, what, error ) ) return false;
				const auto it = blobs ? blobs->find( n ) : std::map<std::string, std::vector<uint8_t>>::const_iterator();
				if ( !blobs || it == blobs->end() ) { error = what + ": unknown blob \"" + n + "\" (define it earlier in the batch file)"; return false; }
				out.bytes = it->second;
			}
			if ( out.bytes.empty() || out.address + ( out.bytes.size() - 1 ) < out.address ) { error = what + ": empty or wrapping range"; return false; }
			if ( const json::value* m = v.find( "mask" ) )
			{
				std::string t;
				if ( !get_str( *m, t, what + ".mask", error ) || !parse_hexbytes( t, max_io_bytes, out.mask, error ) || out.mask.size() != out.bytes.size() )
				{
					error = what + ".mask: hex bytes of the same length";
					return false;
				}
			}
			return true;
		}
		bool range_from_json( const json::value& v, range& out, const std::string& what, std::string& error )
		{
			if ( !only_keys( v, { "address", "size" }, what, error ) ) return false;
			const json::value* a = v.find( "address" );
			const json::value* s = v.find( "size" );
			if ( !a || !s || !get_u( *a, out.address, what, error ) || !get_u( *s, out.size, what, error ) ) { if ( error.empty() ) error = what + ": address and size"; return false; }
			if ( out.size == 0 || out.size > max_io_bytes || out.address + ( out.size - 1 ) < out.address ) { error = what + ": size 1.." + std::to_string( max_io_bytes ) + ", no wrap"; return false; }
			return true;
		}
		bool fault_from_json( const json::value& v, checks& c, const std::string& what, std::string& error )
		{
			c.fault_given = true;
			if ( v.is( json::kind::null ) ) { c.vector = -1; return true; }
			if ( v.is( json::kind::string ) ) return parse_fault( v.text, c.vector, c.error_code, error );
			if ( !only_keys( v, { "vector", "error_code", "pf_access" }, what, error ) ) return false;
			int64_t x = 0;
			const json::value* vec = v.find( "vector" );
			if ( !vec || !get_i( *vec, x, what + ".vector", error ) || x < -1 || x > 255 ) { if ( error.empty() ) error = what + ".vector: -1..255"; return false; }
			c.vector = int( x );
			if ( const json::value* ec = v.find( "error_code" ) )
			{
				if ( !get_i( *ec, c.error_code, what + ".error_code", error ) || c.error_code < -1 || c.error_code > 0xFFFFFFFFll ) { if ( error.empty() ) error = what + ".error_code"; return false; }
			}
			if ( const json::value* pa = v.find( "pf_access" ) )
			{
				if ( !get_i( *pa, x, what + ".pf_access", error ) || ( x != -1 && x != 0 && x != 1 && x != 8 ) ) { if ( error.empty() ) error = what + ".pf_access: -1, 0, 1 or 8"; return false; }
				c.pf_access = int( x );
			}
			return true;
		}
		bool checks_from_json( const json::value& v, checks& c, bool reference, const std::map<std::string, std::vector<uint8_t>>* blobs, std::string& error )
		{
			const std::string what = reference ? "reference" : "expect";
			if ( !only_keys( v, { "mode", "fault", "regs", "zero", "mem", "checked_mem", "full_mem", "source" }, what, error ) ) return false;
			c.present = true;
			c.label = what;
			if ( const json::value* m = v.find( "mode" ) )
			{
				std::string t;
				if ( !get_str( *m, t, what + ".mode", error ) ) return false;
				if ( t != "strict" && t != "loose" ) { error = what + ".mode: strict or loose"; return false; }
				c.strict = t == "strict";
			}
			if ( const json::value* s = v.find( "source" ) )
			{
				std::string t;
				if ( !get_str( *s, t, what + ".source", error ) ) return false;
				c.label = what + " (" + t + ")";
			}
			if ( const json::value* f = v.find( "fault" ) )
				if ( !fault_from_json( *f, c, what + ".fault", error ) ) return false;
			if ( const json::value* rg = v.find( "regs" ) )
			{
				if ( !rg->is( json::kind::object ) ) { error = what + ".regs: an object"; return false; }
				for ( size_t i = 0; i < rg->keys.size(); i++ )
				{
					field_value fv;
					if ( !field_from_json( rg->keys[ i ], rg->items[ i ], fv, error ) ) { error = what + ".regs." + error; return false; }
					c.regs.push_back( fv );
				}
			}
			if ( const json::value* z = v.find( "zero" ) )
			{
				// registers expected all zero, by name (an alias covers its bytes: ymmN = the low 32 bytes of zmmN)
				if ( !z->is( json::kind::array ) ) { error = what + ".zero: an array of register names"; return false; }
				for ( auto& it : z->items )
				{
					std::string n, name;
					size_t covered = 0;
					if ( !get_str( it, n, what + ".zero[]", error ) || !canonical( n, name, covered, error ) ) { error = what + ".zero: " + error; return false; }
					const fdef* f = find_field( name );
					field_value fv;
					fv.name = name;
					fv.bytes.assign( f->size, 0 );
					fv.mask.assign( f->size, 0 );
					std::fill( fv.mask.begin(), fv.mask.begin() + std::ptrdiff_t( std::min( covered, f->size ) ), uint8_t( 0xFF ) );
					c.regs.push_back( fv );
				}
			}
			if ( const json::value* m = v.find( "mem" ) )
			{
				if ( !m->is( json::kind::array ) ) { error = what + ".mem: an array"; return false; }
				for ( auto& it : m->items )
				{
					mem_bytes mb;
					if ( !mem_from_json( it, mb, blobs, 0, what + ".mem[]", error ) ) return false;
					c.mem.push_back( mb );
				}
			}
			for ( auto [ key, dst ] : std::initializer_list<std::pair<const char*, std::vector<range>*>>{ { "checked_mem", &c.checked_mem }, { "full_mem", &c.full_mem } } )
				if ( const json::value* m = v.find( key ) )
				{
					if ( !m->is( json::kind::array ) ) { error = what + "." + key + ": an array"; return false; }
					for ( auto& it : m->items )
					{
						range r;
						if ( !range_from_json( it, r, what + "." + key + "[]", error ) ) return false;
						dst->push_back( r );
					}
				}
			return true;
		}
	}

	bool spec_from_json( const json::value& v, spec& out, std::string& error, const std::map<std::string, std::vector<uint8_t>>* blobs )
	{
		if ( !only_keys( v, { "spec", "name", "config", "code", "regions", "state", "stop", "syscall", "external", "auto_stack", "trace", "dump_mem", "expect", "reference" },
						 "spec", error ) )
			return false;
		if ( const json::value* x = v.find( "spec" ) )
		{
			uint64_t ver = 0;
			if ( !get_u( *x, ver, "spec", error ) ) return false;
			if ( ver != 1 ) { error = "spec: version " + std::to_string( ver ) + " (this emu-snippet reads 1)"; return false; }
		}
		if ( const json::value* x = v.find( "name" ) )
			if ( !get_str( *x, out.name, "name", error ) ) return false;
		if ( const json::value* c = v.find( "config" ) )
		{
			if ( !only_keys( *c, { "profile", "cpuid_file", "strict", "cpu_model", "avx512", "amx", "avx10", "apx", "x86_features", "rdrand", "rdrand_seed", "smm_revision",
								   "feature_control", "xcr0", "cr0", "xinuse_model", "tsc_start", "tsc_per_insn" },
							 "config", error ) )
				return false;
			engine_config& g = out.config;
			if ( const json::value* p = c->find( "profile" ) )
			{
				std::string t;
				if ( !get_str( *p, t, "config.profile", error ) ) return false;
				if ( t == "host" ) g.profile = cpu_profile_choice::host;
				else if ( t == "guest" ) g.profile = cpu_profile_choice::guest;
				else if ( t == "max" ) g.profile = cpu_profile_choice::max;
				else { error = "config.profile: host, guest or max"; return false; }
			}
			if ( const json::value* p = c->find( "cpuid_file" ) )
				if ( !get_str( *p, g.cpuid_file, "config.cpuid_file", error ) ) return false;
			if ( const json::value* p = c->find( "rdrand" ) )
			{
				std::string t;
				if ( !get_str( *p, t, "config.rdrand", error ) ) return false;
				if ( t == "seeded" ) g.rng = rng_source::seeded;
				else if ( t == "host" ) g.rng = rng_source::host_seed;
				else { error = "config.rdrand: seeded or host"; return false; }
			}
			struct ifield { const char* k; int* dst; int64_t lo, hi; } ints[] = {
				{ "strict", &g.strict, -1, 1 }, { "cpu_model", &g.cpu_model, 0, UC_CPU_X86_ENDING - 1 }, { "avx512", &g.avx512, 0, INT32_MAX },
				{ "amx", &g.amx, 0, INT32_MAX }, { "avx10", &g.avx10, 0, INT32_MAX }, { "apx", &g.apx, 0, INT32_MAX }, { "x86_features", &g.x86_features, 0, INT32_MAX },
				{ "xinuse_model", &g.xinuse_model, 0, 16 } };
			for ( auto& f : ints )
				if ( const json::value* p = c->find( f.k ) )
				{
					int64_t x = 0;
					if ( !get_i( *p, x, std::string( "config." ) + f.k, error ) ) return false;
					if ( x < f.lo || x > f.hi ) { error = std::string( "config." ) + f.k + " out of range"; return false; }
					*f.dst = int( x );
				}
			struct ufield { const char* k; uint64_t* dst; } u64s[] = { { "rdrand_seed", &g.rng_seed }, { "feature_control", &g.feature_control }, { "xcr0", &g.xcr0 }, { "cr0", &g.cr0 },
				// U2096 (plan 3.M6): the virtual time base - the TSC before the snippet's first instruction (emu-alltest exports its engine's
				// clock at the snippet start) and the ticks per instruction
				{ "tsc_start", &g.tsc_start }, { "tsc_per_insn", &g.tsc_per_insn } };
			for ( auto& f : u64s )
				if ( const json::value* p = c->find( f.k ) )
					if ( !get_u( *p, *f.dst, std::string( "config." ) + f.k, error ) ) return false;
			if ( const json::value* p = c->find( "smm_revision" ) )
			{
				uint64_t x = 0;
				if ( !get_u( *p, x, "config.smm_revision", error ) ) return false;
				if ( x > 0xFFFFFFFFull ) { error = "config.smm_revision: 32-bit"; return false; }
				g.smm_revision = uint32_t( x );
			}
			if ( !g.tsc_per_insn ) { error = "config.tsc_per_insn: at least 1 (the virtual time base must advance)"; return false; }
		}
		if ( const json::value* c = v.find( "code" ) )
		{
			if ( !only_keys( *c, { "address", "hex", "asm", "prot" }, "code", error ) ) return false;
			if ( const json::value* a = c->find( "address" ) )
				if ( !get_u( *a, out.code_address, "code.address", error ) ) return false;
			const json::value* h = c->find( "hex" );
			const json::value* a = c->find( "asm" );
			if ( ( h != nullptr ) == ( a != nullptr ) ) { error = "code: exactly one of \"hex\" / \"asm\""; return false; }
			std::string t;
			if ( h )
			{
				if ( !get_str( *h, t, "code.hex", error ) ) return false;
				std::string why;
				if ( !parse_hex_bytes( t, out.code, why ) ) { error = "code.hex: " + why; return false; }
			}
			else
			{
				if ( !get_str( *a, t, "code.asm", error ) ) return false;
				if ( !assemble( t, out.code_address, out.code, error ) ) { error = "code.asm: " + error; return false; }
			}
			if ( const json::value* p = c->find( "prot" ) )
			{
				std::string pt;
				if ( !get_str( *p, pt, "code.prot", error ) ) return false;
				region tmp;
				if ( !parse_map( "0:0:" + pt, tmp, error ) ) { error = "code.prot: " + error; return false; }
				out.code_prot = tmp.prot;
			}
		}
		if ( const json::value* rs = v.find( "regions" ) )
		{
			if ( !rs->is( json::kind::array ) ) { error = "regions: an array"; return false; }
			for ( auto& it : rs->items )
			{
				if ( !only_keys( it, { "address", "size", "prot", "name", "data" }, "regions[]", error ) ) return false;
				region rg;
				const json::value* a = it.find( "address" );
				const json::value* s = it.find( "size" );
				if ( !a || !s || !get_u( *a, rg.base, "regions[].address", error ) || !get_u( *s, rg.size, "regions[].size", error ) )
				{
					if ( error.empty() ) error = "regions[]: address and size";
					return false;
				}
				if ( const json::value* p = it.find( "prot" ) )
				{
					std::string pt;
					if ( !get_str( *p, pt, "regions[].prot", error ) ) return false;
					region tmp;
					if ( !parse_map( "0:0:" + pt, tmp, error ) ) { error = "regions[].prot: " + error; return false; }
					rg.prot = tmp.prot;
				}
				rg.name = "region " + hex( rg.base );
				if ( const json::value* n = it.find( "name" ) )
					if ( !get_str( *n, rg.name, "regions[].name", error ) ) return false;
				if ( const json::value* d = it.find( "data" ) )
				{
					if ( !d->is( json::kind::array ) ) { error = "regions[].data: an array"; return false; }
					for ( auto& di : d->items )
					{
						mem_bytes mb;
						if ( !mem_from_json( di, mb, blobs, rg.base, "regions[].data[]", error ) ) return false;
						if ( mb.address < rg.base || mb.address - rg.base + mb.bytes.size() > rg.size ) { error = "regions[].data[]: outside its region " + rg.name; return false; }
						rg.data.push_back( mb );
					}
				}
				out.regions.push_back( rg );
			}
		}
		if ( const json::value* st = v.find( "state" ) )
		{
			if ( !st->is( json::kind::object ) ) { error = "state: an object"; return false; }
			for ( size_t i = 0; i < st->keys.size(); i++ )
			{
				const std::string& k = st->keys[ i ];
				const json::value& x = st->items[ i ];
				if ( k == "msr" )
				{
					if ( !x.is( json::kind::object ) ) { error = "state.msr: an object {\"0xINDEX\": VALUE}"; return false; }
					for ( size_t m = 0; m < x.keys.size(); m++ )
					{
						uint64_t idx = 0, val = 0;
						if ( !parse_u64( x.keys[ m ], idx ) || idx > 0xFFFFFFFFull ) { error = "state.msr: bad index \"" + x.keys[ m ] + "\""; return false; }
						if ( !get_u( x.items[ m ], val, "state.msr." + x.keys[ m ], error ) ) return false;
						out.msrs.push_back( { uint32_t( idx ), val } );
					}
					continue;
				}
				if ( k == "cpl3" )
				{
					if ( !x.is( json::kind::boolean ) ) { error = "state.cpl3: true or false"; return false; }
					out.cpl3 = x.b;
					continue;
				}
				field_value fv;
				if ( !field_from_json( k, x, fv, error ) ) { error = "state." + error; return false; }
				out.regs.push_back( fv );
			}
		}
		if ( const json::value* s = v.find( "stop" ) )
		{
			if ( !only_keys( *s, { "address", "max_insns", "ret_depth" }, "stop", error ) ) return false;
			if ( const json::value* a = s->find( "address" ) )
			{
				if ( !get_u( *a, out.stop_at, "stop.address", error ) ) return false;
				out.stop_at_given = true;
			}
			if ( const json::value* a = s->find( "max_insns" ) )
			{
				if ( !get_u( *a, out.max_insns, "stop.max_insns", error ) ) return false;
				if ( out.max_insns == 0 ) { error = "stop.max_insns: at least 1"; return false; }
			}
			if ( const json::value* a = s->find( "ret_depth" ) )
			{
				if ( !get_u( *a, out.ret_depth, "stop.ret_depth", error ) ) return false;
				out.ret_depth_given = true;
			}
		}
		for ( auto [ key, which ] : std::initializer_list<std::pair<const char*, int>>{ { "syscall", 0 }, { "external", 1 } } )
			if ( const json::value* p = v.find( key ) )
			{
				std::string t;
				if ( !get_str( *p, t, key, error ) ) return false;
				if ( which == 0 )
				{
					if ( t == "stop" ) out.syscall = syscall_policy::stop;
					else if ( t == "cpu" ) out.syscall = syscall_policy::cpu;
					else { error = "syscall: stop or cpu"; return false; }
				}
				else
				{
					if ( t == "stop" ) out.external = external_policy::stop;
					else if ( t == "fault" ) out.external = external_policy::fault;
					else { error = "external: stop or fault"; return false; }
				}
			}
		for ( auto [ key, dst ] : std::initializer_list<std::pair<const char*, bool*>>{ { "auto_stack", &out.auto_stack }, { "trace", &out.trace } } )
			if ( const json::value* p = v.find( key ) )
			{
				if ( !p->is( json::kind::boolean ) ) { error = std::string( key ) + ": true or false"; return false; }
				*dst = p->b;
			}
		if ( const json::value* d = v.find( "dump_mem" ) )
		{
			if ( !d->is( json::kind::array ) ) { error = "dump_mem: an array"; return false; }
			for ( auto& it : d->items )
			{
				range r;
				if ( !range_from_json( it, r, "dump_mem[]", error ) ) return false;
				out.dump_mem.push_back( r );
			}
		}
		if ( const json::value* e = v.find( "expect" ) )
			if ( !checks_from_json( *e, out.expect, false, blobs, error ) ) return false;
		if ( const json::value* e = v.find( "reference" ) )
			if ( !checks_from_json( *e, out.reference, true, blobs, error ) ) return false;
		return true;
	}

	// ------------------------------------------------------------------------------------------------- run
	bool run( const spec& s, result& r )
	{
		r = result{};
		auto fail = [ & ]( const std::string& e ) {
			r.error = e;
			r.reason = stop_reason::error;
			return false;
		};
		if ( s.code.empty() ) return fail( "no code" );
		if ( s.code.size() > max_code_bytes ) return fail( "code larger than " + std::to_string( max_code_bytes ) + " bytes" );
		if ( s.code_address + ( s.code.size() - 1 ) < s.code_address ) return fail( "code wraps around 2^64" );
		const bool apx = s.config.apx != 0;
		for ( auto& f : s.regs )
			if ( is_apx_name( f.name ) && !apx ) return fail( f.name + " needs the APX opt-in (config.apx / --apx)" );

		std::string err;
		auto e = engine::open( s.config, err );
		if ( !e ) return fail( "engine: " + err );
		for ( auto& l : e->log() ) r.log.push_back( l );
		uc_engine* uc = e->handle();

		// memory: the given regions, the code (in a region or its own), the automatic stack
		for ( auto& rg : s.regions )
		{
			if ( !e->map( rg.base, rg.size, rg.prot, rg.name, err ) ) return fail( err );
			for ( auto& d : rg.data )
				if ( !e->write( d.address, d.bytes.data(), d.bytes.size(), err ) ) return fail( rg.name + ": " + err );
		}
		const uint64_t code_last = s.code_address + ( s.code.size() - 1 );
		bool code_in = false;
		for ( auto& rg : e->regions() )
		{
			const uint64_t last = rg.base + ( rg.size - 1 );
			if ( s.code_address >= rg.base && code_last <= last ) code_in = true;
			else if ( s.code_address <= last && rg.base <= code_last ) return fail( "the code [" + hex( s.code_address ) + ", " + hex( code_last ) + "] crosses the edge of " + rg.name );
		}
		if ( !code_in )
		{
			const uint64_t base = s.code_address & ~( page_size - 1 );
			const uint64_t end = ( code_last | ( page_size - 1 ) );
			if ( !e->map( base, end - base + 1, s.code_prot, "code", err ) ) return fail( err );
		}
		if ( !e->write( s.code_address, s.code.data(), s.code.size(), err ) ) return fail( "code: " + err );

		// the desired state: the engine's reset values with the spec's registers merged in
		std::map<std::string, std::vector<uint8_t>> desired;
		if ( !read_fields( uc, apx, desired, err ) ) return fail( "reset state: " + err );
		std::set<std::string> given;
		for ( auto& f : s.regs )
		{
			if ( f.name == "xcr0" || f.name == "fptag" || f.name == "ds" || f.name == "es" || f.name == "fs" || f.name == "gs" )
				return fail( f.name + " cannot be set through the state (xcr0: config.xcr0; fptag: ftw; ds / es / fs / gs: the environment, milestone M4)" );
			if ( ( f.name == "cs" || f.name == "ss" ) && !s.cpl3 ) return fail( f.name + " is set only with state.cpl3 (entered by IRETQ)" );
			merge( desired[ f.name ], f );
			given.insert( f.name );
		}
		if ( s.cpl3 && ( !given.count( "cs" ) || !given.count( "ss" ) || !given.count( "gdtr_base" ) ) )
			return fail( "state.cpl3 needs cs, ss and gdtr_base (+ gdtr_limit) with a GDT in mapped memory" );
		// st given without ftw: tag those registers valid (physical = (TOP + i) & 7)
		if ( !given.count( "ftw" ) )
		{
			const unsigned top = unsigned( le( desired[ "fpsw" ] ) >> 11 ) & 7;
			for ( int i = 0; i < 8; i++ )
				if ( given.count( "st" + std::to_string( i ) ) ) desired[ "ftw" ][ 0 ] |= uint8_t( 1u << ( ( top + unsigned( i ) ) & 7 ) );
		}
		uint64_t start_rip = s.code_address;
		if ( given.count( "rip" ) ) start_rip = le( desired[ "rip" ] );
		desired[ "rip" ] = le_bytes( start_rip, 8 );
		if ( s.auto_stack && !given.count( "rsp" ) )
		{
			const uint64_t base = default_stack_top - default_stack_size;
			if ( !e->map( base, default_stack_size, UC_PROT_READ | UC_PROT_WRITE, "stack", err ) ) return fail( err );
			const uint64_t rsp = default_stack_top - 0x48;   // RSP = 8 (mod 16) as at a function's first instruction
			if ( !e->write( rsp, &return_sentinel, 8, err ) ) return fail( err );
			desired[ "rsp" ] = le_bytes( rsp, 8 );
			r.log.push_back( "automatic stack [" + hex( base ) + ", " + hex( default_stack_top - 1 ) + "], rsp " + hex( rsp ) + ", [rsp] = the return sentinel " + hex( return_sentinel ) );
		}

		// MSRs and GDTR before the loader (IRETQ to CPL3 reads the GDT)
		for ( auto& m : s.msrs )
		{
			uc_x86_msr x{ m.first, m.second };
			const uc_err ue = uc_reg_write( uc, UC_X86_REG_MSR, &x );
			if ( ue != UC_ERR_OK ) return fail( "MSR " + hex( m.first ) + " <- " + hex( m.second ) + ": " + uc_strerror( ue ) );
			r.log.push_back( "MSR " + hex( m.first ) + " written" );
		}
		if ( given.count( "gdtr_base" ) || given.count( "gdtr_limit" ) )
		{
			uc_x86_mmr g{};
			g.base = le( desired[ "gdtr_base" ] );
			g.limit = uint32_t( le( desired[ "gdtr_limit" ] ) );
			const uc_err ue = uc_reg_write( uc, UC_X86_REG_GDTR, &g );
			if ( ue != UC_ERR_OK ) return fail( std::string( "GDTR: " ) + uc_strerror( ue ) );
		}
		if ( !run_loader( *e, s, desired, start_rip, r, err ) ) return fail( err );

		// the rest through the API
		for ( int i = 0; i < 16; i++ )
			if ( !write_scalar( uc, gpr_ids[ i ], le( desired[ gpr_names[ i ] ] ), 8, gpr_names[ i ], err ) ) return fail( err );
		if ( apx )
			for ( int i = 0; i < 16; i++ )
				if ( !write_scalar( uc, UC_X86_REG_R16 + i, le( desired[ "r" + std::to_string( 16 + i ) ] ), 8, "r" + std::to_string( 16 + i ), err ) ) return fail( err );
		if ( !write_scalar( uc, UC_X86_REG_RIP, start_rip, 8, "rip", err ) ) return fail( err );
		if ( !s.cpl3 && !write_scalar( uc, UC_X86_REG_RFLAGS, le( desired[ "rflags" ] ), 8, "rflags", err ) ) return fail( err );
		if ( !write_scalar( uc, UC_X86_REG_FS_BASE, le( desired[ "fs_base" ] ), 8, "fs_base", err ) ) return fail( err );
		if ( !write_scalar( uc, UC_X86_REG_GS_BASE, le( desired[ "gs_base" ] ), 8, "gs_base", err ) ) return fail( err );
		{
			std::map<std::string, std::vector<uint8_t>> now;
			if ( !read_fields( uc, apx, now, err ) ) return fail( err );
			for ( int i = 0; i < 32; i++ )
			{
				const std::string n = "zmm" + std::to_string( i );
				if ( now[ n ] != desired[ n ] && !write_bytes( uc, UC_X86_REG_ZMM0 + i, desired[ n ], n, err ) ) return fail( err );
			}
		}
		for ( int i = 0; i < 8; i++ )
			if ( !write_scalar( uc, UC_X86_REG_K0 + i, le( desired[ "k" + std::to_string( i ) ] ), 8, "k" + std::to_string( i ), err ) ) return fail( err );
		for ( auto n : { "fcs", "fds" } )
			if ( given.count( n ) && !write_scalar( uc, std::string( n ) == "fcs" ? UC_X86_REG_FCS : UC_X86_REG_FDS, le( desired[ n ] ), 2, n, err ) ) return fail( err );
		for ( int i : dr_index )
		{
			const std::string n = "dr" + std::to_string( i );
			if ( given.count( n ) && i != 7 && !write_scalar( uc, UC_X86_REG_DR0 + i, le( desired[ n ] ), 8, n, err ) ) return fail( err );
		}
		if ( given.count( "dr7" ) && !write_scalar( uc, UC_X86_REG_DR7, le( desired[ "dr7" ] ), 8, "dr7", err ) ) return fail( err );
		if ( given.count( "cr0" ) && !write_scalar( uc, UC_X86_REG_CR0, le( desired[ "cr0" ] ), 8, "cr0 (refused like MOV to CR0 would #GP, U1962)", err ) ) return fail( err );
		if ( given.count( "cr4" ) && !write_scalar( uc, UC_X86_REG_CR4, le( desired[ "cr4" ] ), 8, "cr4 (refused like MOV to CR4 would #GP, U1962)", err ) ) return fail( err );

		if ( !read_fields( uc, apx, r.start, err ) ) return fail( "start state: " + err );
		// the start bytes of the strict expectation's memory ranges
		std::vector<std::pair<range, std::vector<uint8_t>>> start_mem;
		for ( auto* c : { &s.expect, &s.reference } )
			for ( auto& cr : c->checked_mem )
			{
				std::vector<uint8_t> b( size_t( cr.size ) );
				if ( !e->read( cr.address, b.data(), cr.size, err ) ) return fail( "checked memory: " + err );
				start_mem.push_back( { cr, b } );
			}

		// hooks
		disassembler dis;
		ctx c;
		c.uc = uc;
		c.s = &s;
		c.r = &r;
		c.dis = &dis;
		c.apx = apx;
		c.per_insn = s.trace || s.ret_depth_given || s.syscall == syscall_policy::stop;
		if ( c.per_insn && !dis.ok() ) return fail( "Capstone did not open" );
		uc_hook h;
		if ( uc_hook_add( uc, &h, UC_HOOK_INTR, (void*)on_intr, &c, 1, 0 ) != UC_ERR_OK ||
			 uc_hook_add( uc, &h, UC_HOOK_INSN_INVALID, (void*)on_invalid, &c, 1, 0 ) != UC_ERR_OK ||
			 uc_hook_add( uc, &h, UC_HOOK_MEM_UNMAPPED | UC_HOOK_MEM_PROT, (void*)on_bad_mem, &c, 1, 0 ) != UC_ERR_OK )
			return fail( "uc_hook_add failed" );
		if ( c.per_insn && uc_hook_add( uc, &h, UC_HOOK_CODE, (void*)on_code, &c, 1, 0 ) != UC_ERR_OK ) return fail( "uc_hook_add (code) failed" );
		if ( s.trace )
		{
			if ( uc_hook_add( uc, &h, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, (void*)on_mem, &c, 1, 0 ) != UC_ERR_OK ) return fail( "uc_hook_add (memory trace) failed" );
			c.prev = r.start;
		}
		r.counted = c.per_insn;

		const uint64_t until = s.stop_at_given ? s.stop_at : s.code_address + s.code.size();
		// U2096 (plan 3.M6): the virtual time base restarts at the snippet's first instruction - the state loader's own instructions above
		// are not part of the case (emu-alltest exports its clock before that instruction: config.tsc_start)
		if ( uc_ctl_set_x86_vclock( uc, UC_X86_VCLOCK_VIRTUAL, e->config().tsc_start, e->config().tsc_per_insn ) != UC_ERR_OK )
			return fail( "UC_CTL_X86_VCLOCK (the clock at the snippet start) failed" );
		r.ran = true;
		const uc_err ue = e->emulate( start_rip, until, s.max_insns );
		if ( s.trace ) finish_trace_entry( c );
		uint64_t rip = 0;
		uc_reg_read( uc, UC_X86_REG_RIP, &rip );
		if ( r.reason == stop_reason::none )
		{
			if ( ue != UC_ERR_OK ) { r.reason = stop_reason::error; r.error = std::string( "uc_emu_start: " ) + uc_strerror( ue ); }
			else if ( rip == until ) r.reason = stop_reason::end;
			else r.reason = stop_reason::count;
			r.rip = rip;
		}
		if ( r.reason == stop_reason::syscall )
		{
			// stopped in the hook before the instruction: RIP is the instruction's address
			uint64_t at = r.rip;
			uc_reg_write( uc, UC_X86_REG_RIP, &at );
		}
		for ( auto& l : e->log() )
			if ( l.rfind( "emulate", 0 ) == 0 ) r.log.push_back( l );
		if ( !read_fields( uc, apx, r.final, err ) ) return fail( "final state: " + err );
		for ( auto& d : s.dump_mem )
		{
			std::vector<uint8_t> b( size_t( d.size ) );
			if ( !e->read( d.address, b.data(), d.size, err ) ) r.log.push_back( "dump [" + hex( d.address ) + "]: " + err );
			else r.dumps.push_back( { d, b } );
		}
		if ( s.expect.present )
		{
			r.expect_checked = true;
			run_checks( s.expect, s, *e, r, start_mem, apx, r.expect_diffs );
			r.expect_ok = r.expect_diffs.empty();
		}
		if ( s.reference.present )
		{
			r.reference_checked = true;
			run_checks( s.reference, s, *e, r, start_mem, apx, r.reference_diffs );
			r.reference_ok = r.reference_diffs.empty();
		}
		return r.reason != stop_reason::error;
	}

	std::vector<std::string> format_result( const spec& s, const result& r, bool with_states )
	{
		std::vector<std::string> o;
		std::string stop = std::string( "stop: " ) + name_of( r.reason );
		switch ( r.reason )
		{
		case stop_reason::fault:
			stop += " " + fault_text( r.vector, r.error_code ) + " at rip " + hex( r.rip );
			if ( r.vector == 14 && r.pf_access >= 0 )
				stop += std::string( " (" ) + ( r.pf_access == 8 ? "execute" : r.pf_access == 1 ? "write" : "read" ) + " access to " + hex( r.pf_address ) + ")";
			break;
		case stop_reason::syscall: stop += " (" + r.syscall_kind + ", rax " + hex( r.syscall_number ) + ") at rip " + hex( r.rip ) + ": stopped before it, not executed, nothing forwarded to the host"; break;
		case stop_reason::external: stop += " to " + hex( r.external_target ) + " (outside the executable mapped memory): stopped, not followed"; break;
		case stop_reason::error: stop += ": " + r.error; break;
		default: stop += " at rip " + hex( r.rip ); break;
		}
		if ( r.counted ) stop += ", " + std::to_string( r.insns ) + " instruction(s)";
		o.push_back( stop );
		if ( r.expect_checked )
		{
			o.push_back( std::string( "expect: " ) + ( r.expect_ok ? "PASS" : "DIFF (" + std::to_string( r.expect_diffs.size() ) + ")" ) + ( s.expect.strict ? " [strict]" : " [loose]" ) );
			for ( auto& d : r.expect_diffs ) o.push_back( "  expect differs: " + d );
		}
		if ( r.reference_checked )
		{
			o.push_back( s.reference.label + ": " + ( r.reference_ok ? "SAME" : "DIFF (" + std::to_string( r.reference_diffs.size() ) + ")" ) );
			for ( auto& d : r.reference_diffs ) o.push_back( "  reference differs: " + d );
		}
		for ( auto& t : r.trace )
		{
			o.push_back( "trace " + hex( t.address ) + "  " + hexbytes( t.bytes, t.bytes.size() ) + "  " + t.text );
			for ( auto& c : t.changes ) o.push_back( "      " + c );
			for ( auto& m : t.mem ) o.push_back( "      " + m );
		}
		if ( with_states && !r.final.empty() )
		{
			o.push_back( "final state:" );
			for ( auto& n : field_names( s.config.apx != 0 ) )
			{
				auto it = r.final.find( n );
				if ( it == r.final.end() ) continue;
				std::string l = "  " + n + " = " + format_field( n, it->second );
				auto st = r.start.find( n );
				if ( st != r.start.end() && st->second != it->second ) l += "   (start " + format_field( n, st->second ) + ")";
				o.push_back( l );
			}
			for ( auto& d : r.dumps ) o.push_back( "  memory [" + hex( d.first.address ) + ", +" + hex( d.first.size ) + ") = " + hexbytes( d.second ) );
		}
		return o;
	}
}
