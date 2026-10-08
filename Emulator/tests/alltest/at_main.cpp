// emu-alltest (plan step 1.9): one full test of every instruction form.
//
//   emu-alltest [--full] [--iters N] [--sample N] [--filter S] [--out DIR] [--quirks N] [--rebuild]
//
//   default (quick)  every 7th form, 2 iterations - the test.cmd smoke run
//   --full           every form, --iters iterations (default 6)
//   --filter S       only forms whose text, key or ISA groups contain S
//   --out DIR        report directory (default: <exe dir>\alltest)
//   --quirks N       UC_CTL_X86_HW_QUIRKS bitmask for Unicorn (default 0 = follow the manual)
//   --rebuild        re-sweep the opcode space instead of using the cached universe
//   --cases FILE     hand-written snippets with a chosen input state (see at_cases.hpp)
//                    [--cpuid FILE] [--strict] [--xcr0 V] [--cr0 V] (Unicorn CPUID profile, strict
//                    #UD, XCR0, CR0; --cr0 0x33 = PE|MP|ET|NE as under Windows x64, so pending
//                    x87 exceptions raise #MF; PG must stay clear: flat Unicorn memory)
//
// Each form runs with identical randomized state on the host CPU (self-generated snippets only,
// native-safe forms) and on Unicorn UC_CPU_X86_MAX; the full architectural result is compared.
// Every form lands in exactly one bucket (see BUCKETS below); the report lists them per ISA group,
// plus the manual's forms that the sweep cannot reach yet (Emulator\data\isa_manual_forms.tsv).
#include "at_universe.hpp"
#include "at_cases.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <random>
#include <sstream>

namespace at
{
	enum bucket { MATCH, DIFF, UC_MISSING, HOST_LACKS_UC_UD, HOST_LACKS_UC_RUNS, NOT_NATIVE_UC_RUNS, NOT_NATIVE_UC_UD, PRIV, HARNESS_ERROR, BUCKET_COUNT };
	static const char* k_bucket_name[ BUCKET_COUNT ] = {
		"match", "differs", "unicorn-#UD (hw runs it)", "host lacks + unicorn #UD", "host lacks, unicorn runs (needs SDM check)",
		"not native-safe, unicorn runs (needs SDM check)", "not native-safe, unicorn #UD", "privileged (CPL0 in raw unicorn; Phase 2 CPL3)", "harness error" };

	// ── randomized state (ported from the old difftest) ───────────────────────────────────
	static std::mt19937_64 g_rng( 0x5EED1234 );

	static uint64_t rnd_gpr()
	{
		static const uint64_t edge[] = { 0, 1, 2, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000ull, 0xFFFFFFFFull,
										 0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, ~0ull, ~0ull - 1 };
		switch ( g_rng() % 4 )
		{
			case 0: return g_rng() % 256;
			case 1: return edge[ g_rng() % 16 ];
			default:
			{
				uint64_t v = g_rng();
				if ( ( v >> 32 ) == 0 && v >= 0x30000000 && v < 0x30100000 ) v ^= 0x40000000;   // never alias our regions
				return v;
			}
		}
	}

	static void rnd_float_bytes( uint8_t* p, int n, bool dbl )
	{
		for ( int k = 0; k < n; k += ( dbl ? 8 : 4 ) )
		{
			int r = int( g_rng() % 10 );
			if ( dbl )
			{
				static const double sp[] = { 0.0, -0.0, INFINITY, -INFINITY, NAN, 1e-310, -1e-310, 1.0, -1.0, 0.5 };
				double d;
				if ( r < 5 ) d = ( double( g_rng() % 2000001 ) - 1000000.0 ) / double( 1 + g_rng() % 1000 );
				else if ( r < 7 ) d = sp[ g_rng() % 10 ];
				else { uint64_t u = g_rng(); std::memcpy( &d, &u, 8 ); }
				std::memcpy( p + k, &d, 8 );
			}
			else
			{
				static const float sp[] = { 0.0f, -0.0f, INFINITY, -INFINITY, NAN, 1e-40f, -1e-40f, 1.0f, -1.0f, 0.5f };
				float f;
				if ( r < 5 ) f = float( ( double( g_rng() % 20001 ) - 10000.0 ) / double( 1 + g_rng() % 100 ) );
				else if ( r < 7 ) f = sp[ g_rng() % 10 ];
				else { uint32_t u = uint32_t( g_rng() ); std::memcpy( &f, &u, 4 ); }
				std::memcpy( p + k, &f, 4 );
			}
		}
	}

