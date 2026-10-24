// emu-selftest (plan 3.M1 / 3.A 2.2, ledger U1940-U1941): the Emulator lib's self-tests (Gate 2 = G3.A: "build.cmd builds
// everything with 0 errors, and the emu-selftest self-tests pass"). Suite emu_selftest runs every case; the no_native_check
// suite runs the case no_native_runtime on its own. Own test code only: nothing from a sample is read or executed, and no
// test process is started (no CreateProcess; the acutest runner is not used for that reason).
//
//   emu-selftest [--list] [NAME ...]      no NAME = every case
//   output: one "Test NAME... [ OK ]" / "[ FAILED ]" line per case (the acutest form test_runner.ps1 counts), then
//           "SUCCESS: All unit tests have passed." (exit 0) or "FAILED: F of N unit tests have failed." (exit 1); 2 = usage
#include <emu/deps.hpp>
#include <emu/host_guard.hpp>
#include <emu/options.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int g_fail = 0;

	void check( bool ok, const std::string& what, const char* file, int line )
	{
		if ( ok ) return;
		g_fail++;
		std::cout << "  CHECK FAILED " << file << ":" << line << ": " << what << "\n";
	}
#define CHECK( cond, what ) check( ( cond ), std::string( #cond ) + " - " + ( what ), __FILE__, __LINE__ )

	emu::parse_result parse( emu::tool_kind t, std::initializer_list<const char*> a )
	{
		std::vector<std::string> v;
		for ( auto s : a ) v.emplace_back( s );
		return emu::parse_options( t, v );
	}

	bool has_line( const std::vector<std::string>& lines, const std::string& part )
	{
		return std::any_of( lines.begin(), lines.end(), [&]( const std::string& l ) { return l.find( part ) != std::string::npos; } );
	}

	// ------------------------------------------------------------------------------------------------ options
	void t_options_oep_defaults()
	{
		auto r = parse( emu::tool_kind::oep, { "input.exe" } );
		CHECK( r.ok, r.error );
		auto& o = r.options;
		CHECK( o.profile == emu::cpu_profile_choice::host && !o.profile_given, "default profile = --host (decision 3.0.3)" );
		CHECK( o.boundary == emu::env_boundary::sys && !o.boundary_given, "default boundary = --Sys (D3)" );
		CHECK( o.rng == emu::rng_source::seeded && !o.rng_given, "default rng = --seeded (D8)" );
		CHECK( !o.max_insns_given && o.max_insns == 0, "no --max-insns" );
		CHECK( o.input_path == "input.exe", "INPUT recorded" );
		CHECK( o.out_dir.empty(), "no --out-dir" );
	}

	void t_options_choices()
	{
		struct row { const char* flag; emu::cpu_profile_choice p; };
		for ( auto rw : { row{ "--host", emu::cpu_profile_choice::host }, row{ "--guest", emu::cpu_profile_choice::guest },
						  row{ "--max", emu::cpu_profile_choice::max }, row{ "--cpuid-runtime", emu::cpu_profile_choice::runtime_cpuid },
						  row{ "--GUEST", emu::cpu_profile_choice::guest } } )
		{
			auto r = parse( emu::tool_kind::oep, { rw.flag, "x.exe" } );
			CHECK( r.ok && r.options.profile == rw.p && r.options.profile_given, rw.flag );
		}
		auto h = parse( emu::tool_kind::oep, { "--HLE", "--HostSeed", "x.exe" } );
		CHECK( h.ok && h.options.boundary == emu::env_boundary::hle && h.options.rng == emu::rng_source::host_seed, "--HLE --HostSeed" );
		auto s = parse( emu::tool_kind::snippet, { "--Sys", "--seeded", "--hex", "c3" } );
		CHECK( s.ok && s.options.boundary == emu::env_boundary::sys && s.options.boundary_given && s.options.rng_given, "--Sys --seeded" );
		auto twice = parse( emu::tool_kind::oep, { "--guest", "--guest", "x.exe" } );
		CHECK( twice.ok && twice.options.profile == emu::cpu_profile_choice::guest, "the same flag twice is accepted" );
	}

	void t_options_conflicts()
	{
		for ( auto pair : { std::pair{ "--host", "--guest" }, std::pair{ "--max", "--cpuid-runtime" }, std::pair{ "--Sys", "--HLE" },
							std::pair{ "--seeded", "--HostSeed" } } )
		{
			auto r = parse( emu::tool_kind::oep, { pair.first, pair.second, "x.exe" } );
			CHECK( !r.ok && r.error.find( "conflicting" ) != std::string::npos, std::string( pair.first ) + " + " + pair.second + ": " + r.error );
		}
	}

	void t_options_u64()
	{
		uint64_t v = 1;
		CHECK( emu::parse_u64( "0", v ) && v == 0, "0" );
		CHECK( emu::parse_u64( "18446744073709551615", v ) && v == UINT64_MAX, "2^64 - 1 decimal" );
		CHECK( emu::parse_u64( "0xffffffffffffffff", v ) && v == UINT64_MAX, "2^64 - 1 hex" );
		CHECK( emu::parse_u64( "0x100000000", v ) && v == 0x100000000ull, "2^32 kept (64-bit, never truncated)" );
		CHECK( emu::parse_u64( "1'000'000", v ) && v == 1000000, "digit separators" );
		CHECK( !emu::parse_u64( "18446744073709551616", v ), "2^64 decimal overflows" );
		CHECK( !emu::parse_u64( "0x10000000000000000", v ), "2^64 hex overflows" );
		CHECK( !emu::parse_u64( "99999999999999999999999", v ), "long decimal overflows" );
		for ( auto bad : { "", "-1", "+1", "0x", "12a", "0xg", " 1", "1 " } ) CHECK( !emu::parse_u64( bad, v ), std::string( "refused: \"" ) + bad + "\"" );
	}

	void t_options_max_insns()
	{
		auto r = parse( emu::tool_kind::oep, { "--max-insns", "0x1a2b3c4d5e", "x.exe" } );
		CHECK( r.ok && r.options.max_insns_given && r.options.max_insns == 0x1a2b3c4d5eull, "--max-insns above 2^32 kept exactly" );
		CHECK( !parse( emu::tool_kind::oep, { "--max-insns", "0", "x.exe" } ).ok, "--max-insns 0 refused" );
		CHECK( !parse( emu::tool_kind::oep, { "x.exe", "--max-insns" } ).ok, "--max-insns without a value" );
		CHECK( !parse( emu::tool_kind::oep, { "--max-insns", "1", "--max-insns", "2", "x.exe" } ).ok, "--max-insns twice" );
		CHECK( !parse( emu::tool_kind::oep, { "--max-insns", "-5", "x.exe" } ).ok, "negative --max-insns" );
	}

	void t_options_oep_args()
	{
		auto m = parse( emu::tool_kind::oep, {} );
		CHECK( !m.ok && m.error == "missing INPUT", m.error );
		auto two = parse( emu::tool_kind::oep, { "a.exe", "b.exe" } );
		CHECK( !two.ok && two.error.find( "more than one INPUT" ) != std::string::npos, two.error );
		auto unk = parse( emu::tool_kind::oep, { "--bogus", "a.exe" } );
		CHECK( !unk.ok && unk.error.find( "unknown option" ) != std::string::npos, unk.error );
		auto sn = parse( emu::tool_kind::oep, { "--hex", "c3", "a.exe" } );
		CHECK( !sn.ok, "emu-oep refuses the emu-snippet option --hex: " + sn.error );
		auto help = parse( emu::tool_kind::oep, { "--help" } );
		CHECK( help.ok && help.options.help, "--help without INPUT" );
		auto od = parse( emu::tool_kind::oep, { "--out-dir", "some\\dir", "a.exe" } );
		CHECK( od.ok && od.options.out_dir == "some\\dir", "--out-dir recorded" );
		CHECK( !parse( emu::tool_kind::oep, { "--out-dir", "d1", "--out-dir", "d2", "a.exe" } ).ok, "--out-dir twice" );
	}

	void t_options_snippet()
	{
		auto h = parse( emu::tool_kind::snippet, { "--hex", "48 31 C0,c3", "--expect", "RAX=0", "--expect", "rcx=0xffffffffffffffff", "--stop-at", "0x401000",
												   "--stop-ret-depth", "1" } );
		CHECK( h.ok, h.error );
		CHECK( h.options.snippet_input == emu::snippet_input_kind::hex, "hex input" );
		CHECK( ( h.options.snippet_bytes == std::vector<uint8_t>{ 0x48, 0x31, 0xC0, 0xC3 } ), "hex bytes" );
		CHECK( h.options.expects.size() == 2 && h.options.expects[ 0 ].reg == "rax" && h.options.expects[ 0 ].value == 0 &&
				   h.options.expects[ 1 ].value == UINT64_MAX, "two --expect items" );
		CHECK( h.options.stop_at_given && h.options.stop_at == 0x401000 && h.options.stop_ret_depth_given && h.options.stop_ret_depth == 1, "stops" );
		auto a = parse( emu::tool_kind::snippet, { "--asm", "xor eax, eax; ret" } );
		CHECK( a.ok && a.options.snippet_input == emu::snippet_input_kind::asm_text && a.options.snippet_asm == "xor eax, eax; ret", "asm input" );
		auto im = parse( emu::tool_kind::snippet, { "--image", "img.bin", "--rva", "0x1000" } );
		CHECK( im.ok && im.options.snippet_input == emu::snippet_input_kind::image && im.options.input_path == "img.bin" && im.options.image_rva == 0x1000, "image input" );
		CHECK( !parse( emu::tool_kind::snippet, { "--image", "img.bin" } ).ok, "--image without --rva" );
		CHECK( !parse( emu::tool_kind::snippet, { "--hex", "c3", "--rva", "1" } ).ok, "--rva without --image" );
		CHECK( !parse( emu::tool_kind::snippet, { "--hex", "c3", "--asm", "ret" } ).ok, "two inputs" );
		CHECK( !parse( emu::tool_kind::snippet, {} ).ok, "no input" );
		CHECK( !parse( emu::tool_kind::snippet, { "--hex", "c3", "x.exe" } ).ok, "positional argument refused" );
		for ( auto bad : { "c", "c3 4", "zz", "", " , " } ) CHECK( !parse( emu::tool_kind::snippet, { "--hex", bad } ).ok, std::string( "bad --hex \"" ) + bad + "\"" );
		for ( auto bad : { "rax", "=1", "rax=", "rax=-1", "r-x=1", "1ax=1", "rax=0x1g", "averyveryverylongregister=1" } )
			CHECK( !parse( emu::tool_kind::snippet, { "--hex", "c3", "--expect", bad } ).ok, std::string( "bad --expect \"" ) + bad + "\"" );
		std::vector<uint8_t> out;
		std::string why;
		std::string big( ( emu::max_hex_bytes + 1 ) * 2, 'a' );
		CHECK( !emu::parse_hex_bytes( big, out, why ) && out.empty(), "more than max_hex_bytes refused: " + why );
	}

	void t_options_describe()
	{
		auto r = parse( emu::tool_kind::oep, { "--max-insns", "4096", "--out-dir", "outd", "in.exe" } );
		CHECK( r.ok, r.error );
		auto d = emu::describe( r.options );
		for ( auto& l : d ) std::cout << "  describe: " << l << "\n";
		CHECK( has_line( d, "input: in.exe (recorded, not opened)" ), "INPUT recorded, not opened" );
		CHECK( has_line( d, "out-dir: outd (recorded, not created)" ), "out-dir recorded, not created" );
		CHECK( has_line( d, "cpu profile: host (default, decision 3.0.3" ), "default profile named" );
		CHECK( has_line( d, "max-insns: 4096 (0x1000)" ), "max-insns decimal + hex" );
		CHECK( has_line( d, "milestone: M1" ), "milestone" );
		auto s = parse( emu::tool_kind::snippet, { "--hex", "00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10", "--expect", "rax=1" } );
		auto ds = emu::describe( s.options );
		CHECK( has_line( ds, "snippet input: hex (17 bytes: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f ...)" ), "hex summary" );
		CHECK( has_line( ds, "expect rax = 0x1 (checked from milestone M3 on)" ), "expect line" );
		CHECK( emu::usage( emu::tool_kind::oep ).find( "--cpuid-runtime" ) != std::string::npos, "usage names the explicit runtime CPUID option" );
	}

	void t_deps_headers()
	{
		const auto s = emu::deps_summary();
		std::cout << "  deps: " << s << "\n";
		CHECK( s.find( "unicorn 2." ) == 0, "Unicorn 2 headers" );
		CHECK( s.find( "capstone 6." ) != std::string::npos, "Capstone 6 headers" );
		CHECK( std::string( emu::milestone() ) == "M1", "milestone M1" );
	}

	// ------------------------------------------------------------------------------------------------ host_guard
	void t_host_guard_regions()
	{
		const auto regs = emu::host_guard::committed_regions();
		std::cout << "  committed regions: " << regs.size() << "\n";
		CHECK( !regs.empty(), "VirtualQuery walk found committed memory" );
		bool sorted = true;
		for ( size_t i = 1; i < regs.size(); i++ )
			if ( regs[ i ].base < regs[ i - 1 ].base + regs[ i - 1 ].size ) sorted = false;
		CHECK( sorted, "regions ascending and non-overlapping" );
		// this function's own code: an executable region of a mapped image (MEM_IMAGE = 0x1000000)
		const uint64_t self = uint64_t( reinterpret_cast<uintptr_t>( &t_host_guard_regions ) );
		bool found = false;
		for ( auto& r : regs )
			if ( self >= r.base && self - r.base < r.size )
			{
				found = true;
				std::cout << "  own code region: " << emu::host_guard::describe( r ) << "\n";
				CHECK( emu::host_guard::is_executable( r.protect ) && r.type == 0x1000000, "own code is executable image memory" );
			}
		CHECK( found, "own code address inside a committed region" );
		CHECK( emu::host_guard::is_executable( 0x20 ) && emu::host_guard::is_executable( 0x40 ) && !emu::host_guard::is_executable( 0x04 ),
			   "PAGE_EXECUTE_READ / _READWRITE executable, PAGE_READWRITE not" );
		CHECK( !emu::host_guard::is_readable( 0x01 ) && !emu::host_guard::is_readable( 0x104 ) && emu::host_guard::is_readable( 0x04 ),
			   "NOACCESS and GUARD pages are not read" );
	}

	// The runtime half of no_native_check (AGENT_RULES "Host safety"; plan 3.M1): (1) no executable memory outside mapped
	// images (no RWX heap, no shellcode buffer, no JIT) exists in this process; (2) a guest-code buffer's bytes are found in
	// readable memory (positive control) but in no executable memory.
	void t_no_native_runtime()
	{
		const auto foreign = emu::host_guard::foreign_executable();
		std::cout << "  executable regions outside mapped images: " << foreign.size() << "\n";
		for ( auto& r : foreign ) std::cout << "    " << emu::host_guard::describe( r ) << "\n";
		CHECK( foreign.empty(), "no executable private / mapped memory in the process (M1: no engine, no JIT, no RWX heap)" );

		// a 64-byte guest-code marker made at run time (xorshift64*), so it exists only in this heap buffer
		std::vector<uint8_t> guest_code( 64 );
		uint64_t x = 0x9E3779B97F4A7C15ull;
		for ( auto& b : guest_code )
		{
			x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
			b = uint8_t( ( x * 0x2545F4914F6CDD1Dull ) >> 56 );
		}
		const uint64_t buf = uint64_t( reinterpret_cast<uintptr_t>( guest_code.data() ) );
		const auto in_exec = emu::host_guard::find_pattern( guest_code.data(), guest_code.size(), true );
		const auto in_any = emu::host_guard::find_pattern( guest_code.data(), guest_code.size(), false );
		std::cout << "  guest-code marker (64 bytes): " << in_any.size() << " copy(ies) in readable memory, " << in_exec.size()
				  << " in executable memory\n";
		CHECK( in_exec.empty(), "guest-code bytes never in host-executable memory" );
		CHECK( std::find( in_any.begin(), in_any.end(), buf ) != in_any.end(), "positive control: the scanner finds the marker in its own buffer" );
	}

	struct test_case { const char* name; void ( *fn )(); };
	const test_case cases[] = {
		{ "options_oep_defaults", t_options_oep_defaults },
		{ "options_choices", t_options_choices },
		{ "options_conflicts", t_options_conflicts },
		{ "options_u64", t_options_u64 },
		{ "options_max_insns", t_options_max_insns },
		{ "options_oep_args", t_options_oep_args },
		{ "options_snippet", t_options_snippet },
		{ "options_describe", t_options_describe },
		{ "deps_headers", t_deps_headers },
		{ "host_guard_regions", t_host_guard_regions },
		{ "no_native_runtime", t_no_native_runtime },
	};
}

