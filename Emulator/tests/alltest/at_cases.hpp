// emu-alltest --cases FILE: hand-written snippets with a chosen input state, on the host CPU
// (self-generated snippets only) and on Unicorn UC_CPU_X86_MAX; prints each engine's fault and
// every field that changed, then the fields where the two engines differ.
//
// One case per line:   <asm, ';' separates instructions> | <assignments separated by spaces>
//   rax..r15=V  rflags=V  mxcsr=V  fcw=V  fsw=V  ftw=V (abridged tag byte)
//   st0..st7=SEXP:MANT (FXSAVE register slot i, tag bit set)   xmm0..xmm15=32 hex digits (low byte first)
//   m+OFF=HEXBYTES (operand memory at MEM + OFF; RSI/R14 = MEM + 0x8000, RDI = MEM + 0x9000)
// Default state: GPRs 0 except RSP/RSI/RDI/R14, RFLAGS 0x202, FCW 037F, MXCSR 1F80, all else 0.
// Lines starting with '#' and empty lines are skipped. Raw bytes: ".byte 0x67, 0xf3, 0xa4".
#pragma once
#include "at_engine.hpp"
#include <fstream>
#include <sstream>

namespace at
{
	static void case_default( state& s )
	{
		std::memset( &s, 0, sizeof( s ) );
		s.gpr[ RSP ] = STACK_TOP;
		s.gpr[ RSI ] = MEM_PTR;
		s.gpr[ RDI ] = MEM_DST;
		s.gpr[ R14 ] = MEM_PTR;
		s.rflags = 0x202;
		uint16_t fcw = 0x037F; std::memcpy( s.fx, &fcw, 2 );
		uint32_t mx = 0x1F80, mxmask = 0xFFFF;
		std::memcpy( s.fx + 24, &mx, 4 );
		std::memcpy( s.fx + 28, &mxmask, 4 );
	}

	static bool case_assign( state& s, const std::string& kv, std::string& err )
	{
		size_t eq = kv.find( '=' );
		if ( eq == std::string::npos ) { err = "no '=' in " + kv; return false; }
		std::string k = kv.substr( 0, eq ), v = kv.substr( eq + 1 );
		auto num = [ & ]() { return std::stoull( v, nullptr, 0 ); };
		auto hexbytes = [ & ]( uint8_t* dst, size_t max ) -> size_t {
			size_t n = 0;
			for ( size_t i = 0; i + 1 < v.size() && n < max; i += 2 ) dst[ n++ ] = uint8_t( std::stoul( v.substr( i, 2 ), nullptr, 16 ) );
			return n;
		};
		for ( int i = 0; i < 16; i++ )
			if ( k == reg_name( i ) ) { s.gpr[ i ] = num(); return true; }
		if ( k == "rflags" ) { s.rflags = num(); return true; }
		if ( k == "mxcsr" ) { uint32_t x = uint32_t( num() ); std::memcpy( s.fx + 24, &x, 4 ); return true; }
		if ( k == "fcw" ) { uint16_t x = uint16_t( num() ); std::memcpy( s.fx, &x, 2 ); return true; }
		if ( k == "fsw" ) { uint16_t x = uint16_t( num() ); std::memcpy( s.fx + 2, &x, 2 ); return true; }
		if ( k == "ftw" ) { s.fx[ 4 ] = uint8_t( num() ); return true; }
		if ( k.size() == 3 && k[ 0 ] == 's' && k[ 1 ] == 't' && k[ 2 ] >= '0' && k[ 2 ] <= '7' )
		{
			int i = k[ 2 ] - '0';
			size_t c = v.find( ':' );
			if ( c == std::string::npos ) { err = "st needs SEXP:MANT"; return false; }
			uint16_t se = uint16_t( std::stoul( v.substr( 0, c ), nullptr, 16 ) );
			uint64_t m = std::stoull( v.substr( c + 1 ), nullptr, 16 );
			std::memcpy( s.fx + 32 + 16 * i, &m, 8 ); std::memcpy( s.fx + 40 + 16 * i, &se, 2 );
			s.fx[ 4 ] |= uint8_t( 1u << i );
			return true;
		}
		if ( k.rfind( "ymmh", 0 ) == 0 )
		{
			int i = std::stoi( k.substr( 4 ) );
			if ( i < 0 || i > 15 ) { err = "bad ymmh"; return false; }
			hexbytes( s.ymmh[ i ], 16 );
			return true;
		}
		if ( k.rfind( "xmm", 0 ) == 0 )
		{
			int i = std::stoi( k.substr( 3 ) );
			if ( i < 0 || i > 15 ) { err = "bad xmm"; return false; }
			hexbytes( s.fx + 160 + 16 * i, 16 );
			return true;
		}
		if ( k.rfind( "m+", 0 ) == 0 )
		{
			uint64_t off = std::stoull( k.substr( 2 ), nullptr, 0 );
			if ( off >= MEM_SIZE ) { err = "m+ offset beyond MEM"; return false; }
			hexbytes( s.mem + off, MEM_SIZE - off );
			return true;
		}
		err = "unknown key " + k;
		return false;
	}

