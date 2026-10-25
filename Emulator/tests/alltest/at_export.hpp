// emu-alltest --snippet-export FILE (plan 3.M3, ledger U1977): every case line Unicorn ran becomes one emu-snippet spec (JSON,
// one per line; emu-snippet --batch FILE replays them on the Emulator lib, Emulator/include/emu/snippet.hpp):
//   config    the Unicorn engine's settings (CPU model UC_CPU_X86_MAX, the CPUID profile FILE, strict, opt-ins, RNG source,
//             platform SMM revision / IA32_FEATURE_CONTROL, XCR0, CR0 without TS - the snippet's TS comes with the state -,
//             XINUSE model), so emu-snippet opens the same machine through emu::engine
//   regions   CODE (the thunk: prologue and epilogue as "blob" lines written once), DATA, MEM and, with cpl=3, the system page
//             (GDT, stubs), with their bytes AT THE SNIPPET'S FIRST INSTRUCTION (start_capture, at_engine.hpp)
//   state     every register at that instruction (CPL3: CS / SS / GDTR, entered by emu-snippet's IRETQ; IA32_EFER)
//   stop      the first epilogue byte (= where the thunk takes over), at most 1000000 instructions; SYSCALL / SYSENTER / INT n
//             run on the emulated CPU ("syscall": "cpu") and a fetch outside the executable memory is a #PF ("external":
//             "fault"), as in this harness
//   expect    (expected-value lines) the "=>" / "=>!" expectation: strict / loose, the fault (vector, error code), every listed
//             field with its byte mask (in snippet field names: zmmN = XMM + YMMH + ZMMH bytes), the listed MEM bytes, and in
//             strict mode the whole MEM region as the case expects it (its input + the listed bytes: "full_mem") - not the bytes at
//             the snippet start, where the prologue's PUSH / POPFQ of RFLAGS left a value below a test stack inside MEM
//   reference (every line whose thunk ran to its end) this engine's final state: GPRs, RFLAGS, FCW, FSW, FTW, MXCSR, ST0-7,
//             FOP, FIP, FDP, the full tag word (FNSTENV), XMM / YMM ("ymmN"; zmmN + K + R16-R31 for expected-value lines, which
//             move that state), all-zero vectors by name ("zero"), the whole MEM region, the fault
// Harness rules carried as masks (the state image this thunk saves, not CPU state): the epilogue's PUSHFQ stores RF = VM = 0;
// the fault path clears TF before it runs the epilogue (resume_if_in_snippet / the #PF path). Not compared: the stack (as the
// "=>" lines), MXCSR_MASK (a constant of the CPU), the FNSTENV FCS / FDS (their saved value depends on the FCS / FDS
// deprecation CPUID bit, not on register state).
#pragma once
#include "at_engine.hpp"
#include <cctype>
#include <fstream>
#include <set>
#include <string>
#include <tuple>

namespace at
{
	struct snippet_exporter
	{
		std::ofstream f;
		bool open = false;
		std::set<std::string> blobs;
		size_t records = 0, skipped = 0, no_reference = 0, no_reference_paging = 0;
		std::string path;
	};

	inline std::string jx_quote( const std::string& s )
	{
		std::string o = "\"";
		for ( unsigned char c : s )
		{
			if ( c == '"' ) o += "\\\"";
			else if ( c == '\\' ) o += "\\\\";
			else if ( c < 0x20 || c >= 0x7F ) { char b[ 8 ]; std::snprintf( b, sizeof( b ), "\\u%04x", c ); o += b; }
			else o += char( c );
		}
		return o + "\"";
	}
	inline std::string jx_hexs( const uint8_t* p, size_t n )
	{
		static const char* d = "0123456789abcdef";
		std::string s;
		s.reserve( 2 * n );
		for ( size_t i = 0; i < n; i++ ) { s += d[ p[ i ] >> 4 ]; s += d[ p[ i ] & 15 ]; }
		return s;
	}
	inline std::string jx_u64( uint64_t v ) { return "\"" + hx( v ) + "\""; }
	inline uint64_t jx_le( const uint8_t* p, size_t n )
	{
		uint64_t v = 0;
		for ( size_t i = 0; i < n && i < 8; i++ ) v |= uint64_t( p[ i ] ) << ( 8 * i );
		return v;
	}