int main( int argc, char** argv )
{
	std::vector<std::string> names;
	for ( int i = 1; i < argc; i++ )
	{
		std::string a = argv[ i ];
		if ( a == "--list" )
		{
			for ( auto& c : cases ) std::cout << c.name << "\n";
			return 0;
		}
		if ( !a.empty() && a[ 0 ] == '-' ) { std::cout << "usage: emu-selftest [--list] [NAME ...]\n"; return 2; }
		names.push_back( a );
	}
	for ( auto& n : names )
		if ( std::none_of( std::begin( cases ), std::end( cases ), [&]( const test_case& c ) { return n == c.name; } ) )
		{
			std::cout << "[selftest] unknown case \"" << n << "\" (emu-selftest --list)\n";
			return 2;
		}
	std::cout << "[selftest] Emulator lib milestone " << emu::milestone() << "\n";
	int run = 0, failed = 0;
	for ( auto& c : cases )
	{
		if ( !names.empty() && std::find( names.begin(), names.end(), c.name ) == names.end() ) continue;
		const int before = g_fail;
		std::cout << "[selftest] case " << c.name << "\n";
		c.fn();
		const bool ok = g_fail == before;
		run++;
		if ( !ok ) failed++;
		std::string label = std::string( "Test " ) + c.name + "...";
		std::cout << label << std::string( label.size() < 50 ? 50 - label.size() : 1, ' ' ) << ( ok ? "[ OK ]" : "[ FAILED ]" ) << "\n";
	}
	if ( failed == 0 )
	{
		std::cout << "SUCCESS: All unit tests have passed.\n";
		return 0;
	}
	std::cout << "FAILED: " << failed << " of " << run << " unit tests have failed.\n";
	return 1;
}