	static void rnd_state( state& s, int iter )
	{
		std::memset( &s, 0, sizeof( s ) );
		for ( int i = 0; i < 16; i++ ) s.gpr[ i ] = rnd_gpr();
		s.gpr[ RSP ] = STACK_TOP;
		s.gpr[ RSI ] = MEM_PTR;
		s.gpr[ RDI ] = MEM_DST;
		s.gpr[ R14 ] = MEM_PTR;
		s.gpr[ RCX ] = g_rng() % 128;          // bounded REP count / shift count / SIB index
		uint64_t fl = 0x202;
		for ( uint64_t b : { 0x1ull, 0x4ull, 0x10ull, 0x40ull, 0x80ull, 0x800ull } ) if ( g_rng() & 1 ) fl |= b;
		if ( g_rng() % 8 == 0 ) fl |= 0x400;     // DF
		s.rflags = fl;
		uint32_t mx = 0x1F80;
		if ( iter % 4 == 3 ) mx |= ( uint32_t( g_rng() % 4 ) << 13 ) | ( ( g_rng() & 1 ) ? 0x8040 : 0 );   // RC, FTZ|DAZ
		for ( int i = 0; i < 16; i++ )
		{
			uint8_t v[ 32 ];
			int mode = ( i + iter ) % 3;
			if ( mode == 0 ) for ( auto& b : v ) b = uint8_t( g_rng() );
			else rnd_float_bytes( v, 32, mode == 2 );
			std::memcpy( s.fx + 160 + i * 16, v, 16 );
			std::memcpy( s.ymmh[ i ], v + 16, 16 );
		}
		uint16_t fcw = 0x037F; std::memcpy( s.fx, &fcw, 2 );
		std::memcpy( s.fx + 24, &mx, 4 );
		uint32_t mxmask = 0xFFFF; std::memcpy( s.fx + 28, &mxmask, 4 );
		static const uint8_t ftw_cycle[ 4 ] = { 0x0F, 0x0F, 0xFF, 0x00 };   // ST0-3 valid / all valid / empty
		s.fx[ 4 ] = ftw_cycle[ iter % 4 ];
		for ( int i = 0; i < 8; i++ )
		{
			uint8_t* st = s.fx + 32 + 16 * i;
			if ( g_rng() % 3 == 0 ) for ( int k = 0; k < 10; k++ ) st[ k ] = uint8_t( g_rng() );
			else
			{
				double d = ( double( g_rng() % 2000001 ) - 1000000.0 ) / double( 1 + g_rng() % 1000 );
				uint64_t u; std::memcpy( &u, &d, 8 );
				uint16_t sign = uint16_t( u >> 63 ), e = uint16_t( ( u >> 52 ) & 0x7FF );
				uint64_t man = 0; uint16_t ex = 0;
				if ( e ) { man = ( 1ull << 63 ) | ( ( u & ( ( 1ull << 52 ) - 1 ) ) << 11 ); ex = uint16_t( e - 1023 + 16383 ); }
				ex |= uint16_t( sign << 15 );
				std::memcpy( st, &man, 8 ); std::memcpy( st + 8, &ex, 2 );
			}
		}
		for ( auto& b : s.mem ) b = uint8_t( g_rng() );
		rnd_float_bytes( s.mem + 0x8000, 256, iter & 1 );
		rnd_float_bytes( s.mem + 0x9000, 256, !( iter & 1 ) );
		for ( auto& b : s.stack ) b = uint8_t( g_rng() );
	}

	// ── comparison ──────────────────────────────────────────────────────────────────────────
	static bool approx_lanes( const uint8_t* a, const uint8_t* b, int n )
	{
		for ( int k = 0; k < n; k += 4 )
		{
			float x, y; std::memcpy( &x, a + k, 4 ); std::memcpy( &y, b + k, 4 );
			if ( ( std::isnan( x ) && std::isnan( y ) ) || x == y ) continue;
			if ( std::isinf( x ) || std::isinf( y ) ) return false;
			if ( std::fabs( double( x ) - y ) / std::max( 1e-30, std::fabs( double( x ) ) ) > 1.5 / 4096.0 ) return false;
		}
		return true;
	}

	static int fault_class( const result& r )
	{
		if ( !r.faulted ) return -1;
		return ( r.vector == 13 || r.vector == 14 ) ? 1314 : r.vector;   // #GP and #PF both surface as AV natively
	}