	// sparse runs of the non-zero bytes of [p, p + n) at address base (zero gaps shorter than 16 bytes are kept inside a run)
	inline std::string jx_runs( const uint8_t* p, size_t n, uint64_t base )
	{
		std::string o;
		size_t i = 0;
		while ( i < n )
		{
			if ( !p[ i ] ) { i++; continue; }
			size_t e = i, z = 0;
			while ( e < n && z < 16 ) { z = p[ e ] ? 0 : z + 1; e++; }
			e -= z;
			o += std::string( o.empty() ? "" : "," ) + "{\"address\":" + jx_u64( base + i ) + ",\"hex\":\"" + jx_hexs( p + i, e - i ) + "\"}";
			i = e;
		}
		return o;
	}

	// one field: scalars as {"value", "mask"} / "0x..", byte fields as {"hex", "mask"} / "hex"
	inline void jx_field( std::string& o, const std::string& name, const uint8_t* v, size_t n, const uint8_t* mask, bool scalar )
	{
		bool full = true;
		if ( mask )
			for ( size_t i = 0; i < n; i++ ) if ( mask[ i ] != 0xFF ) full = false;
		o += std::string( o.back() == '{' ? "" : "," ) + jx_quote( name ) + ":";
		const std::string val = scalar ? jx_u64( jx_le( v, n ) ) : "\"" + jx_hexs( v, n ) + "\"";
		if ( full ) { o += val; return; }
		const std::string msk = scalar ? jx_u64( jx_le( mask, n ) ) : "\"" + jx_hexs( mask, n ) + "\"";
		o += std::string( "{\"" ) + ( scalar ? "value" : "hex" ) + "\":" + val + ",\"mask\":" + msk + "}";
	}

	// the snippet fields of a thunk state image: (name, offset in 'state' parts) - zmmN gathers XMM (FXSAVE), YMMH, ZMMH / ZMM16-31
	struct jx_view { std::string name; std::vector<size_t> off; bool scalar; };
	inline std::vector<jx_view> jx_views( bool apx )
	{
		std::vector<jx_view> v;
		const size_t fx = offsetof( state, fx );
		for ( int i = 0; i < 16; i++ ) { jx_view x{ reg_name( i ), {}, true }; for ( size_t k = 0; k < 8; k++ ) x.off.push_back( offsetof( state, gpr ) + 8 * i + k ); v.push_back( x ); }
		{ jx_view x{ "rflags", {}, true }; for ( size_t k = 0; k < 8; k++ ) x.off.push_back( offsetof( state, rflags ) + k ); v.push_back( x ); }
		auto fxf = [ & ]( const char* n, size_t o, size_t len, bool sc ) { jx_view x{ n, {}, sc }; for ( size_t k = 0; k < len; k++ ) x.off.push_back( fx + o + k ); v.push_back( x ); };
		fxf( "fpcw", 0, 2, true );
		fxf( "fpsw", 2, 2, true );
		fxf( "ftw", 4, 1, true );
		fxf( "mxcsr", 24, 4, true );
		for ( int i = 0; i < 8; i++ ) { jx_view x{ "st" + std::to_string( i ), {}, false }; for ( size_t k = 0; k < 10; k++ ) x.off.push_back( fx + 32 + 16 * i + k ); v.push_back( x ); }
		for ( int i = 0; i < 32; i++ )
		{
			jx_view x{ "zmm" + std::to_string( i ), {}, false };
			if ( i < 16 )
			{
				for ( size_t k = 0; k < 16; k++ ) x.off.push_back( fx + 160 + 16 * i + k );
				for ( size_t k = 0; k < 16; k++ ) x.off.push_back( offsetof( state, ymmh ) + 16 * i + k );
				for ( size_t k = 0; k < 32; k++ ) x.off.push_back( offsetof( state, zmmh ) + 32 * i + k );
			}
			else
				for ( size_t k = 0; k < 64; k++ ) x.off.push_back( offsetof( state, zmmx ) + 64 * ( i - 16 ) + k );
			v.push_back( x );
		}
		for ( int i = 0; i < 8; i++ ) { jx_view x{ "k" + std::to_string( i ), {}, true }; for ( size_t k = 0; k < 8; k++ ) x.off.push_back( offsetof( state, k ) + 8 * i + k ); v.push_back( x ); }
		if ( apx )
			for ( int i = 0; i < 16; i++ ) { jx_view x{ "r" + std::to_string( 16 + i ), {}, true }; for ( size_t k = 0; k < 8; k++ ) x.off.push_back( offsetof( state, egpr ) + 8 * i + k ); v.push_back( x ); }
		return v;
	}