	// every field of 'o' that differs from 'i' (or, for an engine comparison, between two outputs)
	static std::string case_fields( const state& i, const state& o )
	{
		std::ostringstream os;
		auto h = [ & ]( uint64_t v ) { return hx( v ); };
		for ( int r = 0; r < 16; r++ )
			if ( i.gpr[ r ] != o.gpr[ r ] ) os << " " << reg_name( r ) << "=" << h( o.gpr[ r ] );
		if ( i.rflags != o.rflags ) os << " rflags=" << h( o.rflags );
		if ( i.mxcsr() != o.mxcsr() ) os << " mxcsr=" << h( o.mxcsr() );
		if ( i.fcw() != o.fcw() ) os << " fcw=" << h( o.fcw() );
		if ( i.fsw() != o.fsw() ) os << " fsw=" << h( o.fsw() );
		if ( i.ftw_abridged() != o.ftw_abridged() ) os << " ftw=" << h( o.ftw_abridged() );
		for ( int r = 0; r < 8; r++ )
			if ( std::memcmp( i.st( r ), o.st( r ), 10 ) )
			{
				uint64_t m; uint16_t se;
				std::memcpy( &m, o.st( r ), 8 ); std::memcpy( &se, o.st( r ) + 8, 2 );
				char b[ 48 ]; std::snprintf( b, sizeof( b ), " st%d=%04X:%016llX", r, se, ( unsigned long long ) m );
				os << b;
			}
		for ( int r = 0; r < 16; r++ )
			if ( std::memcmp( i.xmm( r ), o.xmm( r ), 16 ) )
			{
				os << " xmm" << r << "=";
				char b[ 4 ];
				for ( int k = 0; k < 16; k++ ) { std::snprintf( b, sizeof( b ), "%02X", o.xmm( r )[ k ] ); os << b; }
			}
		for ( int r = 0; r < 16; r++ )
			if ( std::memcmp( i.ymmh[ r ], o.ymmh[ r ], 16 ) )
			{
				os << " ymmh" << r << "=";
				char b[ 4 ];
				for ( int k = 0; k < 16; k++ ) { std::snprintf( b, sizeof( b ), "%02X", o.ymmh[ r ][ k ] ); os << b; }
			}
		// the rest of the FXSAVE header (FOP, FIP, FDP, MXCSR_MASK) as raw bytes
		for ( int k : { 6, 8, 16, 28 } )
		{
			int n = k == 6 ? 2 : ( k == 28 ? 4 : 8 );
			if ( std::memcmp( i.fx + k, o.fx + k, n ) )
			{
				uint64_t v = 0; std::memcpy( &v, o.fx + k, n );
				os << " fx+" << k << "=" << h( v );
			}
		}
		// FNSTENV image of the epilogue, its reserved upper words (bytes 2-3, 6-7, 10-11, 26-27)
		// masked: those are checked once by the "fnstenv" cases themselves
		uint8_t ei[ 28 ], eo[ 28 ];
		std::memcpy( ei, i.env, 28 ); std::memcpy( eo, o.env, 28 );
		for ( int k : { 2, 3, 6, 7, 10, 11, 26, 27 } ) ei[ k ] = eo[ k ] = 0;
		if ( std::memcmp( ei, eo, 28 ) )
		{
			os << " env=";
			char b[ 4 ];
			for ( int k = 0; k < 28; k++ ) { std::snprintf( b, sizeof( b ), "%02X", o.env[ k ] ); os << b; }
		}
		for ( uint64_t a = 0; a < MEM_SIZE; )
		{
			if ( i.mem[ a ] == o.mem[ a ] ) { ++a; continue; }
			uint64_t b = a;
			while ( b < MEM_SIZE && b - a < 32 && i.mem[ b ] != o.mem[ b ] ) ++b;
			os << " m+" << h( a ) << "=";
			char t[ 4 ];
			for ( uint64_t k = a; k < b; k++ ) { std::snprintf( t, sizeof( t ), "%02X", o.mem[ k ] ); os << t; }
			a = b;
		}
		return os.str();
	}