	static std::string compare( const form& f, const state& in, const result& h, const result& u )
	{
		std::string d;
		char b[ 320 ];
		if ( fault_class( h ) != fault_class( u ) )
		{
			std::snprintf( b, sizeof( b ), "outcome hw=%s uc=%s", h.faulted ? ( "vector " + std::to_string( h.vector ) ).c_str() : "ok",
						   u.faulted ? ( "vector " + std::to_string( u.vector ) ).c_str() : "ok" );
			return b;
		}
		if ( h.faulted || f.kind == 2 ) return "";
		uint64_t fm = f.flag_mask;
		if ( f.shift_op )
		{
			unsigned raw = f.count_kind == 1 ? f.imm : f.count_kind == 2 ? unsigned( in.gpr[ RCX ] & 0xFF ) : 1u;
			unsigned c = raw & ( f.op_bits == 64 ? 0x3Fu : 0x1Fu );
			if ( c != 0 )
			{
				if ( f.shift_op == 1 || f.shift_op == 2 || f.shift_op == 5 ) fm &= ~0x010ull;     // AF
				if ( c != 1 ) fm &= ~0x800ull;                                                    // OF
				if ( f.shift_op == 1 && f.op_bits <= 16 && c >= unsigned( f.op_bits ) ) fm &= ~0x001ull;
				if ( f.shift_op == 5 && c > unsigned( f.op_bits ) ) return "";                    // result undefined
			}
		}
		const state& hs = *h.s; const state& us = *u.s;
		for ( int i = 0; i < 16; i++ )
			if ( hs.gpr[ i ] != us.gpr[ i ] )
			{
				std::snprintf( b, sizeof( b ), "%s hw=%016llX uc=%016llX (in %016llX); ", reg_name( i ),
							   ( unsigned long long ) hs.gpr[ i ], ( unsigned long long ) us.gpr[ i ], ( unsigned long long ) in.gpr[ i ] );
				d += b;
			}
		if ( ( hs.rflags & fm ) != ( us.rflags & fm ) )
		{
			std::snprintf( b, sizeof( b ), "rflags hw=%03llX uc=%03llX mask=%03llX; ", ( unsigned long long )( hs.rflags & 0xFFF ),
						   ( unsigned long long )( us.rflags & 0xFFF ), ( unsigned long long ) fm );
			d += b;
		}
		if ( hs.mxcsr() != us.mxcsr() ) { std::snprintf( b, sizeof( b ), "mxcsr hw=%04X uc=%04X; ", hs.mxcsr(), us.mxcsr() ); d += b; }
		for ( int i = 0; i < 16; i++ )
		{
			bool lo = f.kind == 1 ? approx_lanes( hs.xmm( i ), us.xmm( i ), 16 ) : !std::memcmp( hs.xmm( i ), us.xmm( i ), 16 );
			bool hi = f.kind == 1 ? approx_lanes( hs.ymmh[ i ], us.ymmh[ i ], 16 ) : !std::memcmp( hs.ymmh[ i ], us.ymmh[ i ], 16 );
			if ( !lo || !hi ) { d += "ymm" + std::to_string( i ) + ( lo ? "[255:128]" : "" ) + " differs; "; break; }
		}
		if ( hs.fcw() != us.fcw() || ( hs.fsw() & f.fsw_mask ) != ( us.fsw() & f.fsw_mask ) || hs.ftw_full() != us.ftw_full() )
		{
			std::snprintf( b, sizeof( b ), "x87 fcw/fsw/ftw hw=%04X/%04X/%04X uc=%04X/%04X/%04X (fsw mask %04X); ",
						   hs.fcw(), hs.fsw(), hs.ftw_full(), us.fcw(), us.fsw(), us.ftw_full(), f.fsw_mask );
			d += b;
		}
		for ( int i = 0; i < 8; i++ )
			if ( std::memcmp( hs.st( i ), us.st( i ), 10 ) ) { d += "st(" + std::to_string( i ) + ") differs; "; break; }
		if ( std::memcmp( hs.mem, us.mem, MEM_SIZE ) )
		{
			size_t k = 0; while ( k < MEM_SIZE && hs.mem[ k ] == us.mem[ k ] ) k++;
			std::snprintf( b, sizeof( b ), "mem[+0x%zX] hw=%02X uc=%02X; ", k, hs.mem[ k ], us.mem[ k ] );
			d += b;
		}
		uint64_t rsp = us.gpr[ RSP ];
		size_t from = ( rsp >= STACK_LO && rsp <= STACK_TOP ) ? size_t( rsp - STACK_LO ) : sizeof( hs.stack );
		for ( size_t k = from; k < sizeof( hs.stack ); k++ )
			if ( hs.stack[ k ] != us.stack[ k ] ) { d += "stack differs; "; break; }
		return d;
	}

