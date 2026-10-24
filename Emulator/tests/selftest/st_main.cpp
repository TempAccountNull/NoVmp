// emu-selftest (plan 3.M1 / 3.A 2.2, ledger U1940-U1941; plan 3.M2, ledger U1951-U1954): the Emulator lib's self-tests (Gate 2 = G3.A: "build.cmd builds
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
		CHECK( has_line( d, "milestone: M2" ), "milestone" );
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
		CHECK( std::string( emu::milestone() ) == "M2", "milestone M2" );
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
		for ( auto key : { "milestone M2", "cpu profile: guest", "cpuid profile file:", "uc_open(UC_ARCH_X86, UC_MODE_64) ok", "cpu model: UC_CPU_X86_MAX",
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