	// "LEAF SUB EAX EBX ECX EDX" per line (hex), '#' comments: Emulator\data\cpuid_*.txt
	static std::vector<uc_x86_cpuid> load_cpuid_profile( const std::string& path )
	{
		std::vector<uc_x86_cpuid> v;
		std::ifstream f( path );
		std::string line;
		while ( std::getline( f, line ) )
		{
			if ( line.empty() || line[ 0 ] == '#' ) continue;
			uc_x86_cpuid e{};
			std::istringstream is( line );
			is >> std::hex >> e.leaf >> e.subleaf >> e.eax >> e.ebx >> e.ecx >> e.edx;
			if ( is ) v.push_back( e );
		}
		return v;
	}

	struct case_opts { std::vector<uc_x86_cpuid> cpuid; int strict = 0; uint64_t xcr0 = 0; };

	static int run_cases( const std::string& path, uint32_t quirks, const case_opts& opt = {} )
	{
		std::ifstream f( path );
		if ( !f ) { std::printf( "cannot open %s\n", path.c_str() ); return 2; }
		native_engine hw;
		std::string err;
		if ( !hw.open( err ) ) { std::printf( "native engine: %s\n", err.c_str() ); return 2; }
		std::string line;
		int n = 0, differ = 0;
		while ( std::getline( f, line ) )
		{
			if ( !line.empty() && line.back() == '\r' ) line.pop_back();
			if ( line.empty() || line[ 0 ] == '#' ) continue;
			size_t bar = line.find( '|' );
			std::string text = line.substr( 0, bar ), assigns = bar == std::string::npos ? "" : line.substr( bar + 1 );
			std::string asmtext = text;
			for ( char& c : asmtext ) if ( c == ';' ) c = '\n';
			auto st_in = std::make_unique<state>();
			case_default( *st_in );
			std::istringstream as( assigns );
			std::string kv;
			bool ok = true;
			while ( as >> kv )
				if ( !case_assign( *st_in, kv, err ) ) { std::printf( "[%d] %s\n    bad assignment: %s\n", n, line.c_str(), err.c_str() ); ok = false; break; }
			if ( !ok ) continue;
			std::vector<uint8_t> bytes = assemble( asmtext, CODE, &err );
			if ( bytes.empty() ) { std::printf( "[%d] %s\n    assembly failed: %s\n", n, line.c_str(), err.c_str() ); continue; }
			program p = build( bytes, &err );
			if ( p.code.empty() ) { std::printf( "[%d] %s\n    build failed: %s\n", n, line.c_str(), err.c_str() ); continue; }
			result h, u;
			unicorn_engine uc( UC_CPU_X86_MAX, quirks );
			uc.cpuid = opt.cpuid; uc.strict = opt.strict; uc.xcr0 = opt.xcr0;
			if ( !uc.load( p, err ) ) { std::printf( "[%d] uc load: %s\n", n, err.c_str() ); continue; }
			uc.run( *st_in, u );
			hw.run( p, *st_in, h );
			bool same_fault = h.faulted == u.faulted && h.vector == u.vector;
			std::string hd = case_fields( *st_in, *h.s ), ud = case_fields( *st_in, *u.s ), x = case_fields( *h.s, *u.s );
			bool same = same_fault && x.empty();
			differ += !same;
			std::printf( "[%d] %s %s\n", n, same ? "SAME" : "DIFF", line.c_str() );
			std::printf( "    hw: %s%s\n", h.faulted ? ( "fault #" + std::to_string( h.vector ) + " " ).c_str() : "", hd.c_str() );
			std::printf( "    uc: %s%s\n", u.faulted ? ( "fault #" + std::to_string( u.vector ) + " " ).c_str() : "", ud.c_str() );
			if ( !same ) std::printf( "    uc vs hw:%s%s\n", same_fault ? "" : " (fault differs)", x.c_str() );
			++n;
		}
		std::printf( "cases: %d, differing: %d\n", n, differ );
		return 0;
	}
}
