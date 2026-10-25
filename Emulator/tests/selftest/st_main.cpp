// emu-selftest (plan 3.M1 / 3.A 2.2, ledger U1940-U1941; plan 3.M2, ledger U1951-U1954; plan 3.M3, ledger U1976): the Emulator lib's self-tests (Gate 2 = G3.A: "build.cmd builds
// everything with 0 errors, and the emu-selftest self-tests pass"). Suite emu_selftest runs every case; the no_native_check
// suite runs the case no_native_runtime on its own. Own test code only: nothing from a sample is read or executed, and no
// test process is started (no CreateProcess; the acutest runner is not used for that reason).
//
//   emu-selftest [--list] [NAME ...]      no NAME = every case
//   output: one "Test NAME... [ OK ]" / "[ FAILED ]" line per case (the acutest form test_runner.ps1 counts), then
//           "SUCCESS: All unit tests have passed." (exit 0) or "FAILED: F of N unit tests have failed." (exit 1); 2 = usage
#include <emu/deps.hpp>
#include <emu/engine.hpp>
#include <emu/host_guard.hpp>
#include <emu/options.hpp>
#include <emu/profile.hpp>
#include <emu/reg.hpp>
#include <emu/assemble.hpp>
#include <emu/input_file.hpp>
#include <emu/json.hpp>
#include <emu/snippet.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
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
		CHECK( has_line( d, "milestone: M3" ), "milestone" );
		auto s = parse( emu::tool_kind::snippet, { "--hex", "00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10", "--expect", "rax=1" } );
		auto ds = emu::describe( s.options );
		CHECK( has_line( ds, "snippet input: hex (17 bytes: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f ...)" ), "hex summary" );
		CHECK( has_line( ds, "  expect rax=1" ), "expect line (U1975: as given)" );
		CHECK( emu::usage( emu::tool_kind::oep ).find( "--cpuid-runtime" ) != std::string::npos, "usage names the explicit runtime CPUID option" );
	}

	void t_deps_headers()
	{
		const auto s = emu::deps_summary();
		std::cout << "  deps: " << s << "\n";
		CHECK( s.find( "unicorn 2." ) == 0, "Unicorn 2 headers" );
		CHECK( s.find( "capstone 6." ) != std::string::npos, "Capstone 6 headers" );
		CHECK( std::string( emu::milestone() ) == "M3", "milestone M3" );
	}

	// ------------------------------------------------------------------------------------------------ M2 engine (U1951-U1954)
	// Plan 3.M2: profile CPUID leaves, strict profile UC_ERR_ARG after the first instruction, dump round trip, the dump of a
	// reset engine equals the fork's uc_context values; plus D8, the memory map and the verbose log. No CPUID value is printed
	// (the profiles are real captures): only counts, leaf numbers and register names.
	constexpr uint64_t st_code = 0x10000;   // guest code page of the engine cases
	constexpr uint64_t st_data = 0x20000;   // guest data pages

	std::unique_ptr<emu::engine> open_engine( emu::cpu_profile_choice p, emu::rng_source rng = emu::rng_source::seeded, uint64_t seed = 0 )
	{
		emu::engine_config c;
		c.profile = p;
		c.rng = rng;
		c.rng_seed = seed;
		std::string err;
		auto e = emu::engine::open( c, err );
		CHECK( e != nullptr, std::string( "engine::open " ) + emu::name_of( p ) + ": " + err );
		return e;
	}

	bool put_code( emu::engine& e, const std::vector<uint8_t>& code )
	{
		std::string err;
		bool ok = true;
		if ( e.regions().empty() ) ok = e.map( st_code, 0x1000, UC_PROT_ALL, "code", err ) && e.map( st_data, 0x2000, UC_PROT_READ | UC_PROT_WRITE, "data", err );
		ok = ok && e.write( st_code, code.data(), code.size(), err );
		CHECK( ok, "code page: " + err );
		return ok;
	}

	bool set64( emu::engine& e, int id, uint64_t v ) { return uc_reg_write( e.handle(), id, &v ) == UC_ERR_OK; }
	uint64_t get64( emu::engine& e, int id )
	{
		uint64_t v = 0;
		uc_reg_read( e.handle(), id, &v );
		return v;
	}

	// CPUID (0F A2) of every (leaf, sub-leaf) row of the engine's profile, executed by the engine, against the file. Two bits
	// follow CR4 by architecture and are compared with the engine's CR4: "The value of CR4.OSXSAVE is returned in
	// CPUID.01H:ECX.OSXSAVE[27]" (253665-093 Vol1 13.3 p. 13-6) and "CPUID.07H.00H:ECX.OSPKE[4] returns the value of CR4.PKE"
	// (253668-093 Vol3A 5.1.4 p. 5-6); QEMU 11.1 target/i386/cpu.c:8655-8657 / :8792 compute them the same way.
	void check_profile_leaves( emu::cpu_profile_choice p )
	{
		auto e = open_engine( p );
		if ( !e ) return;
		CHECK( e->has_profile(), "a captured profile was loaded" );
		const auto& rows = e->profile().leaves;
		size_t count = 0;
		uc_ctl_get_x86_cpuid_count( e->handle(), &count );
		CHECK( count == rows.size(), "the fork holds every profile row (UC_CTL_X86_CPUID count)" );
		if ( !put_code( *e, { 0x0F, 0xA2 } ) ) return;
		const uint64_t cr4 = get64( *e, UC_X86_REG_CR4 );
		size_t same = 0, cr4_bits = 0, bad = 0;
		for ( const auto& r : rows )
		{
			set64( *e, UC_X86_REG_RAX, r.leaf );
			set64( *e, UC_X86_REG_RCX, r.subleaf );
			set64( *e, UC_X86_REG_RBX, 0 );
			set64( *e, UC_X86_REG_RDX, 0 );
			const uc_err ue = e->emulate( st_code, st_code + 2, 1 );
			uint32_t want[ 4 ] = { r.eax, r.ebx, r.ecx, r.edx };
			if ( r.leaf == 1 && r.subleaf == 0 )
			{
				want[ 2 ] = ( want[ 2 ] & ~( 1u << 27 ) ) | ( ( cr4 >> 18 ) & 1 ? 1u << 27 : 0 );
				cr4_bits++;
			}
			if ( r.leaf == 7 && r.subleaf == 0 )
			{
				want[ 2 ] = ( want[ 2 ] & ~( 1u << 4 ) ) | ( ( cr4 >> 22 ) & 1 ? 1u << 4 : 0 );
				cr4_bits++;
			}
			const uint32_t got[ 4 ] = { uint32_t( get64( *e, UC_X86_REG_RAX ) ), uint32_t( get64( *e, UC_X86_REG_RBX ) ),
										uint32_t( get64( *e, UC_X86_REG_RCX ) ), uint32_t( get64( *e, UC_X86_REG_RDX ) ) };
			bool ok = ue == UC_ERR_OK;
			for ( int k = 0; k < 4; k++ )
				if ( got[ k ] != want[ k ] )
				{
					ok = false;
					int nbits = 0;
					for ( uint32_t x = got[ k ] ^ want[ k ]; x; x &= x - 1 ) nbits++;
					std::cout << "  leaf " << std::hex << r.leaf << "H." << r.subleaf << std::dec << " " << "abcd"[ k ] << "x: " << nbits
							  << " bit(s) differ from the profile (values not printed)\n";
				}
			if ( ok ) same++; else bad++;
		}
		std::cout << "  " << emu::name_of( p ) << ": " << rows.size() << " profile rows executed by the engine: " << same << " equal, " << bad
				  << " differing (" << cr4_bits << " row(s) with the CR4-reported bits OSXSAVE / OSPKE compared with CR4)\n";
		CHECK( bad == 0 && same == rows.size() && same > 0, "every captured (leaf, sub-leaf) row returned exactly" );
	}

	void t_engine_profile_host() { check_profile_leaves( emu::cpu_profile_choice::host ); }
	void t_engine_profile_guest() { check_profile_leaves( emu::cpu_profile_choice::guest ); }

	// the profile is installed at uc_open: the fork's reset XCR0 comes from the profile's leaf 0DH only when the profile
	// was installed before the engine initialised (fork U1530: 7H for both i5-13600K captures, as Windows); --max has none
	void t_engine_profile_at_open()
	{
		for ( auto p : { emu::cpu_profile_choice::host, emu::cpu_profile_choice::guest } )
		{
			auto e = open_engine( p );
			if ( !e ) continue;
			int strict = -1;
			uc_ctl_get_x86_cpuid_strict( e->handle(), &strict );
			CHECK( strict == 1, std::string( emu::name_of( p ) ) + ": strict on (the fork's default with a profile, U435)" );
			CHECK( get64( *e, UC_X86_REG_XCR0 ) == 7, std::string( emu::name_of( p ) ) + ": reset XCR0 = 7H from the captured leaf 0DH (installed before init)" );
			CHECK( e->profile().has_microcode, std::string( emu::name_of( p ) ) + ": the profile's microcode line was read" );
			const std::string file = emu::profile_file_name( p );
			CHECK( e->profile().path.size() >= file.size() && e->profile().path.compare( e->profile().path.size() - file.size(), file.size(), file ) == 0,
				   "the profile file of the choice: " + file );
		}
		auto m = open_engine( emu::cpu_profile_choice::max );
		if ( m )
		{
			size_t count = 1;
			int strict = -1;
			uc_ctl_get_x86_cpuid_count( m->handle(), &count );
			uc_ctl_get_x86_cpuid_strict( m->handle(), &strict );
			CHECK( !m->has_profile() && count == 0 && strict == 0, "--max: no profile, strict off" );
		}
		emu::engine_config rc;
		rc.profile = emu::cpu_profile_choice::runtime_cpuid;
		std::string err;
		CHECK( !emu::engine::open( rc, err ) && err.find( "--cpuid-runtime" ) != std::string::npos, "--cpuid-runtime refused in M2: " + err );
		emu::engine_config bad;
		bad.data_dir = "Z:\\no\\such\\folder\\for\\emu-selftest";
		CHECK( !emu::engine::open( bad, err ) && err.find( "cannot open" ) != std::string::npos, "a missing profile file fails loudly: " + err );
		// the options -> config mapping (default --host --seeded)
		auto po = parse( emu::tool_kind::oep, { "--guest", "--HostSeed", "x.exe" } );
		const auto pc = emu::config_from( po.options );
		CHECK( pc.profile == emu::cpu_profile_choice::guest && pc.rng == emu::rng_source::host_seed, "config_from(--guest --HostSeed)" );
		const auto dc = emu::config_from( parse( emu::tool_kind::oep, { "x.exe" } ).options );
		CHECK( dc.profile == emu::cpu_profile_choice::host && dc.rng == emu::rng_source::seeded, "config_from defaults = --host --seeded" );
	}

	// the strict parser of the profile format (in memory; no file)
	void t_engine_profile_parse()
	{
		emu::cpuid_profile p;
		std::string err;
		CHECK( emu::parse_cpuid_profile( "# c\r\n0 0 16 756E6547 6C65746E 49656E69\r\n#@ microcode 1a2b platform-id 3\r\n\r\n7 1 0 0 0 0\n", p, err ), err );
		CHECK( p.leaves.size() == 2 && p.leaves[ 0 ].eax == 0x16 && p.leaves[ 1 ].leaf == 7 && p.leaves[ 1 ].subleaf == 1, "two rows" );
		CHECK( p.has_microcode && p.microcode_rev == 0x1a2b && p.microcode_platform_id == 3, "microcode line" );
		const char* bad[] = { "0 0 1 2 3\n", "0 0 1 2 3 4 5\n", "0 0 1 2 3 0x4\n", "0 0 1 2 3 123456789\n", "0 0 1 2 3 4\n0 0 1 2 3 4\n",
							  "#@ microcode 1 platform-id 8\n0 0 1 2 3 4\n", "#@ microcode 1 platform-id 1\n#@ microcode 1 platform-id 1\n0 0 1 2 3 4\n",
							  "#@ microcode zz platform-id 1\n0 0 1 2 3 4\n", "# only comments\n", "", "0 0 1 2 3 -4\n" };
		for ( auto b : bad )
		{
			emu::cpuid_profile q;
			std::string why;
			CHECK( !emu::parse_cpuid_profile( b, q, why ) && !why.empty(), std::string( "refused: \"" ) + b + "\" (" + why + ")" );
		}
		std::string big( size_t( emu::max_profile_bytes ) + 1, '#' );
		CHECK( !emu::parse_cpuid_profile( big, p, err ), "above max_profile_bytes refused" );
		std::string many;
		for ( size_t i = 0; i <= emu::max_profile_rows; i++ ) many += std::to_string( i ) + " 0 0 0 0 0\n";
		CHECK( !emu::parse_cpuid_profile( many, p, err ) && err.find( "more than" ) != std::string::npos, "more than max_profile_rows refused: " + err );
	}

	// plan 1.J.47.a: a strict profile only at uc_open - accepted again before the first instruction, UC_ERR_ARG after it
	void t_engine_strict_after_first_insn()
	{
		auto e = open_engine( emu::cpu_profile_choice::host );
		if ( !e ) return;
		const auto rows = e->profile().leaves;
		CHECK( e->install_cpuid_profile( rows ) == UC_ERR_OK, "before the first instruction: accepted (positive control)" );
		if ( !put_code( *e, { 0x90, 0x90 } ) ) return;
		const uint64_t xcr0 = get64( *e, UC_X86_REG_XCR0 );
		CHECK( e->emulate( st_code, st_code + 1, 1 ) == UC_ERR_OK && get64( *e, UC_X86_REG_RIP ) == st_code + 1, "one NOP executed" );
		CHECK( e->code_ran(), "the engine knows code ran" );
		CHECK( e->install_cpuid_profile( rows ) == UC_ERR_ARG, "after the first instruction: UC_ERR_ARG (1.J.47.a)" );
		int strict = -1;
		size_t count = 0;
		uc_ctl_get_x86_cpuid_strict( e->handle(), &strict );
		uc_ctl_get_x86_cpuid_count( e->handle(), &count );
		CHECK( strict == 1 && count == rows.size() && get64( *e, UC_X86_REG_XCR0 ) == xcr0, "the refused write changed nothing" );

		auto m = open_engine( emu::cpu_profile_choice::max );
		if ( !m || !put_code( *m, { 0x90 } ) ) return;
		CHECK( m->emulate( st_code, st_code + 1, 1 ) == UC_ERR_OK, "--max: one NOP executed" );
		CHECK( m->install_cpuid_profile( rows ) == UC_ERR_ARG, "--max: the host profile after the first instruction: UC_ERR_ARG" );
		uc_ctl_get_x86_cpuid_count( m->handle(), &count );
		CHECK( count == 0, "--max: still no profile" );
		const bool logged = std::any_of( m->log().begin(), m->log().end(), []( const std::string& l ) { return l.find( "UC_ERR_ARG" ) != std::string::npos ||
																										 l.find( "Invalid argument" ) != std::string::npos; } );
		CHECK( logged, "the refusal is in the verbose log" );
	}

	// decision D8: --seeded = the fork's deterministic model (U835: value n = SplitMix64(seed + n * 9E3779B97F4A7C15H), n = 1, 2,
	// ...), CF = 1 and OF SF ZF AF PF = 0 ("CF=1 indicates that the data in the destination is valid", "The OF, SF, ZF, AF,
	// and PF flags are set to 0": 253667-093 Vol2B RDRAND p. 4-554); --HostSeed selects the host source (not executed here)
	uint64_t splitmix64( uint64_t seed, uint64_t n )
	{
		uint64_t z = seed + n * 0x9e3779b97f4a7c15ull;
		z = ( z ^ ( z >> 30 ) ) * 0xbf58476d1ce4e5b9ull;
		z = ( z ^ ( z >> 27 ) ) * 0x94d049bb133111ebull;
		return z ^ ( z >> 31 );
	}

	void t_engine_rng()
	{
		for ( uint64_t seed : { 0ull, 0x5eedull } )
		{
			auto e = open_engine( emu::cpu_profile_choice::host, emu::rng_source::seeded, seed );
			if ( !e || !put_code( *e, { 0x48, 0x0F, 0xC7, 0xF0 } ) ) return;   // RDRAND RAX
			int mode = -1;
			uint64_t s = 1;
			uc_ctl_get_x86_rdrand( e->handle(), &mode, &s );
			CHECK( mode == UC_X86_RDRAND_SEEDED && s == seed, "--seeded mode and seed read back" );
			for ( uint64_t n = 1; n <= 3; n++ )
			{
				set64( *e, UC_X86_REG_RFLAGS, 0x8D5 );   // OF SF ZF AF PF CF preset to 1 (bit 1 reserved)
				CHECK( e->emulate( st_code, st_code + 4, 1 ) == UC_ERR_OK, "RDRAND executed" );
				CHECK( get64( *e, UC_X86_REG_RAX ) == splitmix64( seed, n ), "value " + std::to_string( n ) + " = the seeded model" );
				CHECK( ( get64( *e, UC_X86_REG_RFLAGS ) & 0x8D5 ) == 1, "CF = 1, OF SF ZF AF PF = 0 (SDM RDRAND)" );
			}
		}
		auto h = open_engine( emu::cpu_profile_choice::host, emu::rng_source::host_seed );
		if ( !h ) return;
		int mode = -1;
		uint64_t s = 0;
		uc_ctl_get_x86_rdrand( h->handle(), &mode, &s );
		CHECK( mode == UC_X86_RDRAND_HOST, "--HostSeed selects UC_X86_RDRAND_HOST" );
		CHECK( std::any_of( h->log().begin(), h->log().end(), []( const std::string& l ) { return l.find( "--HostSeed" ) != std::string::npos; } ),
			   "--HostSeed in the verbose log" );
	}

	// the dump of a reset engine equals the fork's uc_context values (every field through uc_context_reg_read2)
	void t_reg_dump_reset_context()
	{
		for ( auto p : { emu::cpu_profile_choice::host, emu::cpu_profile_choice::guest, emu::cpu_profile_choice::max } )
		{
			auto e = open_engine( p );
			if ( !e ) continue;
			emu::reg::dump live, saved;
			std::string err;
			CHECK( emu::reg::read( e->handle(), live, err ), err );
			uc_context* ctx = nullptr;
			CHECK( uc_context_alloc( e->handle(), &ctx ) == UC_ERR_OK && uc_context_save( e->handle(), ctx ) == UC_ERR_OK, "uc_context_save" );
			CHECK( emu::reg::read( ctx, saved, err ), err );
			const auto d = emu::reg::diff( live, saved );
			std::cout << "  " << emu::name_of( p ) << ": " << emu::reg::fields().size() << " dump fields, " << d.size() << " differ from the uc_context\n";
			for ( auto& n : d ) std::cout << "    differs: " << n << "\n";
			CHECK( d.empty() && live.v.size() == emu::reg::fields().size(), "reset dump == uc_context values" );
			// XMMn / YMMn are the low 128 / 256 bits of ZMMn
			bool low = true;
			for ( int i = 0; i < 32; i++ )
			{
				uint8_t z[ 64 ], x[ 16 ], y[ 32 ];
				std::string zn = "zmm" + std::to_string( i );
				emu::reg::get_bytes( live, zn.c_str(), z, 64 );
				uc_reg_read( e->handle(), UC_X86_REG_XMM0 + i, x );
				uc_reg_read( e->handle(), UC_X86_REG_YMM0 + i, y );
				low = low && std::memcmp( z, x, 16 ) == 0 && std::memcmp( z, y, 32 ) == 0;
			}
			CHECK( low, "XMM / YMM reads = the low parts of the ZMM dump" );
			if ( p == emu::cpu_profile_choice::host )
				for ( auto& l : emu::reg::format( live ) )
					if ( l.rfind( "rip", 0 ) == 0 || l.rfind( "rflags", 0 ) == 0 || l.rfind( "cr", 0 ) == 0 || l.rfind( "xcr0", 0 ) == 0 || l.rfind( "fpsw", 0 ) == 0 ||
						 l.rfind( "mxcsr", 0 ) == 0 || l.rfind( "cs ", 0 ) == 0 || l.rfind( "gdtr", 0 ) == 0 )
						std::cout << "    reset " << l << "\n";
			uc_context_free( ctx );
		}
	}

	// dump round trip: text (format -> parse) and engine (write_user_state -> read) on a fresh engine
	void t_reg_dump_round_trip()
	{
		auto e = open_engine( emu::cpu_profile_choice::host );
		if ( !e ) return;
		emu::reg::dump a, b, c;
		std::string err;
		CHECK( emu::reg::read( e->handle(), a, err ), err );
		// a pattern in every user-writable field (valid values: RFLAGS with bit 1, MXCSR without reserved bits)
		uint64_t x = 0x0123456789ABCDEFull;
		for ( const auto& f : emu::reg::fields() )
		{
			if ( !f.writable ) continue;
			uint8_t buf[ 64 ] = {};
			for ( size_t i = 0; i < emu::reg::byte_size( f.k ); i++ )
			{
				x = x * 6364136223846793005ull + 1442695040888963407ull;
				buf[ i ] = uint8_t( x >> 56 );
			}
			emu::reg::set_bytes( a, f.name, buf, emu::reg::byte_size( f.k ) );
		}
		emu::reg::set_u64( a, "rflags", 0xCD7 );   // CF PF AF ZF SF DF OF + bit 1
		emu::reg::set_u64( a, "mxcsr", 0x7FBF );   // flags, DAZ, masks, RC = 11b, FTZ = 0
		emu::reg::set_u64( a, "fpcw", 0x037F );
		emu::reg::set_u64( a, "rip", st_code );
		const auto text = emu::reg::format( a );
		CHECK( emu::reg::parse( text, b, err ), err );
		CHECK( emu::reg::diff( a, b ).empty(), "text round trip exact (" + std::to_string( text.size() ) + " lines)" );
		CHECK( emu::reg::write_user_state( e->handle(), a, err ), err );
		CHECK( emu::reg::read( e->handle(), c, err ), err );
		const auto d = emu::reg::diff( a, c );
		std::vector<std::string> unexpected;
		for ( auto& n : d )
			if ( n != "fptag" ) unexpected.push_back( n );   // the abridged tag follows R0-R7 (derived on read)
		for ( auto& n : unexpected ) std::cout << "    round trip differs: " << n << "\n";
		CHECK( unexpected.empty(), "engine round trip: every field reads back as written (" + std::to_string( d.size() ) + " differing incl. fptag)" );
		std::vector<std::string> bad_lines = { "rax = 0x1" };
		CHECK( !emu::reg::parse( bad_lines, b, err ), "a dump with missing fields is refused: " + err );
		auto t2 = text;
		t2.push_back( "rax = 0x1" );
		CHECK( !emu::reg::parse( t2, b, err ), "a repeated field is refused: " + err );
		auto t3 = text;
		t3[ 0 ] = "rax = 0x10000000000000000";
		CHECK( !emu::reg::parse( t3, b, err ), "a value above 64 bits is refused: " + err );
		auto t4 = text;
		t4.push_back( "nosuchreg = 0x1" );
		CHECK( !emu::reg::parse( t4, b, err ), "an unknown field is refused: " + err );
	}

	// the memory map: named page-aligned regions, bounds-checked, equal to Unicorn's view
	void t_engine_memory_map()
	{
		auto e = open_engine( emu::cpu_profile_choice::host );
		if ( !e ) return;
		std::string err;
		CHECK( e->map( 0x10000, 0x2000, UC_PROT_READ | UC_PROT_WRITE, "data", err ), err );
		CHECK( e->map( 0x400000, 0x1000, UC_PROT_ALL, "code", err ), err );
		CHECK( e->map( 0xFFFFFFFFFFFFE000ull, 0x2000, UC_PROT_READ, "top", err ), "the last two pages below 2^64: " + err );
		CHECK( !e->map( 0x30001, 0x1000, UC_PROT_READ, "unaligned", err ), "unaligned base refused" );
		CHECK( !e->map( 0x30000, 0x800, UC_PROT_READ, "half", err ), "unaligned size refused" );
		CHECK( !e->map( 0x30000, 0, UC_PROT_READ, "empty", err ), "size 0 refused" );
		CHECK( !e->map( 0xFFFFFFFFFFFFF000ull, 0x2000, UC_PROT_READ, "wrap", err ), "a region past 2^64 refused" );
		CHECK( !e->map( 0x11000, 0x1000, UC_PROT_READ, "overlap", err ), "overlap refused" );
		CHECK( !e->map( 0x30000, 0x1000, 8, "prot", err ), "unknown protection bit refused" );
		CHECK( !e->map( 0x100000000ull, 1ull << 40, UC_PROT_READ, "huge", err ), "more than max_mapped_bytes refused" );
		CHECK( e->check_regions( err ), "engine table == uc_mem_regions: " + err );
		CHECK( e->regions().size() == 3 && e->regions()[ 0 ].name == "data" && e->regions()[ 2 ].name == "top", "sorted named table" );
		uint8_t w[ 16 ] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }, r[ 16 ] = {};
		CHECK( e->write( 0x11FF8, w, 16, err ) == false, "write crossing the end of a region refused" );
		CHECK( e->write( 0x10FF8, w, 16, err ) && e->read( 0x10FF8, r, 16, err ) && std::memcmp( w, r, 16 ) == 0, "write / read across two pages: " + err );
		CHECK( !e->read( 0x30000, r, 1, err ), "unmapped read refused" );
		CHECK( !e->read( 0xFFFFFFFFFFFFFFF8ull, r, 16, err ), "a read wrapping 2^64 refused: " + err );
		CHECK( !e->read( 0x10000, r, emu::max_io_bytes + 1, err ), "a read above max_io_bytes refused" );
		CHECK( e->read( 0xFFFFFFFFFFFFFFF0ull, r, 16, err ), "the last 16 bytes below 2^64: " + err );
		CHECK( !e->unmap( 0x10000, 0x1000, err ), "unmap of part of a region refused" );
		CHECK( e->unmap( 0x400000, 0x1000, err ) && e->check_regions( err ) && e->regions().size() == 2, "unmap a whole region: " + err );
		for ( auto& l : e->describe_memory_map() ) std::cout << "  " << l << "\n";
	}

	// the verbose log names every step of open
	void t_engine_verbose_log()
	{
		auto e = open_engine( emu::cpu_profile_choice::guest );
		if ( !e ) return;
		for ( auto& l : e->log() ) std::cout << "  log: " << l << "\n";
		for ( auto key : { "milestone M3", "cpu profile: guest", "cpuid profile file:", "uc_open(UC_ARCH_X86, UC_MODE_64) ok", "cpu model: UC_CPU_X86_MAX",
						   "CPUID profile installed at uc_open", "rng: --seeded", "reset state (fork values): rip", "engine controls read back" } )
			CHECK( std::any_of( e->log().begin(), e->log().end(), [ & ]( const std::string& l ) { return l.find( key ) != std::string::npos; } ),
				   std::string( "log line with \"" ) + key + "\"" );
		auto m = open_engine( emu::cpu_profile_choice::max );
		if ( m )
			CHECK( std::any_of( m->log().begin(), m->log().end(), []( const std::string& l ) { return l.find( "no CPUID profile (--max)" ) != std::string::npos; } ),
				   "--max logged" );
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

	// The runtime half of no_native_check (AGENT_RULES "Host safety"; plan 3.M1, extended by 3.M2 / U1954): (1) before any
	// engine: no executable memory outside mapped images (no RWX heap, no shellcode buffer, no JIT) exists in this process;
	// (2) a guest-code buffer's bytes are found in readable memory (positive control) but in no executable memory; (3) M2:
	// with an engine open that has TRANSLATED AND RUN that guest code, the executable regions outside images are Unicorn's
	// own (its TCG code buffer and the VEH trampoline of its lazy commit, qemu/accel/tcg/translate-all.c alloc_code_gen_buffer)
	// - named here, they never hold the 64 guest-code bytes - and (4) every one of them is gone after uc_close, so all of them
	// belonged to the engine.
	void t_no_native_runtime()
	{
		const auto foreign = emu::host_guard::foreign_executable();
		std::cout << "  executable regions outside mapped images (no engine open): " << foreign.size() << "\n";
		for ( auto& r : foreign ) std::cout << "    " << emu::host_guard::describe( r ) << "\n";
		CHECK( foreign.empty(), "no executable private / mapped memory in the process while no engine is open (no JIT, no RWX heap)" );

		// 64 bytes of guest code made at run time: six MOV RAX, imm64 (48 B8 + 8 random bytes, xorshift64*) and four NOPs, so
		// the bytes exist only in this heap buffer (and, below, in the engine's guest memory)
		std::vector<uint8_t> guest_code;
		uint64_t x = 0x9E3779B97F4A7C15ull;
		for ( int i = 0; i < 6; i++ )
		{
			guest_code.push_back( 0x48 );
			guest_code.push_back( 0xB8 );
			for ( int k = 0; k < 8; k++ )
			{
				x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
				guest_code.push_back( uint8_t( ( x * 0x2545F4914F6CDD1Dull ) >> 56 ) );
			}
		}
		for ( int i = 0; i < 4; i++ ) guest_code.push_back( 0x90 );
		const uint64_t buf = uint64_t( reinterpret_cast<uintptr_t>( guest_code.data() ) );
		const auto in_exec = emu::host_guard::find_pattern( guest_code.data(), guest_code.size(), true );
		const auto in_any = emu::host_guard::find_pattern( guest_code.data(), guest_code.size(), false );
		std::cout << "  guest-code marker (64 bytes): " << in_any.size() << " copy(ies) in readable memory, " << in_exec.size()
				  << " in executable memory\n";
		CHECK( in_exec.empty(), "guest-code bytes never in host-executable memory" );
		CHECK( std::find( in_any.begin(), in_any.end(), buf ) != in_any.end(), "positive control: the scanner finds the marker in its own buffer" );

		{
			auto e = open_engine( emu::cpu_profile_choice::host );
			if ( e && put_code( *e, guest_code ) )
			{
				CHECK( e->emulate( st_code, st_code + guest_code.size(), 10 ) == UC_ERR_OK && get64( *e, UC_X86_REG_RIP ) == st_code + guest_code.size(),
					   "the engine translated and ran the 10 guest instructions" );
				const auto during = emu::host_guard::foreign_executable();
				std::cout << "  engine open after the run: " << during.size() << " executable region(s) outside images (Unicorn's TCG code buffer / "
						  << "VEH trampoline; translator output, never guest bytes)\n";
				for ( auto& r : during ) std::cout << "    engine: " << emu::host_guard::describe( r ) << "\n";
				const auto exec2 = emu::host_guard::find_pattern( guest_code.data(), guest_code.size(), true );
				const auto any2 = emu::host_guard::find_pattern( guest_code.data(), guest_code.size(), false );
				std::cout << "  guest-code marker with the engine open: " << any2.size() << " copy(ies) in readable memory, " << exec2.size()
						  << " in executable memory\n";
				CHECK( exec2.empty(), "translated and run, the guest bytes are still in no host-executable region" );
				CHECK( any2.size() >= 2, "positive control: the own buffer and the engine's guest memory hold the bytes" );
			}
		}
		const auto after = emu::host_guard::foreign_executable();
		std::cout << "  after uc_close: " << after.size() << " executable region(s) outside images\n";
		for ( auto& r : after ) std::cout << "    " << emu::host_guard::describe( r ) << "\n";
		CHECK( after.empty(), "uc_close released every executable region the engine had (all of them were the engine's)" );
	}

	// ------------------------------------------------------------------------------------------------ M3 emu-snippet v1 (U1976)
	// Plan 3.M3: "--expect selftests fail before the executor exists" (RED on the stub executor), then pass. Every case runs
	// self-generated snippets on emu::snippet::run (one emu::engine each, decision 3.0.3 default --host unless a case says --max).
	emu::snippet::spec sn_spec( const std::string& asm_text, std::initializer_list<const char*> sets = {} )
	{
		emu::snippet::spec s;
		s.name = asm_text;
		s.auto_stack = true;
		std::string err;
		CHECK( emu::assemble( asm_text, s.code_address, s.code, err ), "assemble \"" + asm_text + "\": " + err );
		for ( auto a : sets )
		{
			emu::snippet::field_value f;
			CHECK( emu::snippet::parse_assignment( a, f, err ), std::string( a ) + ": " + err );
			s.regs.push_back( f );
		}
		return s;
	}
	void sn_expect( emu::snippet::spec& s, std::initializer_list<const char*> exps )
	{
		std::string err;
		for ( auto a : exps )
		{
			emu::snippet::field_value f;
			CHECK( emu::snippet::parse_assignment( a, f, err ), std::string( a ) + ": " + err );
			s.expect.regs.push_back( f );
		}
		s.expect.present = true;
		s.expect.label = "expect";
	}
	bool sn_run( const emu::snippet::spec& s, emu::snippet::result& r, const std::string& what )
	{
		const bool ok = emu::snippet::run( s, r );
		for ( auto& l : emu::snippet::format_result( s, r, false ) ) std::cout << "  [" << what << "] " << l << "\n";
		CHECK( ok, what + ": run: " + r.error );
		return ok;
	}
	uint64_t sn_u64( const emu::snippet::result& r, const char* name, bool final = true )
	{
		const auto& m = final ? r.final : r.start;
		auto it = m.find( name );
		uint64_t v = 0;
		if ( it != m.end() )
			for ( size_t i = 0; i < it->second.size() && i < 8; i++ ) v |= uint64_t( it->second[ i ] ) << ( 8 * i );
		return v;
	}

	// --expect passes on a right value and differs on a wrong one (loose and strict)
	void t_snippet_expect()
	{
		auto s = sn_spec( "mov eax, 5; add rax, rcx", { "rcx=7" } );
		sn_expect( s, { "rax=12", "rcx=7" } );
		emu::snippet::result r;
		if ( sn_run( s, r, "pass" ) )
		{
			CHECK( r.reason == emu::snippet::stop_reason::end && r.rip == s.code_address + s.code.size(), "stopped at the end of the code" );
			CHECK( r.expect_checked && r.expect_ok, "rax = 12 expected and found" );
			CHECK( sn_u64( r, "rcx", false ) == 7, "--set rcx=7 is the start value" );
			CHECK( r.counted && r.insns == 2, "two instructions counted" );
		}
		auto w = sn_spec( "mov eax, 5; add rax, rcx", { "rcx=7" } );
		sn_expect( w, { "rax=13" } );
		emu::snippet::result rw;
		if ( sn_run( w, rw, "wrong" ) )
			CHECK( rw.expect_checked && !rw.expect_ok && rw.expect_diffs.size() == 1 && rw.expect_diffs[ 0 ].find( "rax" ) == 0, "a wrong --expect is a DIFF naming rax" );
		// strict: an unlisted change (rdx) is a difference; listing it passes
		auto st = sn_spec( "mov eax, 1; mov edx, 2" );
		sn_expect( st, { "rax=1" } );
		st.expect.strict = true;
		emu::snippet::result rs;
		if ( sn_run( st, rs, "strict" ) )
			CHECK( !rs.expect_ok && rs.expect_diffs.size() == 1 && rs.expect_diffs[ 0 ].find( "rdx" ) == 0, "strict: the unlisted rdx change is reported" );
		sn_expect( st, { "rdx=2" } );
		emu::snippet::result rs2;
		if ( sn_run( st, rs2, "strict listed" ) ) CHECK( rs2.expect_ok, "strict with rdx listed passes" );
		// vector registers: byte form, xmm alias of zmm, partial bytes
		auto v = sn_spec( "movdqa xmm1, xmm0; pxor xmm2, xmm2", { "xmm0=00112233445566778899aabbccddeeff", "xmm2=ffff" } );
		sn_expect( v, { "xmm1=00112233445566778899aabbccddeeff", "zmm2=0000" } );
		emu::snippet::result rv;
		if ( sn_run( v, rv, "vector" ) ) CHECK( rv.expect_ok, "xmm1 copied, xmm2 cleared" );
	}

	// faults: the vector, the error code, the state at the faulting instruction; #PF access type and address
	void t_snippet_faults()
	{
		struct fcase { const char* code; const char* set; int vector; int64_t ec; int access; } cases[] = {
			{ "ud2", nullptr, 6, -1, -1 },
			{ "xor ecx, ecx; div rcx", nullptr, 0, -1, -1 },
			{ "int3", nullptr, 3, -1, -1 },
			{ "mov rax, qword ptr [rbx]", "rbx=0x12345000", 14, -1, 0 },
			{ "mov qword ptr [rbx], rax", "rbx=0x12345008", 14, -1, 1 },
			{ "cli", nullptr, -1, -1, -1 },   // CPL0 by default: allowed
		};
		for ( auto& c : cases )
		{
			auto s = c.set ? sn_spec( c.code, { c.set } ) : sn_spec( c.code );
			emu::snippet::result r;
			if ( !sn_run( s, r, c.code ) ) continue;
			if ( c.vector < 0 )
				CHECK( !r.faulted && r.reason == emu::snippet::stop_reason::end, std::string( c.code ) + ": no fault" );
			else
			{
				CHECK( r.faulted && r.reason == emu::snippet::stop_reason::fault && r.vector == c.vector, std::string( c.code ) + ": vector " + std::to_string( c.vector ) );
				if ( c.access >= 0 ) CHECK( r.pf_access == c.access && ( r.pf_address == 0x12345000 || r.pf_address == 0x12345008 ), std::string( c.code ) + ": #PF access / address" );
			}
		}
		// --expect-fault: right and wrong; #GP(0) with its error code (a non-canonical memory operand)
		auto g = sn_spec( "mov rax, qword ptr [rbx]", { "rbx=0x8000000000000000" } );
		std::string err;
		g.expect.present = true;
		g.expect.fault_given = true;
		CHECK( emu::snippet::parse_fault( "#GP(0)", g.expect.vector, g.expect.error_code, err ) && g.expect.vector == 13 && g.expect.error_code == 0, "parse #GP(0)" );
		emu::snippet::result rg;
		if ( sn_run( g, rg, "#GP(0)" ) ) CHECK( rg.expect_ok && rg.error_code == 0, "non-canonical load: #GP(0)" );
		g.expect.vector = 6;
		emu::snippet::result rg2;
		if ( sn_run( g, rg2, "#UD expected" ) ) CHECK( !rg2.expect_ok, "a wrong expected fault is a DIFF" );
		int vec = 0;
		int64_t ec = 0;
		for ( auto bad : { "#XX", "#GP(", "#GP(0x100000000)", "300" } ) CHECK( !emu::snippet::parse_fault( bad, vec, ec, err ), std::string( "bad fault token " ) + bad );
	}

	// SYSCALL / SYSENTER / INT n: policy stop = reported BEFORE they execute (nothing forwarded to the host); policy cpu = the
	// emulated CPU executes SYSCALL to the guest's own IA32_LSTAR
	void t_snippet_syscall()
	{
		for ( auto code : { "mov eax, 0x55; syscall; mov eax, 1", "mov eax, 0x55; sysenter; mov eax, 1", "mov eax, 0x55; int 0x2e; mov eax, 1" } )
		{
			auto s = sn_spec( code, { "rcx=0x1234" } );
			emu::snippet::result r;
			if ( !sn_run( s, r, code ) ) continue;
			CHECK( r.reason == emu::snippet::stop_reason::syscall && r.rip == s.code_address + 5 && r.syscall_number == 0x55, std::string( code ) + ": stopped at the instruction" );
			CHECK( sn_u64( r, "rip" ) == s.code_address + 5 && sn_u64( r, "rcx" ) == 0x1234 && sn_u64( r, "rax" ) == 0x55, "not executed: RIP at it, RCX / RAX unchanged" );
		}
		// cpu: SYSCALL with IA32_LSTAR = the label k inside the snippet (CPL0 engine, IA32_EFER.SCE set at reset)
		auto c = sn_spec( "mov ecx, 0xC0000082; lea rax, [rip + k]; mov rdx, rax; shr rdx, 32; wrmsr; syscall; k: mov ebx, 7" );
		c.syscall = emu::snippet::syscall_policy::cpu;
		sn_expect( c, { "rbx=7" } );
		emu::snippet::result r;
		if ( sn_run( c, r, "syscall cpu" ) )
			CHECK( r.reason == emu::snippet::stop_reason::end && r.expect_ok && sn_u64( r, "rcx" ) == c.code_address + c.code.size() - 5, "SYSCALL ran inside the engine: RCX = the return address, then k" );
	}

	// external transfers stop (policy stop) or fault (policy fault); a RET to the return sentinel is "returned"; ret depth; count;
	// stop address
	void t_snippet_stops()
	{
		auto x = sn_spec( "mov rax, 0x700000000000; call rax" );
		emu::snippet::result rx;
		if ( sn_run( x, rx, "external call" ) )
			CHECK( rx.reason == emu::snippet::stop_reason::external && rx.external_target == 0x700000000000ull && sn_u64( rx, "rsp" ) + 8 == sn_u64( rx, "rsp", false ),
				   "call to an unmapped address: external transfer, the return address pushed" );
		x.external = emu::snippet::external_policy::fault;
		emu::snippet::result rf;
		if ( sn_run( x, rf, "external fault" ) ) CHECK( rf.faulted && rf.vector == 14 && rf.pf_access == 8, "policy fault: #PF execute" );
		// a jump into mapped but not executable memory
		auto d = sn_spec( "mov rax, 0x500000; jmp rax" );
		emu::snippet::region rg;
		std::string err;
		CHECK( emu::snippet::parse_map( "0x500000:0x1000:rw", rg, err ), err );
		d.regions.push_back( rg );
		emu::snippet::result rd;
		if ( sn_run( d, rd, "non-exec jump" ) ) CHECK( rd.reason == emu::snippet::stop_reason::external && rd.external_target == 0x500000, "jump into rw- memory: external" );
		// ret depth 1: call f; f returns; then the outer ret ends the run (depth -1)
		auto rt = sn_spec( "call f; mov eax, 2; ret; f: mov eax, 1; ret" );
		rt.ret_depth_given = true;
		rt.ret_depth = 1;
		emu::snippet::result rr;
		if ( sn_run( rt, rr, "ret depth" ) ) CHECK( rr.reason == emu::snippet::stop_reason::ret_depth && sn_u64( rr, "rax" ) == 2 && rr.insns == 5, "stopped after the outer ret" );
		rt.ret_depth_given = false;
		emu::snippet::result rn;
		if ( sn_run( rt, rn, "returned" ) ) CHECK( rn.reason == emu::snippet::stop_reason::returned, "without --stop-ret-depth the outer ret returns to the sentinel" );
		auto lp = sn_spec( "l: inc rax; jmp l" );
		lp.max_insns = 100;
		emu::snippet::result rl;
		if ( sn_run( lp, rl, "count" ) ) CHECK( rl.reason == emu::snippet::stop_reason::count && sn_u64( rl, "rax" ) == 50, "the instruction bound stops an endless loop" );
		auto sa = sn_spec( "mov eax, 1; mov eax, 2; mov eax, 3" );
		sa.stop_at_given = true;
		sa.stop_at = sa.code_address + 5;
		emu::snippet::result ra;
		if ( sn_run( sa, ra, "stop at" ) ) CHECK( ra.reason == emu::snippet::stop_reason::end && sn_u64( ra, "rax" ) == 1, "--stop-at after the first instruction" );
	}

	// the per-instruction trace: address, bytes, disassembly, changed registers / flags, memory reads / writes
	void t_snippet_trace()
	{
		auto s = sn_spec( "mov rax, 5; push rax; pop rbx; add rbx, rax" );
		s.trace = true;
		emu::snippet::result r;
		if ( !sn_run( s, r, "trace" ) ) return;
		for ( auto& l : emu::snippet::format_result( s, r, false ) ) std::cout << "  " << l << "\n";
		CHECK( r.trace.size() == 4, "four trace entries" );
		if ( r.trace.size() == 4 )
		{
			CHECK( r.trace[ 0 ].address == s.code_address && r.trace[ 0 ].text.find( "mov rax, 5" ) == 0 && !r.trace[ 0 ].changes.empty() &&
					   r.trace[ 0 ].changes[ 0 ].find( "rax:" ) == 0, "entry 0: mov rax, 5 changes rax" );
			CHECK( !r.trace[ 1 ].mem.empty() && r.trace[ 1 ].mem[ 0 ].find( "write" ) == 0, "entry 1: push writes memory" );
			CHECK( !r.trace[ 2 ].mem.empty() && r.trace[ 2 ].mem[ 0 ].find( "read" ) == 0, "entry 2: pop reads memory" );
			bool flags = false;
			for ( auto& c : r.trace[ 3 ].changes )
				if ( c.find( "rflags:" ) == 0 && c.find( "PF" ) != std::string::npos ) flags = true;
			CHECK( flags, "entry 3: the changed flag is named" );
		}
	}

	// the state: x87 / SSE / AVX through the XRSTOR64 loader (XINUSE reports them in use), AVX-512 / K through the API, CPL3 by IRETQ
	void t_snippet_state()
	{
		// XGETBV(ECX = 1) = XCR0 AND XINUSE (253665-093 Vol1 13.6 p. 13-18: "Executing XGETBV with ECX = 1 returns in EDX:EAX the
		// logical-AND of XCR0 and the current value of the XINUSE state-component bitmap."); the loader's XRSTOR64 with XSTATE_BV = RFBM marks x87 / SSE / AVX in use
		auto s = sn_spec( "mov ecx, 1; xgetbv", { "xmm3=0102030405060708090a0b0c0d0e0f10", "ymm4=00000000000000000000000000000000c1c2c3c4c5c6c7c8d1d2d3d4d5d6d7d8",
												  "st1=4000:c000000000000000", "fpsw=0x3800", "mxcsr=0x9fc0" } );
		emu::snippet::result r;
		if ( sn_run( s, r, "xinuse" ) )
		{
			CHECK( ( sn_u64( r, "rax" ) & 7 ) == 7, "XINUSE[2:0] = 7 after the loader (x87, SSE, AVX in use)" );
			CHECK( sn_u64( r, "mxcsr", false ) == 0x9fc0 && sn_u64( r, "fpsw", false ) == 0x3800, "MXCSR and FSW (TOP = 7) loaded" );
			auto st1 = r.start.find( "st1" );
			CHECK( st1 != r.start.end() && st1->second[ 9 ] == 0x40 && st1->second[ 7 ] == 0xc0, "ST(1) = 4000:C000000000000000 relative to TOP" );
			CHECK( ( sn_u64( r, "ftw", false ) >> ( ( 7 + 1 ) & 7 ) ) & 1, "ST(1) given: its physical register (TOP + 1) tagged valid" );
		}
		auto z = sn_spec( "nop", { "k1=0xf0f0", "zmm17=" "0102030405060708091011121314151617181920212223242526272829303132"
														 "3334353637383940414243444546474849505152535455565758596061626364" } );
		emu::run_options zo;
		zo.avx512 = 1;
		z.config.profile = emu::cpu_profile_choice::max;
		z.config.avx512 = emu::config_from( zo ).avx512;   // the emu-alltest --avx512 mask
		sn_expect( z, { "k1=0xf0f0", "zmm17=0102030405060708091011121314151617181920212223242526272829303132"
											"3334353637383940414243444546474849505152535455565758596061626364" } );
		emu::snippet::result rz;
		if ( sn_run( z, rz, "avx512" ) ) CHECK( rz.expect_ok && sn_u64( rz, "k1", false ) == 0xf0f0, "K1 and ZMM17 (AVX-512 state) set through the API" );
		// CPL3: a Windows x64 GDT (as emu-alltest's cpl=3 page), entered by IRETQ with CS = 33h / SS = 2Bh
		auto c = sn_spec( "mov ax, cs; mov bx, ss; cli", { "cs=0x33", "ss=0x2b", "gdtr_base=0x600000", "gdtr_limit=0x37" } );
		c.cpl3 = true;
		emu::snippet::region g;
		std::string err;
		CHECK( emu::snippet::parse_map( "0x600000:0x1000:rw", g, err ), err );
		const uint64_t gdt[ 7 ] = { 0, 0, 0x00209B0000000000ull, 0x00CF93000000FFFFull, 0x00CFFB000000FFFFull, 0x00CFF3000000FFFFull, 0x0020FB0000000000ull };
		emu::snippet::mem_bytes b;
		b.address = 0x600000;
		b.bytes.assign( reinterpret_cast<const uint8_t*>( gdt ), reinterpret_cast<const uint8_t*>( gdt ) + sizeof( gdt ) );
		g.data.push_back( b );
		c.regions.push_back( g );
		emu::snippet::result rc;
		if ( sn_run( c, rc, "cpl3" ) )
		{
			CHECK( ( sn_u64( rc, "rax" ) & 0xFFFF ) == 0x33 && ( sn_u64( rc, "rbx" ) & 0xFFFF ) == 0x2b, "CS = 33h, SS = 2Bh at the snippet" );
			CHECK( rc.faulted && rc.vector == 13 && rc.error_code == 0, "CLI at CPL3 (IOPL 0): #GP(0)" );
		}
		// refusals: APX registers without APX, cs without cpl3, an unknown register
		emu::snippet::field_value f;
		CHECK( !emu::snippet::parse_assignment( "foo=1", f, err ), "unknown register refused" );
		CHECK( !emu::snippet::parse_assignment( "eflags=1", f, err ), "eflags is not a snippet register name (rflags)" );
		CHECK( !emu::snippet::parse_assignment( "ftw=0x100", f, err ), "a value wider than the register refused" );
		auto a = sn_spec( "nop", { "r20=1" } );
		emu::snippet::result ra;
		CHECK( !emu::snippet::run( a, ra ) && ra.error.find( "APX" ) != std::string::npos, "R20 without --apx refused: " + ra.error );
		auto cs = sn_spec( "nop", { "cs=0x33" } );
		emu::snippet::result rcs;
		CHECK( !emu::snippet::run( cs, rcs ) && rcs.error.find( "cpl3" ) != std::string::npos, "cs without state.cpl3 refused: " + rcs.error );
	}

	// JSON: the spec format, strict parsing and limits
	void t_snippet_json()
	{
		const char* doc = R"({"spec": 1, "name": "json case", "config": {"profile": "max"},
			"code": {"address": "0x401000", "asm": "mov rax, qword ptr [rsi]; add rax, 1; mov qword ptr [rsi + 8], rax"},
			"regions": [{"address": "0x800000", "size": "0x1000", "prot": "rw", "name": "data", "data": [{"offset": 0, "hex": "0807060504030201"}]}],
			"state": {"rsi": "0x800000", "rsp": "0x800800"},
			"stop": {"max_insns": 10}, "syscall": "stop",
			"expect": {"mode": "strict", "fault": null, "regs": {"rax": "0x0102030405060709", "rflags": "0x6"},
					   "mem": [{"address": "0x800008", "hex": "0907060504030201"}], "checked_mem": [{"address": "0x800000", "size": "0x1000"}]},
			"reference": {"source": "self", "fault": {"vector": -1}, "regs": {"rax": {"value": "0x0102030405060709", "mask": "0xffffffffffffffff"}},
						  "mem": [{"address": "0x800000", "hex": "08070605040302010907060504030201"}], "full_mem": [{"address": "0x800000", "size": "0x1000"}]}})";
		emu::json::value v;
		std::string err;
		CHECK( emu::json::parse( doc, v, err ), "parse: " + err );
		emu::snippet::spec s;
		CHECK( emu::snippet::spec_from_json( v, s, err ), "spec: " + err );
		emu::snippet::result r;
		if ( sn_run( s, r, "json" ) )
		{
			CHECK( r.expect_ok, "strict expectation from JSON passes" );
			CHECK( r.reference_ok, "reference (registers + full memory range) passes" );
		}
		// a reference that differs in one memory byte
		auto s2 = s;
		emu::snippet::result r2;
		CHECK( !s2.reference.mem.empty() && s2.reference.mem[ 0 ].bytes.size() > 8, "the reference memory was read from the JSON" );
		if ( !s2.reference.mem.empty() && s2.reference.mem[ 0 ].bytes.size() > 8 && ( s2.reference.mem[ 0 ].bytes[ 8 ] ^= 1, sn_run( s2, r2, "json ref diff" ) ) ) CHECK( !r2.reference_ok && r2.reference_diffs.size() == 1, "a one-byte reference memory difference is found" );
		// strict JSON
		const char* bad[] = { "{\"a\": 1,}", "{\"a\": 1.5}", "{\"a\": 1, \"a\": 2}", "[1 2]", "{\"a\": \"\\x\"}", "{\"a\": 01}", "{} x", "",
							  "{\"a\": \"\x01\"}", "{\"a\": tru}" };
		for ( auto b : bad )
		{
			emu::json::value x;
			CHECK( !emu::json::parse( b, x, err ) && err.find( "offset" ) != std::string::npos, std::string( "refused: " ) + b + " - " + err );
		}
		std::string deep( 100, '[' );
		deep += std::string( 100, ']' );
		emu::json::value dv;
		CHECK( !emu::json::parse( deep, dv, err ) && err.find( "nesting" ) != std::string::npos, "nesting depth limited: " + err );
		// spec refusals
		for ( auto b : { R"({"bogus": 1})", R"({"code": {"hex": "zz"}})", R"({"state": {"rax": "0x1g"}})", R"({"state": {"xmm40": "00"}})",
						 R"({"stop": {"max_insns": 0}})", R"({"expect": {"mode": "fuzzy"}})", R"({"spec": 2})", R"({"code": {"hex": "90", "asm": "nop"}})" } )
		{
			emu::json::value x;
			emu::snippet::spec t;
			CHECK( emu::json::parse( b, x, err ) && !emu::snippet::spec_from_json( x, t, err ), std::string( "spec refused: " ) + b + " - " + err );
		}
		uint64_t u = 0;
		emu::json::value n;
		CHECK( emu::json::parse( "18446744073709551615", n, err ) && emu::json::get_u64( n, u, err ) && u == UINT64_MAX, "2^64-1 as a JSON integer" );
		CHECK( emu::json::parse( "18446744073709551616", n, err ) && !emu::json::get_u64( n, u, err ), "2^64 refused" );
	}

	// the audited file input (emu::read_input_file / read_input_lines): read-only, bounded, loud
	void t_snippet_input_file()
	{
		std::string dir, err, text;
		if ( !emu::default_data_dir( dir, err ) ) { CHECK( false, err ); return; }
		const std::string prof = dir + "\\" + emu::profile_file_name( emu::cpu_profile_choice::host );
		CHECK( emu::read_input_file( prof, emu::max_profile_bytes, text, err ) && !text.empty(), "the bare-host profile read: " + err );
		CHECK( !emu::read_input_file( prof, 16, text, err ) && err.find( "outside" ) != std::string::npos, "a file above the bound refused: " + err );
		CHECK( !emu::read_input_file( dir + "\\no_such_file_for_emu_selftest.json", 1024, text, err ) && err.find( "cannot open" ) != std::string::npos, "missing file: " + err );
		uint64_t lines = 0;
		CHECK( emu::read_input_lines( prof, 4096, emu::max_profile_bytes, [ & ]( std::string_view, uint64_t ) { lines++; return true; }, err ) && lines > 10,
			   "read_input_lines: " + std::to_string( lines ) + " lines " + err );
		CHECK( !emu::read_input_lines( prof, 8, emu::max_profile_bytes, []( std::string_view, uint64_t ) { return true; }, err ) && err.find( "longer than" ) != std::string::npos,
			   "a line above the bound refused: " + err );
	}

	// the emu-snippet v1 options
	void t_snippet_options()
	{
		auto a = parse( emu::tool_kind::snippet, { "--asm", "nop", "--set", "rax=1", "--set", "xmm0=00ff", "--map", "0x1000:0x1000:rw", "--mem", "0x1000=0102", "--trace",
												   "--syscall", "cpu", "--external", "fault", "--expect-fault", "#UD", "--expect-strict", "--dump-mem", "0x1000:16", "--at", "0x401000",
												   "--avx512", "--apx", "--avx10", "2" } );
		CHECK( a.ok, a.error );
		auto& o = a.options;
		CHECK( o.sets.size() == 2 && o.maps.size() == 1 && o.mems.size() == 1 && o.trace && o.syscall == "cpu" && o.external == "fault" && o.expect_fault == "#UD" &&
				   o.expect_strict && o.dump_mems.size() == 1 && o.code_address_given && o.code_address == 0x401000 && o.avx512 && o.apx && o.avx10 == 2,
			   "every v1 option recorded" );
		const auto c = emu::config_from( o );
		CHECK( c.avx512 != 0 && c.apx == UC_X86_APX_F && c.avx10 == 2, "opt-ins mapped to the emu-alltest masks" );
		CHECK( parse( emu::tool_kind::snippet, { "--batch", "x.jsonl", "--quiet" } ).ok, "--batch with --quiet" );
		CHECK( !parse( emu::tool_kind::snippet, { "--batch", "x.jsonl", "--hex", "90" } ).ok, "--batch takes everything from its file" );
		CHECK( !parse( emu::tool_kind::snippet, { "--hex", "90", "--syscall", "maybe" } ).ok, "--syscall stop|cpu" );
		CHECK( parse( emu::tool_kind::snippet, { "--state", "x.json" } ).ok, "--state alone gives the code" );
		CHECK( !parse( emu::tool_kind::snippet, { "--hex", "90", "--set", "rax=zz" } ).ok, "bad --set value" );
		CHECK( parse( emu::tool_kind::snippet, { "--hex", "90", "--expect", "st0=3fff:8000000000000000" } ).ok, "st SEXP:MANT form" );
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
		{ "engine_profile_parse", t_engine_profile_parse },
		{ "engine_profile_at_open", t_engine_profile_at_open },
		{ "engine_profile_host", t_engine_profile_host },
		{ "engine_profile_guest", t_engine_profile_guest },
		{ "engine_strict_after_first_insn", t_engine_strict_after_first_insn },
		{ "engine_rng", t_engine_rng },
		{ "engine_memory_map", t_engine_memory_map },
		{ "engine_verbose_log", t_engine_verbose_log },
		{ "reg_dump_reset_context", t_reg_dump_reset_context },
		{ "reg_dump_round_trip", t_reg_dump_round_trip },
		{ "host_guard_regions", t_host_guard_regions },
		{ "no_native_runtime", t_no_native_runtime },
		{ "snippet_expect", t_snippet_expect },
		{ "snippet_faults", t_snippet_faults },
		{ "snippet_syscall", t_snippet_syscall },
		{ "snippet_stops", t_snippet_stops },
		{ "snippet_trace", t_snippet_trace },
		{ "snippet_state", t_snippet_state },
		{ "snippet_json", t_snippet_json },
		{ "snippet_input_file", t_snippet_input_file },
		{ "snippet_options", t_snippet_options },
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