	struct form_result
	{
		bucket b = MATCH;
		int iters = 0, iters_diff = 0;
		std::string detail;
		std::string uc_outcome, hw_outcome;
	};

	static std::string outcome_str( const result& r ) { return r.faulted ? "#" + std::to_string( r.vector ) : "ok"; }

	static form_result run_form( const form& f, native_engine& hw, int iters, uint32_t quirks )
	{
		form_result fr;
		std::string err;
		program p = build( f.bytes, &err );
		if ( p.code.empty() ) { fr.b = HARNESS_ERROR; fr.detail = "build: " + err; return fr; }
		unicorn_engine uc( UC_CPU_X86_MAX, quirks );
		if ( !uc.load( p, err ) ) { fr.b = HARNESS_ERROR; fr.detail = err; return fr; }
		auto in = std::make_unique<state>();
		bool hw_ud_all = true, uc_ud_all = true, any_diff = false;
		for ( int it = 0; it < iters; ++it )
		{
			rnd_state( *in, it );
			result u;
			uc.run( *in, u );
			if ( !u.ran && !u.faulted ) { fr.b = HARNESS_ERROR; fr.detail = u.err; return fr; }
			uc_ud_all = uc_ud_all && u.faulted && u.vector == 6;
			fr.uc_outcome = outcome_str( u );
			fr.iters++;
			if ( !f.hw_safe || f.privileged ) continue;
			result h;
			hw.run( p, *in, h );
			hw_ud_all = hw_ud_all && h.faulted && h.vector == 6;
			fr.hw_outcome = outcome_str( h );
			std::string d = compare( f, *in, h, u );
			if ( !d.empty() ) { any_diff = true; fr.iters_diff++; if ( fr.detail.empty() ) fr.detail = "iter " + std::to_string( it ) + ": " + d; }
		}
		if ( f.privileged ) fr.b = PRIV;
		else if ( !f.hw_safe ) fr.b = uc_ud_all ? NOT_NATIVE_UC_UD : NOT_NATIVE_UC_RUNS;
		else if ( hw_ud_all && uc_ud_all ) fr.b = HOST_LACKS_UC_UD;
		else if ( hw_ud_all ) fr.b = HOST_LACKS_UC_RUNS;
		else if ( uc_ud_all ) fr.b = UC_MISSING;
		else fr.b = any_diff ? DIFF : MATCH;
		if ( !f.hw_safe && fr.detail.empty() ) fr.detail = f.skip_reason;
		return fr;
	}

	static std::string csv_escape( const std::string& s )
	{
		std::string o = "\"";
		for ( char c : s ) { if ( c == '"' ) o += "\"\""; else o += c; }
		return o + "\"";
	}
}