	inline std::string jx_fault( bool faulted, int vector, int64_t ec, int access )
	{
		if ( !faulted ) return "{\"vector\":-1}";
		std::string o = "{\"vector\":" + std::to_string( vector );
		if ( ec >= 0 ) o += ",\"error_code\":" + std::to_string( ec );
		if ( vector == 14 && access >= 0 ) o += ",\"pf_access\":" + std::to_string( access );
		return o + "}";
	}

	// U1977: one record (+ the blob lines it needs); false = not exported (counted as skipped)
	inline bool export_case( snippet_exporter& x, const std::string& name, const std::string& cpuid_path, const unicorn_engine& uc, const program& p, const start_capture& cap,
							 const result& u, bool expect, bool loose, const state* st_exp, const std::vector<uint8_t>* listed, bool exp_fault, int exp_vector,
							 int64_t exp_ec, bool exp_dr6_set, uint64_t exp_dr6 )
	{
		if ( !x.open || !cap.done || !u.err.empty() || p.ctl ) return false;
		std::string o = "{\"spec\":1,\"name\":" + jx_quote( name ) + ",\"config\":{\"profile\":\"max\"";
		if ( !uc.cpuid.empty() ) o += ",\"cpuid_file\":" + jx_quote( cpuid_path );
		if ( uc.strict >= 0 ) o += ",\"strict\":" + std::to_string( uc.strict );
		if ( uc.avx512 ) o += ",\"avx512\":" + std::to_string( uc.avx512 );
		if ( uc.amx ) o += ",\"amx\":" + std::to_string( uc.amx );
		if ( uc.avx10 ) o += ",\"avx10\":" + std::to_string( uc.avx10 );
		if ( uc.apx ) o += ",\"apx\":" + std::to_string( uc.apx );
		if ( uc.x86_features ) o += ",\"x86_features\":" + std::to_string( uc.x86_features );
		o += std::string( ",\"rdrand\":\"" ) + ( uc.rdrand == UC_X86_RDRAND_HOST ? "host" : "seeded" ) + "\",\"rdrand_seed\":" + jx_u64( uc.rdrand_seed );
		if ( uc.smm_revision ) o += ",\"smm_revision\":" + jx_u64( uc.smm_revision );
		if ( uc.feature_control && !uc.cpuid.empty() && uc.strict != 0 && !uc.apx ) o += ",\"feature_control\":" + jx_u64( uc.feature_control );
		if ( uc.xcr0 ) o += ",\"xcr0\":" + jx_u64( uc.xcr0 );
		if ( uc.cr0 ) o += ",\"cr0\":" + jx_u64( uc.ts_hooks ? ( uc.cr0 & ~unicorn_engine::CR0_TS ) : uc.cr0 );
		if ( uc.xinuse_model ) o += ",\"xinuse_model\":" + std::to_string( uc.xinuse_model );
		o += "}";
		const std::vector<uint8_t> snip( p.code.begin() + std::ptrdiff_t( p.snippet_begin - CODE ), p.code.begin() + std::ptrdiff_t( p.snippet_end - CODE ) );
		o += ",\"code\":{\"address\":" + jx_u64( p.snippet_begin ) + ",\"hex\":\"" + jx_hexs( snip.data(), snip.size() ) + "\"}";
		// CODE: the program (prologue + snippet + epilogue) and zeros - the prologue / epilogue as blobs when the region is exactly that
		std::string blob_lines;
		std::string code_data;
		{
			std::vector<uint8_t> want( CODE_SIZE, 0 );
			std::memcpy( want.data(), p.code.data(), p.code.size() );
			const size_t pro = size_t( p.snippet_begin - CODE ), epi = size_t( p.epilogue_at - CODE );
			if ( want == cap.code )
			{
				const std::string epi_name = "epilogue_" + std::to_string( snip.size() );
				for ( auto [ bn, b, e ] : std::initializer_list<std::tuple<std::string, size_t, size_t>>{ { "prologue", 0, pro }, { epi_name, epi, p.code.size() } } )
					if ( !x.blobs.count( bn ) )
					{
						blob_lines += "{\"blob\":" + jx_quote( bn ) + ",\"hex\":\"" + jx_hexs( p.code.data() + b, e - b ) + "\"}\n";
						x.blobs.insert( bn );
					}
				code_data = "{\"offset\":\"0x0\",\"blob\":\"prologue\"}," "{\"address\":" + jx_u64( p.snippet_begin ) + ",\"hex\":\"" + jx_hexs( snip.data(), snip.size() ) +
							"\"},{\"address\":" + jx_u64( p.epilogue_at ) + ",\"blob\":" + jx_quote( epi_name ) + "}";
			}
			else
				code_data = jx_runs( cap.code.data(), cap.code.size(), CODE );
		}
		o += ",\"regions\":[{\"address\":" + jx_u64( CODE ) + ",\"size\":" + jx_u64( CODE_SIZE ) + ",\"prot\":\"rwx\",\"name\":\"CODE\",\"data\":[" + code_data + "]}";
		o += ",{\"address\":" + jx_u64( DATA ) + ",\"size\":" + jx_u64( DATA_SIZE ) + ",\"prot\":\"rw\",\"name\":\"DATA\",\"data\":[" + jx_runs( cap.data.data(), cap.data.size(), DATA ) + "]}";
		o += ",{\"address\":" + jx_u64( MEM ) + ",\"size\":" + jx_u64( MEM_SIZE ) + ",\"prot\":\"rw\",\"name\":\"MEM\",\"data\":[" + jx_runs( cap.mem.data(), cap.mem.size(), MEM ) + "]}";
		if ( uc.cpl3 )
			o += ",{\"address\":" + jx_u64( SYS_PAGE ) + ",\"size\":" + jx_u64( SYS_SIZE ) + ",\"prot\":\"rwx\",\"name\":\"SYS\",\"data\":[" + jx_runs( cap.sys.data(), cap.sys.size(), SYS_PAGE ) + "]}";
		o += "]";
		// state: every register at the snippet's first instruction (vectors / x87 only when not zero: emu-snippet's reset is zero)
		o += ",\"state\":{";
		for ( auto& [ n, b ] : cap.regs )
		{
			if ( n == "fptag" || n == "rip" ) continue;
			if ( ( n == "cs" || n == "ss" || n == "gdtr_base" || n == "gdtr_limit" ) && !uc.cpl3 ) continue;
			if ( n.rfind( "dr", 0 ) == 0 && !uc.dbg.any ) continue;
			const bool scalar = !( n.rfind( "zmm", 0 ) == 0 || n.rfind( "st", 0 ) == 0 );
			if ( !scalar && std::all_of( b.begin(), b.end(), []( uint8_t c ) { return c == 0; } ) ) continue;
			jx_field( o, n, b.data(), b.size(), nullptr, scalar );   // vectors: all 64 bytes (the start state is exact)
		}
		{
			// the abridged tag from the full tag word (bit i = physical register i not empty)
			auto t = cap.regs.find( "fptag" );
			if ( t != cap.regs.end() )
			{
				const uint64_t tag = jx_le( t->second.data(), 2 );
				uint8_t ftw = 0;
				for ( int i = 0; i < 8; i++ ) if ( ( ( tag >> ( 2 * i ) ) & 3 ) != 3 ) ftw |= uint8_t( 1u << i );
				jx_field( o, "ftw", &ftw, 1, nullptr, true );
			}
		}
		if ( uc.cpl3 ) o += ",\"cpl3\":true,\"msr\":{\"0xc0000080\":" + jx_u64( cap.efer ) + "}";
		o += "}";
		o += ",\"stop\":{\"address\":" + jx_u64( p.snippet_end ) + ",\"max_insns\":1000000},\"syscall\":\"cpu\",\"external\":\"fault\"";
		const uint64_t RF = 1ull << 16, VM = 1ull << 17, TF = 1ull << 8;
		const auto views = jx_views( uc.apx != 0 );
		if ( expect && st_exp && listed )
		{
			const uint8_t* se = ( const uint8_t* ) st_exp;
			o += std::string( ",\"expect\":{\"mode\":\"" ) + ( loose ? "loose" : "strict" ) + "\",\"fault\":" +
				 ( exp_fault ? jx_fault( true, exp_vector, exp_ec, -1 ) : std::string( "{\"vector\":-1}" ) ) + ",\"regs\":{";
			for ( auto& v : views )
			{
				std::vector<uint8_t> val( v.off.size() ), mk( v.off.size() );
				bool any = false;
				for ( size_t k = 0; k < v.off.size(); k++ )
				{
					val[ k ] = se[ v.off[ k ] ];
					mk[ k ] = ( *listed )[ v.off[ k ] ] ? 0xFF : 0;
					any |= mk[ k ] != 0;
				}
				if ( v.name == "rflags" )
				{
					// PUSHFQ stores RF = VM = 0; a fault resumes the epilogue with TF cleared
					if ( !loose ) { any = true; std::fill( mk.begin(), mk.end(), uint8_t( 0xFF ) ); }
					const uint64_t m = jx_le( mk.data(), 8 ) & ~( RF | VM | ( exp_fault ? TF : 0 ) );
					for ( int k = 0; k < 8; k++ ) mk[ size_t( k ) ] = uint8_t( m >> ( 8 * k ) );
				}
				if ( any ) jx_field( o, v.name, val.data(), val.size(), mk.data(), v.scalar );
			}
			if ( exp_dr6_set ) { std::vector<uint8_t> d( 8 ); std::memcpy( d.data(), &exp_dr6, 8 ); jx_field( o, "dr6", d.data(), 8, nullptr, true ); }
			o += "},\"mem\":[";
			std::string mem;
			const size_t mo = offsetof( state, mem );
			for ( size_t i = 0; i < MEM_SIZE; )
			{
				if ( !( *listed )[ mo + i ] ) { i++; continue; }
				size_t e = i;
				while ( e < MEM_SIZE && ( *listed )[ mo + e ] ) e++;
				mem += std::string( mem.empty() ? "" : "," ) + "{\"address\":" + jx_u64( MEM + i ) + ",\"hex\":\"" + jx_hexs( se + mo + i, e - i ) + "\"}";
				i = e;
			}
			// strict: the whole MEM region must equal the expected image (the case input + the listed bytes), not the bytes at the
			// snippet's first instruction - the prologue's PUSH / POPFQ of RFLAGS leaves its value below a test stack inside MEM
			if ( !loose ) mem = jx_runs( se + mo, MEM_SIZE, MEM );
			o += mem + "]";
			if ( !loose ) o += ",\"full_mem\":[{\"address\":" + jx_u64( MEM ) + ",\"size\":" + jx_u64( MEM_SIZE ) + "}]";
			o += "}";
		}
		// reference: this engine's final state - only when Unicorn ran the thunk to its end (else the epilogue did not save it, e.g.
		// an epilogue RET raised #CP after the snippet enabled CET shadow stacks at CPL0)
		// - and not when the snippet left paging on: the epilogue's own stores then go through the snippet's page tables in MEM
		// and set their Accessed / Dirty bits (a harness side effect after the snippet, e.g. cases_vmx_profile_*.txt)
		if ( !u.uc_complete ) x.no_reference++;
		else if ( u.uc_guest_paging ) x.no_reference_paging++;
		else
		{
			const uint8_t* us = ( const uint8_t* ) u.s.get();
			std::string zero;
			o += ",\"reference\":{\"source\":\"emu-alltest Unicorn engine\",\"mode\":\"loose\",\"fault\":" + jx_fault( u.faulted, u.vector, u.error_code, u.ev.access ) + ",\"regs\":{";
			for ( auto& v : views )
			{
				const bool vec_ext = v.name.rfind( "zmm", 0 ) == 0 && std::stoi( v.name.substr( 3 ) ) >= 16;
				const bool apx_reg = v.name.size() == 3 && v.name[ 0 ] == 'r' && std::isdigit( ( unsigned char )v.name[ 1 ] ) && std::stoi( v.name.substr( 1 ) ) >= 16;
				if ( ( vec_ext || v.name[ 0 ] == 'k' || apx_reg ) && !uc.ext_regs ) continue;   // AVX-512 / APX state: moved by the hooks of expected-value lines only
				std::vector<uint8_t> val( v.off.size() ), mk( v.off.size(), 0xFF );
				for ( size_t k = 0; k < v.off.size(); k++ ) val[ k ] = us[ v.off[ k ] ];
				if ( v.name.rfind( "zmm", 0 ) == 0 )
				{
					// XMM + YMMH only (the "ymmN" alias) when the case does not move ZMM state; an all-zero vector by name only
					const std::string vn = uc.ext_regs ? v.name : "y" + v.name.substr( 1 );
					const size_t vb = uc.ext_regs ? 64 : 32;
					if ( std::all_of( val.begin(), val.begin() + std::ptrdiff_t( vb ), []( uint8_t c ) { return c == 0; } ) )
						zero += std::string( zero.empty() ? "" : "," ) + jx_quote( vn );
					else jx_field( o, vn, val.data(), vb, nullptr, false );
					continue;
				}
				if ( v.name == "rflags" )
				{
					const uint64_t m = ~( RF | VM | ( u.faulted ? TF : 0 ) );
					for ( int k = 0; k < 8; k++ ) mk[ size_t( k ) ] = uint8_t( m >> ( 8 * k ) );
				}
				jx_field( o, v.name, val.data(), val.size(), mk.data(), v.scalar );
			}
			const size_t fx = offsetof( state, fx );
			jx_field( o, "fop", us + fx + 6, 2, nullptr, true );
			jx_field( o, "fip", us + fx + 8, 8, nullptr, true );
			jx_field( o, "fdp", us + fx + 16, 8, nullptr, true );
			jx_field( o, "fptag", us + offsetof( state, env ) + 8, 2, nullptr, true );
			if ( uc.dbg.any && u.dr6_end_valid ) { std::vector<uint8_t> d( 8 ); std::memcpy( d.data(), &u.dr6_end, 8 ); jx_field( o, "dr6", d.data(), 8, nullptr, true ); }
			o += "},\"zero\":[" + zero + "],\"mem\":[" + jx_runs( us + offsetof( state, mem ), MEM_SIZE, MEM ) + "],\"full_mem\":[{\"address\":" + jx_u64( MEM ) +
				 ",\"size\":" + jx_u64( MEM_SIZE ) + "}]}";
		}
		o += "}\n";
		x.f << blob_lines << o;
		x.records++;
		return true;
	}
}