int main( int argc, char** argv )
{
	using namespace at;
	std::setvbuf( stdout, nullptr, _IONBF, 0 );
	bool full = false, rebuild = false;
	int iters = -1, sample = -1;
	uint32_t quirks = 0;
	std::string filter, out_dir, cases;
	at::case_opts copt;
	for ( int i = 1; i < argc; ++i )
	{
		std::string a = argv[ i ];
		auto val = [ & ]() -> std::string { if ( i + 1 >= argc ) { std::printf( "missing value for %s\n", a.c_str() ); std::exit( 2 ); } return argv[ ++i ]; };
		if ( a == "--full" ) full = true;
		else if ( a == "--rebuild" ) rebuild = true;
		else if ( a == "--iters" ) iters = std::stoi( val() );
		else if ( a == "--sample" ) sample = std::stoi( val() );
		else if ( a == "--filter" ) filter = val();
		else if ( a == "--out" ) out_dir = val();
		else if ( a == "--quirks" ) quirks = uint32_t( std::stoul( val(), nullptr, 0 ) );
		else if ( a == "--cases" ) cases = val();
		else if ( a == "--cpuid" ) copt.cpuid = at::load_cpuid_profile( val() );
		else if ( a == "--strict" ) copt.strict = 1;
		else if ( a == "--xcr0" ) copt.xcr0 = std::stoull( val(), nullptr, 0 );
		else if ( a == "--cr0" ) copt.cr0 = std::stoull( val(), nullptr, 0 );
		else { std::printf( "usage: emu-alltest [--full] [--iters N] [--sample N] [--filter S] [--out DIR] [--quirks N] [--rebuild] [--cases FILE [--cpuid FILE] [--strict] [--xcr0 V] [--cr0 V]]\n" ); return 2; }
	}
	if ( !cases.empty() ) return at::run_cases( cases, quirks, copt );
	if ( iters < 0 ) iters = full ? 6 : 2;
	if ( sample < 0 ) sample = full ? 1 : 7;
	char exe[ MAX_PATH ]; GetModuleFileNameA( nullptr, exe, MAX_PATH );
	std::filesystem::path exe_dir = std::filesystem::path( exe ).parent_path();
	if ( out_dir.empty() ) out_dir = ( exe_dir / "alltest" ).string();
	std::filesystem::create_directories( out_dir );
	std::filesystem::path root = exe_dir / ".." / ".." / ".." / "..";          // build\x64\Release\tests -> NoVmp root
	std::string manual_tsv = ( root / "Emulator" / "data" / "isa_manual_forms.tsv" ).lexically_normal().string();

	unsigned maj = 0, min = 0; uc_version( &maj, &min );
	std::printf( "emu-alltest: Unicorn %u.%u UC_CPU_X86_MAX vs host CPU | mode %s | iterations %d | sample 1/%d | quirks 0x%X\n",
				 maj, min, full ? "full" : "quick", iters, sample, quirks );
	std::printf( "  output: %s\n", out_dir.c_str() );

	// universe
	auto t0 = std::chrono::steady_clock::now();
	universe uv;
	std::string cache = out_dir + "\\forms.cache";
	if ( rebuild || !uv.load_cache( cache ) )
	{
		std::printf( "  sweeping the opcode space (legacy, VEX, EVEX) ...\n" );
		uv.sweep();
		uv.save_cache( cache );
	}
	double sweep_s = std::chrono::duration<double>( std::chrono::steady_clock::now() - t0 ).count();
	std::map<std::string, int> per_cls;
	for ( auto& f : uv.forms ) per_cls[ f.cls ]++;
	std::printf( "  universe: %zu forms (", uv.forms.size() );
	for ( auto& [ k, v ] : per_cls ) std::printf( "%s %d ", k.c_str(), v );
	std::printf( ") in %.1f s\n", sweep_s );

	native_engine hw;
	std::string err;
	if ( !hw.open( err ) ) { std::printf( "native engine: %s\n", err.c_str() ); return 1; }

	struct row { const form* f; form_result r; };
	std::vector<row> rows;
	size_t idx = 0;
	auto t1 = std::chrono::steady_clock::now();
	for ( auto& f : uv.forms )
	{
		if ( !filter.empty() && f.text.find( filter ) == std::string::npos && f.key.find( filter ) == std::string::npos && f.groups.find( filter ) == std::string::npos ) continue;
		if ( idx++ % size_t( sample ) ) continue;
		rows.push_back( { &f, run_form( f, hw, iters, quirks ) } );
		if ( rows.size() % 500 == 0 ) std::printf( "  ... %zu forms run\n", rows.size() );
	}
	double run_s = std::chrono::duration<double>( std::chrono::steady_clock::now() - t1 ).count();

	// CSV
	{
		std::ofstream csv( out_dir + "\\alltest.csv" );
		csv << "bucket,class,groups,form,bytes,hw,unicorn,iters,iters_diff,detail\n";
		for ( auto& r : rows )
		{
			std::string hex;
			for ( uint8_t c : r.f->bytes ) { char h[ 4 ]; std::snprintf( h, sizeof( h ), "%02X ", c ); hex += h; }
			csv << csv_escape( k_bucket_name[ r.r.b ] ) << ',' << r.f->cls << ',' << csv_escape( r.f->groups ) << ',' << csv_escape( r.f->text ) << ','
				<< csv_escape( hex ) << ',' << r.r.hw_outcome << ',' << r.r.uc_outcome << ',' << r.r.iters << ',' << r.r.iters_diff << ',' << csv_escape( r.r.detail ) << '\n';
		}
	}

	// summary per bucket and per ISA group
	int count[ BUCKET_COUNT ] = {};
	std::map<std::string, std::array<int, BUCKET_COUNT>> per_group;
	for ( auto& r : rows ) { count[ r.r.b ]++; per_group[ r.f->groups ][ r.r.b ]++; }

	// the manual's forms the sweep could not reach (no Capstone decode yet)
	std::set<std::string> swept;
	for ( auto& f : uv.forms )
	{
		std::string mn = f.text.substr( 0, f.text.find( ' ' ) );
		swept.insert( ( f.cls == "x87" ? std::string( "legacy" ) : f.cls ) + ":" + mn );
	}
	std::map<std::string, std::vector<std::string>> unreached;   // isa -> mnemonics
	int manual_forms = 0, manual_unreached = 0;
	{
		std::ifstream tsv( manual_tsv );
		std::string line;
		while ( std::getline( tsv, line ) )
		{
			if ( line.empty() || line[ 0 ] == '#' ) continue;
			std::istringstream ls( line );
			std::string mn, enc, vlen, isa, flags;
			std::getline( ls, mn, '\t' ); std::getline( ls, enc, '\t' ); std::getline( ls, vlen, '\t' ); std::getline( ls, isa, '\t' );
			++manual_forms;
			if ( enc == "xop" ) enc = "vex";
			if ( !swept.count( enc + ":" + mn ) )
			{
				++manual_unreached;
				auto& v = unreached[ isa ];
				if ( std::find( v.begin(), v.end(), mn ) == v.end() ) v.push_back( mn );
			}
		}
	}

	std::ofstream md( out_dir + "\\alltest_report.md" );
	md << "# emu-alltest report\n\nUnicorn " << maj << "." << min << " `UC_CPU_X86_MAX` vs host CPU. Mode **" << ( full ? "full" : "quick" )
	   << "**, " << iters << " iterations per form, sample 1/" << sample << ", quirks 0x" << std::hex << quirks << std::dec << ".\n\n";
	md << "Universe: **" << uv.forms.size() << " forms** decoded by Capstone (";
	for ( auto& [ k, v ] : per_cls ) md << k << " " << v << ", ";
	md << "). Run: **" << rows.size() << " forms** in " << int( run_s ) << " s.\n\n## Buckets\n\n| bucket | forms |\n|---|---|\n";
	for ( int b = 0; b < BUCKET_COUNT; ++b ) md << "| " << k_bucket_name[ b ] << " | " << count[ b ] << " |\n";
	md << "\n## Per ISA group (Capstone groups)\n\n| group | match | differs | unicorn #UD | host lacks + uc #UD | host lacks, uc runs | not native, uc runs | not native, uc #UD | privileged | error |\n|---|---|---|---|---|---|---|---|---|---|\n";
	for ( auto& [ g, c ] : per_group )
	{
		md << "| " << g;
		for ( int b = 0; b < BUCKET_COUNT; ++b ) md << " | " << c[ b ];
		md << " |\n";
	}
	md << "\n## Differences (first iteration that differs)\n\n";
	for ( auto& r : rows )
		if ( r.r.b == DIFF || r.r.b == UC_MISSING || r.r.b == HARNESS_ERROR )
			md << "- `" << r.f->text << "` (" << r.f->groups << ", " << k_bucket_name[ r.r.b ] << ", " << r.r.iters_diff << "/" << r.r.iters << "): " << r.r.detail << "\n";
	md << "\n## Manual forms not reachable by the sweep yet\n\n" << manual_unreached << " of " << manual_forms
	   << " forms in `Emulator\\data\\isa_manual_forms.tsv` have a mnemonic/encoding Capstone 6 never produced (no decoder yet - plan 5.3/5.3b; some are naming differences to review):\n\n";
	for ( auto& [ isa, mns ] : unreached )
	{
		md << "- **" << isa << "** (" << mns.size() << "): ";
		for ( size_t k = 0; k < mns.size() && k < 40; ++k ) md << ( k ? ", " : "" ) << mns[ k ];
		if ( mns.size() > 40 ) md << ", …";
		md << "\n";
	}

	std::printf( "\n  buckets:\n" );
	for ( int b = 0; b < BUCKET_COUNT; ++b ) std::printf( "    %-50s %6d\n", k_bucket_name[ b ], count[ b ] );
	std::printf( "  manual forms not reachable by the sweep yet: %d of %d\n", manual_unreached, manual_forms );
	std::printf( "  report: %s\\alltest_report.md  (csv: alltest.csv)  - %.0f s\n", out_dir.c_str(), run_s );
	// exit status: harness errors fail the run; the buckets are the report (the work list), not a pass/fail
	return count[ HARNESS_ERROR ] ? 1 : 0;
}
