// emu-selftest (plan 3.M1 / 3.A 2.2, ledger U1940-U1941; plan 3.M2, ledger U1951-U1954; plan 3.M3, ledger U1976; plan 3.M4, ledger U1985; plan 3.M5, ledger U2014): the Emulator lib's self-tests (Gate 2 = G3.A: "build.cmd builds
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
#include <emu/pe_image.hpp>
#include <emu/sysdll.hpp>
#include <emu/win_env.hpp>
#include <emu/nt_kernel.hpp>
#include <emu/hle.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // winnt.h IMAGE_* (the in-memory test PEs of the M4 cases)

#include <algorithm>
#include <array>
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
		// U1985 (plan 3.M4): the emu-oep M4 options
		auto dr = parse( emu::tool_kind::oep, { "--dry-run", "a.exe" } );
		CHECK( dr.ok && dr.options.dry_run, "--dry-run" );
		auto ib = parse( emu::tool_kind::oep, { "--image-base", "0x7ff700000000", "a.exe" } );
		CHECK( ib.ok && ib.options.image_base_given && ib.options.image_base == 0x7ff700000000ull, "--image-base: " + ib.error );
		CHECK( !parse( emu::tool_kind::oep, { "--image-base", "0x140000800", "a.exe" } ).ok, "--image-base not 4 KiB aligned refused" );
		CHECK( !parse( emu::tool_kind::oep, { "--image-base", "1", "--image-base", "2", "a.exe" } ).ok, "--image-base twice" );
		auto sq = parse( emu::tool_kind::oep, { "--guest", "--syscall-number", "NtClose" } );
		CHECK( sq.ok && sq.options.syscall_query == "NtClose" && sq.options.input_path.empty(), "--syscall-number without INPUT: " + sq.error );
		CHECK( !parse( emu::tool_kind::oep, { "--syscall-number", "NtClose", "a.exe" } ).ok, "--syscall-number with an INPUT refused" );
		auto sd = parse( emu::tool_kind::oep, { "--sysdlls", "cap\\dir", "a.exe" } );
		CHECK( sd.ok && sd.options.sysdlls_dir == "cap\\dir", "--sysdlls recorded" );
		CHECK( !parse( emu::tool_kind::snippet, { "--hex", "90", "--dry-run" } ).ok, "--dry-run is an emu-oep option" );
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
		CHECK( has_line( d, "input: in.exe (read as data, never loaded or run on the host)" ), "INPUT read as data (U1984)" );
		auto dd = emu::describe( parse( emu::tool_kind::oep, { "--dry-run", "in.exe" } ).options );
		CHECK( has_line( dd, "input: in.exe (recorded, not opened)" ) && has_line( dd, "--dry-run: nothing opened" ), "--dry-run: INPUT recorded, not opened" );
		CHECK( has_line( d, "out-dir: outd (recorded, not created)" ), "out-dir recorded, not created" );
		CHECK( has_line( d, "cpu profile: host (default, decision 3.0.3" ), "default profile named" );
		CHECK( has_line( d, "max-insns: 4096 (0x1000)" ), "max-insns decimal + hex" );
		CHECK( has_line( d, "milestone: M5" ), "milestone (U2013: M5)" );
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
		CHECK( std::string( emu::milestone() ) == "M5", "milestone M5 (U2013)" );
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
		for ( auto key : { "milestone M5", "cpu profile: guest", "cpuid profile file:", "uc_open(UC_ARCH_X86, UC_MODE_64) ok", "cpu model: UC_CPU_X86_MAX",
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

	// ------------------------------------------------------------------------------------------------ M4 win_env (U1985)
	// Plan 3.M4: a self-written 64-bit PE mapped as data, PEB / TEB / KUSER from the facts and the documented layouts, GS = TEB,
	// --Sys / --HLE recorded, a direct SYSCALL of NtTerminateProcess handled by the emulated kernel layer; loud stops with a state
	// dump; the PE parser fuzzed (host safety). Every image here is built in memory (helper prefix w4_); the syscall table and the
	// OS / platform facts are synthetic (no capture is needed: the capture-based run is suite win_env_t1).
	std::vector<uint8_t> w4_ntdll();

	struct w4_section
	{
		const char* name;
		uint32_t rva;
		std::vector<uint8_t> data;
		uint32_t characteristics;
		uint32_t vsize = 0;   // 0 = data size
	};
	struct w4_pe
	{
		uint64_t image_base = 0x140000000ull;
		uint32_t entry_rva = 0x1000;
		uint16_t file_characteristics = 0x22;   // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
		uint64_t stack_reserve = 0x100000;
		std::vector<w4_section> sections;
		std::array<std::pair<uint32_t, uint32_t>, 16> dirs{};
	};

	// a PE32+ file: headers 400H, FileAlignment 200H, SectionAlignment 1000H (winnt.h IMAGE_* structures)
	std::vector<uint8_t> w4_build( const w4_pe& p )
	{
		std::vector<uint8_t> f( 0x400, 0 );
		IMAGE_DOS_HEADER dos{};
		dos.e_magic = IMAGE_DOS_SIGNATURE;
		dos.e_lfanew = 0x80;
		std::memcpy( f.data(), &dos, sizeof( dos ) );
		const uint32_t sig = IMAGE_NT_SIGNATURE;
		std::memcpy( f.data() + 0x80, &sig, 4 );
		IMAGE_FILE_HEADER fh{};
		fh.Machine = IMAGE_FILE_MACHINE_AMD64;
		fh.NumberOfSections = uint16_t( p.sections.size() );
		fh.SizeOfOptionalHeader = sizeof( IMAGE_OPTIONAL_HEADER64 );
		fh.Characteristics = p.file_characteristics;
		std::memcpy( f.data() + 0x84, &fh, sizeof( fh ) );
		IMAGE_OPTIONAL_HEADER64 oh{};
		oh.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		oh.AddressOfEntryPoint = p.entry_rva;
		oh.ImageBase = p.image_base;
		oh.SectionAlignment = 0x1000;
		oh.FileAlignment = 0x200;
		oh.MajorSubsystemVersion = 6;
		uint32_t end = 0x1000;
		for ( auto& s : p.sections ) end = std::max<uint32_t>( end, s.rva + ( ( std::max<uint32_t>( s.vsize, uint32_t( s.data.size() ) ) + 0xFFF ) & ~0xFFFu ) );
		oh.SizeOfImage = end;
		oh.SizeOfHeaders = 0x400;
		oh.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
		oh.DllCharacteristics = 0x8160;
		oh.SizeOfStackReserve = p.stack_reserve;
		oh.SizeOfStackCommit = 0x1000;
		oh.NumberOfRvaAndSizes = 16;
		for ( int i = 0; i < 16; i++ ) oh.DataDirectory[ i ] = { p.dirs[ size_t( i ) ].first, p.dirs[ size_t( i ) ].second };
		std::memcpy( f.data() + 0x98, &oh, sizeof( oh ) );
		uint32_t raw = 0x400;
		for ( size_t i = 0; i < p.sections.size(); i++ )
		{
			const auto& s = p.sections[ i ];
			IMAGE_SECTION_HEADER sh{};
			std::memcpy( sh.Name, s.name, std::min<size_t>( 8, std::strlen( s.name ) ) );
			sh.Misc.VirtualSize = s.vsize ? s.vsize : uint32_t( s.data.size() );
			sh.VirtualAddress = s.rva;
			sh.SizeOfRawData = uint32_t( ( s.data.size() + 0x1FF ) & ~size_t( 0x1FF ) );
			sh.PointerToRawData = sh.SizeOfRawData ? raw : 0;
			sh.Characteristics = s.characteristics;
			std::memcpy( f.data() + 0x98 + sizeof( oh ) + i * sizeof( sh ), &sh, sizeof( sh ) );
			if ( sh.SizeOfRawData )
			{
				f.resize( raw + sh.SizeOfRawData, 0 );
				std::memcpy( f.data() + raw, s.data.data(), s.data.size() );
				raw += sh.SizeOfRawData;
			}
		}
		return f;
	}

	constexpr uint32_t w4_scn_text = 0x60000020, w4_scn_rdata = 0x40000040, w4_scn_data = 0xC0000040, w4_scn_reloc = 0x42000040;

	// T1's shape (Emulator\tests\pe\t1_exit\t1_exit.asm): mov rax, [rip -> .data pointer]; mov eax, [rax]; mov rcx, -1;
	// mov edx, EXIT; mov r10, rcx; syscall; ud2 - the number in .rdata, the pointer in .data (one DIR64 relocation)
	w4_pe w4_t1( uint32_t number, uint32_t exit_code = 0x2A, const std::vector<uint8_t>& code_override = {} )
	{
		w4_pe p;
		std::vector<uint8_t> code = { 0x48, 0x8B, 0x05, 0xF9, 0x1F, 0x00, 0x00, 0x8B, 0x00, 0x48, 0xC7, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF, 0xBA };
		for ( int k = 0; k < 4; k++ ) code.push_back( uint8_t( exit_code >> ( 8 * k ) ) );
		code.insert( code.end(), { 0x4C, 0x8B, 0xD1, 0x0F, 0x05, 0x0F, 0x0B } );
		if ( !code_override.empty() ) code = code_override;
		std::vector<uint8_t> rdata( 8, 0 ), data( 8, 0 );
		std::memcpy( rdata.data(), &number, 4 );
		const uint64_t ptr = p.image_base + 0x2000;
		std::memcpy( data.data(), &ptr, 8 );
		const std::vector<uint8_t> reloc = { 0x00, 0x30, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0xA0, 0x00, 0x00 };   // page 3000H, size CH, DIR64 at +0
		p.sections = { { ".text", 0x1000, code, w4_scn_text }, { ".rdata", 0x2000, rdata, w4_scn_rdata }, { ".data", 0x3000, data, w4_scn_data },
					   { ".reloc", 0x4000, reloc, w4_scn_reloc } };
		p.dirs[ 5 ] = { 0x4000, 0xC };
		return p;
	}

	emu::win::environment_facts w4_facts()
	{
		emu::win::environment_facts f;
		f.env = emu::win::environment::vmware_guest;
		f.os_major = 10;
		f.os_minor = 0;
		f.os_build = 0x4A64;
		f.product_type = 1;
		f.system_root = "C:\\Windows";
		f.processors = 8;
		f.processor_architecture = 9;
		f.total_phys = 0x400000000ull;
		f.min_app_address = 0x10000;
		f.max_app_address = 0x7FFFFFFEFFFFull;
		for ( auto [ n, name ] : { std::pair<uint32_t, const char*>{ 0x2C, "NtTerminateProcess" }, { 0x0F, "NtClose" }, { 0x18, "NtAllocateVirtualMemory" } } )
		{
			f.syscalls.by_number[ n ] = name;
			f.syscalls.by_name[ name ] = n;
		}
		f.syscalls.nt_exports = f.syscalls.stubs = 3;
		f.capture_dir = "(synthetic: emu-selftest)";
		return f;
	}

	struct w4_run
	{
		std::unique_ptr<emu::win::process> p;
		emu::win::run_result r;
		std::string error;
		bool ok = false;
	};

	w4_run w4_go( const std::vector<uint8_t>& file, emu::env_boundary b = emu::env_boundary::sys, bool rebase = false, uint64_t max_insns = 10000 )
	{
		w4_run w;
		emu::engine_config ec;
		ec.profile = emu::cpu_profile_choice::guest;
		emu::win::process_options po;
		po.boundary = b;
		po.image_base_given = rebase;
		po.image_base = 0x7FF700000000ull;
		po.max_insns = max_insns;
		po.image_name = "w4";
		std::vector<std::string> log;
		w.p = emu::win::process::create( ec, w4_facts(), po, file, log, w.error );
		if ( !w.p ) return w;
		w.ok = w.p->run( w.r, w.error );
		return w;
	}

	uint64_t w4_read( emu::win::process& p, uint64_t va, unsigned n )
	{
		uint64_t v = 0;
		std::string err;
		CHECK( p.eng().read( va, &v, n, err ), "guest read " + err );
		return v;
	}

	// the parser refuses each malformed header with its reason (and accepts the good one)
	void t_pe_parse_refusals()
	{
		const auto good = w4_build( w4_t1( 0x2C ) );
		emu::pe::image img;
		std::string err;
		CHECK( emu::pe::parse( good, img, err ), "the T1-shaped image parses: " + err );
		CHECK( img.sections.size() == 4 && img.sections[ 0 ].prot == ( UC_PROT_READ | UC_PROT_EXEC ) && img.sections[ 1 ].prot == UC_PROT_READ &&
				   img.sections[ 2 ].prot == ( UC_PROT_READ | UC_PROT_WRITE ),
			   "section protections from IMAGE_SCN_MEM_*" );
		struct bad_case { const char* why; std::function<void( std::vector<uint8_t>& )> mutate; const char* expect; };
		const bad_case bad[] = {
			{ "no MZ", []( std::vector<uint8_t>& f ) { f[ 0 ] = 'X'; }, "no MZ" },
			{ "e_lfanew outside", []( std::vector<uint8_t>& f ) { const int32_t v = 0x7FFFFFF0; std::memcpy( f.data() + 0x3C, &v, 4 ); }, "outside the file" },
			{ "negative e_lfanew", []( std::vector<uint8_t>& f ) { const int32_t v = -4; std::memcpy( f.data() + 0x3C, &v, 4 ); }, "negative" },
			{ "i386 machine", []( std::vector<uint8_t>& f ) { f[ 0x84 ] = 0x4C; f[ 0x85 ] = 0x01; }, "not AMD64" },
			{ "0 sections", []( std::vector<uint8_t>& f ) { f[ 0x86 ] = 0; f[ 0x87 ] = 0; }, "NumberOfSections" },
			{ "PE32 magic", []( std::vector<uint8_t>& f ) { f[ 0x98 ] = 0x0B; f[ 0x99 ] = 0x01; }, "not PE32+" },
			{ "SectionAlignment 200H", []( std::vector<uint8_t>& f ) { const uint32_t v = 0x200; std::memcpy( f.data() + 0x98 + 32, &v, 4 ); }, "SectionAlignment" },
			{ "SizeOfImage 0", []( std::vector<uint8_t>& f ) { const uint32_t v = 0; std::memcpy( f.data() + 0x98 + 56, &v, 4 ); }, "SizeOfImage" },
			{ "entry outside", []( std::vector<uint8_t>& f ) { const uint32_t v = 0x900000; std::memcpy( f.data() + 0x98 + 16, &v, 4 ); }, "AddressOfEntryPoint" },
			{ "17 directories", []( std::vector<uint8_t>& f ) { const uint32_t v = 17; std::memcpy( f.data() + 0x98 + 108, &v, 4 ); }, "NumberOfRvaAndSizes" },
			{ "sections overlap", []( std::vector<uint8_t>& f ) { const uint32_t v = 0x1000; std::memcpy( f.data() + 0x188 + 40 + 12, &v, 4 ); }, "overlaps" },
			{ "raw data outside", []( std::vector<uint8_t>& f ) { const uint32_t v = 0x10000; std::memcpy( f.data() + 0x188 + 20, &v, 4 ); }, "outside the file" },
			{ "truncated", []( std::vector<uint8_t>& f ) { f.resize( 0x90 ); }, "outside the file" },
		};
		for ( auto& b : bad )
		{
			auto f = good;
			b.mutate( f );
			emu::pe::image x;
			std::string why;
			const bool ok = emu::pe::parse( f, x, why );
			std::cout << "  " << b.why << ": " << ( ok ? "accepted" : why ) << "\n";
			CHECK( !ok && why.find( b.expect ) != std::string::npos, std::string( b.why ) + " refused with \"" + b.expect + "\": " + why );
		}
	}

	// host safety: deterministic mutations of the headers / section table and of an export table never crash the parser, the
	// export reader or the mapper; every outcome is "accepted" or a refusal with a reason
	void t_pe_fuzz()
	{
		const auto good = w4_build( w4_t1( 0x2C ) );
		uint64_t x = 0x2545F4914F6CDD1Dull, accepted = 0, refused = 0, mapped = 0, map_refused = 0;
		auto rnd = [ & ]() { x ^= x >> 12; x ^= x << 25; x ^= x >> 27; return x * 0x2545F4914F6CDD1Dull; };
		for ( int i = 0; i < 3000; i++ )
		{
			auto f = good;
			const int flips = 1 + int( rnd() % 6 );
			for ( int k = 0; k < flips; k++ ) f[ size_t( rnd() % 0x2B0 ) ] = uint8_t( rnd() );
			if ( rnd() % 16 == 0 ) f.resize( size_t( rnd() % f.size() ) );
			emu::pe::image img;
			std::string err;
			if ( !emu::pe::parse( f, img, err ) )
			{
				refused++;
				CHECK( !err.empty(), "a refusal names its reason" );
				continue;
			}
			accepted++;
			std::vector<emu::pe::export_entry> ex;
			emu::pe::exports( f, img, ex, err );
			if ( i % 25 == 0 && img.size_of_image <= 0x1000000 )   // a mutated SizeOfImage up to 1 GiB: map the small ones only (time)
			{
				emu::engine_config ec;
				ec.profile = emu::cpu_profile_choice::max;
				auto e = emu::engine::open( ec, err );
				emu::pe::mapping m;
				if ( e && emu::pe::map( *e, f, img, 0x7FF600000000ull, "fuzz", m, err ) ) mapped++;
				else map_refused++;
			}
		}
		std::cout << "  header fuzz: 3000 mutated images, " << accepted << " accepted, " << refused << " refused; mapped " << mapped << ", map refused " << map_refused << "\n";
		CHECK( accepted + refused == 3000 && refused > 1000, "every mutation ended in accept / refuse" );
		// the export reader on mutated synthetic ntdll images
		uint64_t ok_tables = 0, bad_tables = 0;
		const auto nt = w4_ntdll();
		for ( int i = 0; i < 2000; i++ )
		{
			auto f = nt;
			for ( int k = 0; k < 4; k++ ) f[ 0x600 + size_t( rnd() % 0x200 ) ] = uint8_t( rnd() );
			emu::win::syscall_table t;
			std::string err;
			if ( emu::win::parse_syscall_table( f, t, err ) ) ok_tables++;
			else bad_tables++;
		}
		std::cout << "  export / stub fuzz: 2000 mutated ntdll images, " << ok_tables << " tables, " << bad_tables << " refused\n";
		CHECK( ok_tables + bad_tables == 2000, "every export mutation ended in a table or a refusal" );
	}

	// mapped as data: headers R, sections with their protections, the bytes; another base applies the DIR64 relocation
	void t_pe_map_relocate()
	{
		const auto file = w4_build( w4_t1( 0x2C ) );
		emu::pe::image img;
		std::string err;
		CHECK( emu::pe::parse( file, img, err ), err );
		for ( uint64_t base : { 0x140000000ull, 0x7FF700000000ull } )
		{
			emu::engine_config ec;
			ec.profile = emu::cpu_profile_choice::max;
			auto e = emu::engine::open( ec, err );
			if ( !e ) { CHECK( false, err ); return; }
			emu::pe::mapping m;
			CHECK( emu::pe::map( *e, file, img, base, "t1", m, err ), "map at " + std::to_string( base ) + ": " + err );
			for ( auto& l : m.log ) std::cout << "  " << l << "\n";
			const auto regs = e->regions();
			CHECK( regs.size() == 5, "headers + 4 sections mapped" );
			const uint32_t want[] = { UC_PROT_READ, UC_PROT_READ | UC_PROT_EXEC, UC_PROT_READ, UC_PROT_READ | UC_PROT_WRITE, UC_PROT_READ };
			for ( size_t i = 0; i < regs.size() && i < 5; i++ ) CHECK( regs[ i ].prot == want[ i ] && regs[ i ].base == base + 0x1000 * i, "region " + regs[ i ].name );
			uint64_t ptr = 0;
			uint8_t first = 0;
			CHECK( e->read( base + 0x3000, &ptr, 8, err ) && e->read( base + 0x1000, &first, 1, err ) && first == 0x48, "image bytes in guest memory" );
			CHECK( ptr == base + 0x2000, "the DIR64 fixup: pointer = mapped base + 2000H" );
			CHECK( m.relocated == ( base != 0x140000000ull ) && m.relocations == ( base != 0x140000000ull ? 1u : 0u ), "relocation count" );
		}
		// refusals: an unmodelled relocation type, RELOCS_STRIPPED / no directory when not at the preferred base
		auto bad_type = w4_t1( 0x2C );
		bad_type.sections[ 3 ].data[ 9 ] = 0x50;   // type 5 (MIPS_JMPADDR) instead of 10
		auto stripped = w4_t1( 0x2C );
		stripped.file_characteristics |= IMAGE_FILE_RELOCS_STRIPPED;
		auto nodir = w4_t1( 0x2C );
		nodir.dirs[ 5 ] = { 0, 0 };
		for ( auto& [ pe, expect ] : { std::pair<w4_pe, const char*>{ bad_type, "relocation type 5" }, { stripped, "no base relocations" }, { nodir, "no base relocations" } } )
		{
			const auto f = w4_build( pe );
			emu::pe::image im;
			CHECK( emu::pe::parse( f, im, err ), err );
			emu::engine_config ec;
			ec.profile = emu::cpu_profile_choice::max;
			auto e = emu::engine::open( ec, err );
			emu::pe::mapping m;
			const bool ok = e && emu::pe::map( *e, f, im, 0x7FF700000000ull, "t1", m, err );
			std::cout << "  refused at another base: " << err << "\n";
			CHECK( !ok && err.find( expect ) != std::string::npos, std::string( "refused: " ) + expect );
		}
	}

	// a synthetic "ntdll": .text with three Nt* stubs and one non-stub, .rdata with the export directory (Zw* alias, a forwarder)
	std::vector<uint8_t> w4_ntdll_with( const std::vector<std::pair<std::string, std::vector<uint8_t>>>& funcs )
	{
		w4_pe p;
		p.image_base = 0x180000000ull;
		p.file_characteristics = 0x2022;
		std::vector<uint8_t> text;
		std::vector<uint32_t> rvas;
		for ( auto& fn : funcs )
		{
			rvas.push_back( 0x1000 + uint32_t( text.size() ) );
			text.insert( text.end(), fn.second.begin(), fn.second.end() );
			while ( text.size() % 32 ) text.push_back( 0xCC );
		}
		// export directory at 2000H: dir (40) | functions | names | ordinals | strings (names sorted as given)
		std::vector<uint8_t> ed( 40, 0 );
		const uint32_t n = uint32_t( funcs.size() );
		const uint32_t fo = 0x2000 + 40, no = fo + 4 * n, oo = no + 4 * n, so = oo + 2 * n;
		auto put32 = [ & ]( std::vector<uint8_t>& v, size_t at, uint32_t x ) { if ( v.size() < at + 4 ) v.resize( at + 4 ); std::memcpy( v.data() + at, &x, 4 ); };
		put32( ed, 16, 1 );   // Base
		put32( ed, 20, n );   // NumberOfFunctions
		put32( ed, 24, n );   // NumberOfNames
		put32( ed, 28, fo );
		put32( ed, 32, no );
		put32( ed, 36, oo );
		std::vector<uint8_t> strs;
		for ( uint32_t i = 0; i < n; i++ )
		{
			put32( ed, 40 + 4 * i, rvas[ i ] );
			put32( ed, ( no - 0x2000 ) + 4 * i, so + uint32_t( strs.size() ) );
			const uint16_t o = uint16_t( i );
			if ( ed.size() < ( oo - 0x2000 ) + 2 * i + 2 ) ed.resize( ( oo - 0x2000 ) + 2 * i + 2 );
			std::memcpy( ed.data() + ( oo - 0x2000 ) + 2 * i, &o, 2 );
			strs.insert( strs.end(), funcs[ i ].first.begin(), funcs[ i ].first.end() );
			strs.push_back( 0 );
		}
		ed.resize( so - 0x2000 );
		ed.insert( ed.end(), strs.begin(), strs.end() );
		p.sections = { { ".text", 0x1000, text, w4_scn_text }, { ".rdata", 0x2000, ed, w4_scn_rdata } };
		p.dirs[ 0 ] = { 0x2000, uint32_t( ed.size() ) };
		p.entry_rva = 0;
		return w4_build( p );
	}
	std::vector<uint8_t> w4_stub( uint32_t number )
	{
		std::vector<uint8_t> s = { 0x4C, 0x8B, 0xD1, 0xB8 };
		for ( int k = 0; k < 4; k++ ) s.push_back( uint8_t( number >> ( 8 * k ) ) );
		s.insert( s.end(), { 0x0F, 0x05, 0xC3 } );
		return s;
	}
	std::vector<uint8_t> w4_ntdll()
	{
		return w4_ntdll_with( { { "NtClose", w4_stub( 0x0F ) }, { "NtGetTickCount", { 0xB9, 0x20, 0x03, 0xFE, 0x7F, 0xC3 } }, { "NtTerminateProcess", w4_stub( 0x2C ) },
								{ "RtlNothing", w4_stub( 0x77 ) }, { "ZwClose", w4_stub( 0x0F ) } } );
	}

	// plan 6.4.b: the numbers come from the stubs (never hardcoded); a broken table is refused
	void t_sysdll_syscall_table()
	{
		emu::win::syscall_table t;
		std::string err;
		CHECK( emu::win::parse_syscall_table( w4_ntdll(), t, err ), "the synthetic ntdll parses: " + err );
		CHECK( t.stubs == 2 && t.nt_exports == 3 && t.by_name.count( "NtTerminateProcess" ) && t.by_name.at( "NtTerminateProcess" ) == 0x2C &&
				   t.by_number.count( 0x0F ) && t.by_number.at( 0x0F ) == "NtClose" && !t.by_number.count( 0x77 ),
			   "2 stubs of 3 Nt* exports (NtGetTickCount is no stub, RtlNothing / ZwClose are not Nt*)" );
		const struct { std::vector<std::pair<std::string, std::vector<uint8_t>>> funcs; const char* expect; } bad[] = {
			{ { { "NtA", w4_stub( 0x10 ) }, { "NtB", w4_stub( 0x10 ) } }, "both load service number" },
			{ { { "NtA", w4_stub( 0x1010 ) } }, "outside the NT table" },
			{ { { "NtA", { 0x90, 0xC3 } } }, "no system-service stub" },
		};
		for ( auto& b : bad )
		{
			emu::win::syscall_table x;
			std::string why;
			const bool ok = emu::win::parse_syscall_table( w4_ntdll_with( b.funcs ), x, why );
			std::cout << "  refused: " << why << "\n";
			CHECK( !ok && why.find( b.expect ) != std::string::npos, std::string( "refused with \"" ) + b.expect + "\": " + why );
		}
		CHECK( emu::win::environment_of( emu::cpu_profile_choice::guest ) == emu::win::environment::vmware_guest &&
				   emu::win::environment_of( emu::cpu_profile_choice::host ) == emu::win::environment::bare_host &&
				   emu::win::environment_of( emu::cpu_profile_choice::max ) == emu::win::environment::none,
			   "environments of the profile choices (decision 3.0.8)" );
		std::string dir;
		CHECK( !emu::win::locate_capture( emu::win::environment::none, "", ".", dir, err ), "--max has no environment" );
		CHECK( !emu::win::locate_capture( emu::win::environment::vmware_guest, "C:\\no_such_capture_dir_for_emu_selftest", ".", dir, err ) && err.find( "manifest.txt" ) != std::string::npos,
			   "an explicit folder without a manifest is refused: " + err );
	}

	// PEB / TEB / KUSER fields in guest memory (offsets: emu::win::layout, checked against the SDK in win_env.cpp / check_win_layout.py)
	void t_win_env_layout()
	{
		auto w = w4_go( w4_build( w4_t1( 0x2C ) ) );
		CHECK( w.p && w.ok, "process: " + w.error );
		if ( !w.p ) return;
		namespace L = emu::win::layout;
		auto& p = *w.p;
		const uint64_t teb = p.teb(), peb = p.peb(), k = emu::win::guest_kuser_va;
		CHECK( w4_read( p, teb + L::teb_self, 8 ) == teb && w4_read( p, teb + L::teb_peb, 8 ) == peb, "TEB.Self = TEB, TEB.ProcessEnvironmentBlock = PEB" );
		CHECK( w4_read( p, teb + L::teb_stack_base, 8 ) == emu::win::guest_stack_top && w4_read( p, teb + L::teb_stack_limit, 8 ) == emu::win::guest_stack_top - 0x100000,
			   "TEB StackBase / StackLimit = the stack (1 MiB reserve, fully committed)" );
		CHECK( w4_read( p, teb + L::teb_client_id, 8 ) == emu::win::guest_process_id && w4_read( p, teb + L::teb_client_id + 8, 8 ) == emu::win::guest_thread_id, "ClientId" );
		CHECK( w4_read( p, peb + L::peb_image_base_address, 8 ) == p.image_base() && w4_read( p, peb + L::peb_being_debugged, 1 ) == 0, "PEB ImageBaseAddress, BeingDebugged 0" );
		CHECK( w4_read( p, peb + L::peb_os_major_version, 4 ) == 10 && w4_read( p, peb + L::peb_os_minor_version, 4 ) == 0 && w4_read( p, peb + L::peb_os_build_number, 2 ) == 0x4A64 &&
				   w4_read( p, peb + L::peb_os_platform_id, 4 ) == 2,
			   "PEB OS version fields from the facts" );
		CHECK( w4_read( p, k + L::kuser_major_version, 4 ) == 10 && w4_read( p, k + L::kuser_build_number, 4 ) == 0x4A64 && w4_read( p, k + L::kuser_product_type, 4 ) == 1 &&
				   w4_read( p, k + L::kuser_image_number_low, 2 ) == 0x8664 && w4_read( p, k + L::kuser_native_processor_architecture, 2 ) == 9 &&
				   w4_read( p, k + L::kuser_active_processor_count, 4 ) == 8 && w4_read( p, k + L::kuser_number_of_physical_pages, 4 ) == 0x400000 &&
				   w4_read( p, k + L::kuser_kd_debugger_enabled, 1 ) == 0 && w4_read( p, k + L::kuser_system_call, 4 ) == 0,
			   "KUSER fields from the facts (synthesized, no host page)" );
		CHECK( w4_read( p, k + L::kuser_system_root, 6 ) == 0x005C003A0043ull, "KUSER NtSystemRoot = L\"C:\\...\"" );
		CHECK( w4_read( p, k + L::kuser_interrupt_time, 8 ) == 0 && w4_read( p, k + L::kuser_system_time, 8 ) == 0, "time fields 0 until M6" );
		bool kuser_ro = false;
		for ( auto& r : p.eng().regions() )
			if ( r.base == k ) kuser_ro = r.prot == UC_PROT_READ;
		CHECK( kuser_ro, "KUSER mapped read-only" );
		if ( w.r.have_final )
		{
			uint64_t gs = 0, ds = 0, fs = 0, es = 0;
			emu::reg::get_u64( w.r.final, "gs_base", gs );
			emu::reg::get_u64( w.r.final, "ds", ds );
			emu::reg::get_u64( w.r.final, "fs", fs );
			emu::reg::get_u64( w.r.final, "es", es );
			CHECK( gs == teb && ds == 0x2B && es == 0x2B && fs == 0x53, "GS base = TEB; DS / ES = 2BH, FS = 53H (ksamd64.inc KGDT64_R3_DATA / R3_CMTEB | 3)" );
		}
		CHECK( w.r.user_rflags == 0x202 && w.r.user_rsp == p.user_rsp(), "user RFLAGS (R11) 202H, RSP unchanged at the SYSCALL" );
	}

	// T1's shape: NtTerminateProcess( NtCurrentProcess(), 2AH ) in both modes and at another base: the same observable result
	void t_win_env_terminate()
	{
		const auto f = w4_build( w4_t1( 0x2C ) );
		std::vector<std::string> first;
		for ( auto [ b, rebase ] : { std::pair{ emu::env_boundary::sys, false }, std::pair{ emu::env_boundary::hle, false }, std::pair{ emu::env_boundary::sys, true },
									 std::pair{ emu::env_boundary::hle, true } } )
		{
			auto w = w4_go( f, b, rebase );
			CHECK( w.p && w.ok, "process: " + w.error );
			if ( !w.p ) continue;
			std::cout << "  " << emu::name_of( b ) << ( rebase ? " rebased" : "" ) << ": " << emu::win::name_of( w.r.stop ) << ", exit status " << w.r.exit_status << ", "
					  << w.r.insns << " instructions\n";
			CHECK( w.r.stop == emu::win::stop_kind::terminated && w.r.exit_status == 0x2A, "terminated with 2AH: " + w.r.detail );
			CHECK( w.r.syscall_insns == 1 && w.r.syscalls_handled == 1 && w.r.syscalls.size() == 1 && w.r.syscalls[ 0 ].name == "NtTerminateProcess",
				   "one SYSCALL executed, one handled by the kernel layer" );
			const bool mode_line = std::any_of( w.p->log().begin(), w.p->log().end(), [ & ]( const std::string& l ) { return l.rfind( std::string( "boundary: --" ) + emu::name_of( b ), 0 ) == 0; } );
			CHECK( mode_line, "the mode is recorded in the log" );
			const auto obs = w.p->observable( w.r );
			if ( !rebase && b == emu::env_boundary::sys ) first = obs;
			if ( !rebase && b == emu::env_boundary::hle ) CHECK( obs == first, "--Sys and --HLE: identical observable results" );
			if ( rebase ) CHECK( w.p->image_base() == 0x7FF700000000ull, "mapped at the requested base" );
		}
	}

	// loud stops with a state dump: unknown / unimplemented services, unsupported arguments; faults, return, bound
	void t_win_env_loud_stops()
	{
		struct stop_case { const char* what; w4_pe pe; emu::win::stop_kind want; const char* detail; uint64_t max_insns; };
		std::vector<stop_case> cases = {
			{ "unknown number 1FFH", w4_t1( 0x1FF ), emu::win::stop_kind::unknown_syscall, "unknown", 10000 },
			{ "EAX FFFFFFFFH (bits 15:12 set)", w4_t1( 0xFFFFFFFF ), emu::win::stop_kind::unknown_syscall, "unknown", 10000 },
			// U2014 (M5): NtClose is a service now; NtClose of the pseudo handle NtCurrentProcess() has no documented answer = still a loud stop
			{ "NtClose( NtCurrentProcess() ): no documented answer", w4_t1( 0x0F ), emu::win::stop_kind::unimplemented_syscall, "pseudo handle", 10000 },
			{ "NtTerminateProcess( 1234H )", w4_t1( 0x2C, 0x2A, { 0xB8, 0x2C, 0, 0, 0, 0x41, 0xBA, 0x34, 0x12, 0, 0, 0x0F, 0x05, 0x0F, 0x0B } ), emu::win::stop_kind::unimplemented_syscall,
			  "ProcessHandle 0x1234", 10000 },
			{ "INT 2EH", w4_t1( 0x2C, 0x2A, { 0xCD, 0x2E, 0x0F, 0x0B } ), emu::win::stop_kind::fault, "INT 2EH", 10000 },
			{ "write to .rdata", w4_t1( 0x2C, 0x2A, { 0x48, 0x8D, 0x05, 0xF9, 0x0F, 0x00, 0x00, 0xC6, 0x00, 0x01, 0x0F, 0x0B } ), emu::win::stop_kind::fault, "#PF write", 10000 },
			{ "return from the entry", w4_t1( 0x2C, 0x2A, { 0xC3 } ), emu::win::stop_kind::returned, "returned", 10000 },
			{ "instruction bound", w4_t1( 0x2C, 0x2A, { 0xEB, 0xFE } ), emu::win::stop_kind::count, "bound", 1000 },
			{ "#UD", w4_t1( 0x2C, 0x2A, { 0x0F, 0x0B } ), emu::win::stop_kind::fault, "#UD", 10000 },
		};
		for ( auto& c : cases )
		{
			auto w = w4_go( w4_build( c.pe ), emu::env_boundary::sys, false, c.max_insns );
			CHECK( w.p && w.ok, std::string( c.what ) + ": " + w.error );
			if ( !w.p ) continue;
			const auto dump = w.p->state_dump( w.r );
			std::cout << "  " << c.what << ": " << emu::win::name_of( w.r.stop ) << " - " << w.r.detail << " (" << dump.size() << " dump lines)\n";
			CHECK( w.r.stop == c.want && w.r.detail.find( c.detail ) != std::string::npos, std::string( c.what ) + ": " + w.r.detail );
			CHECK( dump.size() > 90 && dump[ 0 ].find( "STATE DUMP" ) == 0, std::string( c.what ) + ": the state dump (registers + memory map)" );
			if ( c.want == emu::win::stop_kind::unknown_syscall || c.want == emu::win::stop_kind::unimplemented_syscall )
				CHECK( has_line( dump, "user mode at the SYSCALL: rip" ) && w.r.syscalls_handled == 1 && w.r.exit_status == 0, std::string( c.what ) + ": the system call in the dump, no exit status" );
		}
	}

	// refused before anything runs (milestone M5 items, a DLL, no environment, collisions, the stack bound)
	void t_win_env_refusals()
	{
		auto imports = w4_t1( 0x2C );
		imports.dirs[ 1 ] = { 0x2000, 0x14 };
		auto tls = w4_t1( 0x2C );
		tls.dirs[ 9 ] = { 0x2000, 0x28 };
		auto dll = w4_t1( 0x2C );
		dll.file_characteristics |= IMAGE_FILE_DLL;
		auto clash = w4_t1( 0x2C );
		clash.image_base = emu::win::guest_stack_top - 0x10000;   // inside the stack area
		auto high = w4_t1( 0x2C );
		high.image_base = 0x7FFFFFFF0000ull;                      // above lpMaximumApplicationAddress
		auto big_stack = w4_t1( 0x2C );
		big_stack.stack_reserve = 0x4000000;
		const struct { const char* what; w4_pe pe; const char* expect; } cases[] = {
			// U2014 (M5): imports / TLS are --HLE features now; under --Sys (w4_go's default) refused with the LdrInitializeThunk reason
			{ "imports under --Sys", imports, "LdrInitializeThunk" }, { "TLS under --Sys", tls, "has a TLS directory" }, { "DLL", dll, "DLL" }, { "image in the stack area", clash, "overlaps" },
			{ "image above the user range", high, "user address range" }, { "stack reserve 4000000H", big_stack, "stack area" },
		};
		for ( auto& c : cases )
		{
			auto w = w4_go( w4_build( c.pe ) );
			std::cout << "  " << c.what << ": " << ( w.p ? "accepted" : w.error ) << "\n";
			CHECK( !w.p && w.error.find( c.expect ) != std::string::npos, std::string( c.what ) + " refused with \"" + c.expect + "\": " + w.error );
		}
		emu::engine_config ec;
		ec.profile = emu::cpu_profile_choice::max;
		auto f = w4_facts();
		f.env = emu::win::environment::none;
		std::vector<std::string> log;
		std::string err;
		CHECK( !emu::win::process::create( ec, f, {}, w4_build( w4_t1( 0x2C ) ), log, err ) && err.find( "no Windows environment" ) != std::string::npos, "no environment: " + err );
	}

	// runtime half of no_host_passthrough: the guest's NtTerminateProcess( NtCurrentProcess() ) ends only the EMULATED
	// process - this test process continues; the SYSCALL was seen and handled in the engine; no guest byte sits in host-executable
	// memory and every executable region is gone with the engine
	void t_win_env_no_passthrough_runtime()
	{
		const auto pe = w4_t1( 0x2C, 0x2A );
		{
			auto w = w4_go( w4_build( pe ) );
			CHECK( w.p && w.ok && w.r.stop == emu::win::stop_kind::terminated, "terminated: " + w.r.detail );
			std::cout << "  guest exit status " << w.r.exit_status << "; this process continues (the host process was not terminated)\n";
			CHECK( w.r.syscall_insns == w.r.syscalls_handled && w.r.syscalls_handled == 1, "every SYSCALL the guest executed was handled by the emulated kernel" );
			const auto& code = pe.sections[ 0 ].data;
			const auto in_exec = emu::host_guard::find_pattern( code.data(), code.size(), true );
			std::cout << "  guest .text bytes in host-executable memory: " << in_exec.size() << "\n";
			CHECK( in_exec.empty(), "the guest code is in no host-executable region" );
		}
		const auto after = emu::host_guard::foreign_executable();
		CHECK( after.empty(), "no executable region outside images after the engine is closed" );
	}

	// ------------------------------------------------------------------------------------------------ M5 syscall layer / --HLE (U2014)
	// Plan 3.M5: the services of emu::nt (answered as documented, or stopped loudly), the --HLE import redirect and TLS start, a
	// SYSCALL from any address (plan 1.K.6), EAX AND FFFFH (plan 6.4.a), and the syscall-argument fuzz (AGENT_RULES "Host
	// safety": "Fuzz the layer"). Images are built in memory (helper prefix w5_), their code assembled by Keystone at its image
	// address; the syscall numbers and the OS / platform facts are synthetic (the capture-based runs are suite win_env_m5).
	constexpr uint32_t w5_n_close = 0x0F, w5_n_alloc = 0x18, w5_n_qip = 0x19, w5_n_free = 0x1E, w5_n_qvm = 0x23, w5_n_term = 0x2C, w5_n_other = 0x55;
	constexpr uint64_t w5_base = 0x140000000ull, w5_data = w5_base + 0x3000, w5_scratch = 0xFF0000ull;   // w5_scratch: inside the M4 stack area

	emu::win::environment_facts w5_facts()
	{
		auto f = w4_facts();
		f.syscalls = {};
		for ( auto [ n, name ] : { std::pair<uint32_t, const char*>{ w5_n_term, "NtTerminateProcess" }, { w5_n_close, "NtClose" }, { w5_n_alloc, "NtAllocateVirtualMemory" },
								   { w5_n_qip, "NtQueryInformationProcess" }, { w5_n_free, "NtFreeVirtualMemory" }, { w5_n_qvm, "NtQueryVirtualMemory" },
								   { w5_n_other, "NtCreateFile" } } )
		{
			f.syscalls.by_number[ n ] = name;
			f.syscalls.by_name[ name ] = n;
		}
		f.syscalls.nt_exports = f.syscalls.stubs = 7;
		f.page_size = 0x1000;
		f.allocation_granularity = 0x10000;
		f.active_processor_mask = 0xFF;
		return f;
	}

	struct w5_import
	{
		std::string dll;
		std::vector<std::string> names;
	};
	struct w5_spec
	{
		std::string entry_asm;          // assembled at base + 1000H
		std::string callback_asm;       // assembled at base + 1800H ("" = none)
		std::vector<w5_import> imports;
		bool tls = false;
		bool tls_callback = true;
	};

	// the IAT slot of each imported name: .rdata 2200H on, 8 bytes each, one zero slot after each module
	std::map<std::string, uint64_t> w5_iat_map( const w5_spec& s )
	{
		std::map<std::string, uint64_t> m;
		uint64_t at = w5_base + 0x2200;
		for ( auto& mod : s.imports )
		{
			for ( auto& n : mod.names )
			{
				m[ n ] = at;
				at += 8;
			}
			at += 8;
		}
		return m;
	}

	// "IAT(Name)" in the assembly text -> the slot's address
	std::string w5_fill( std::string t, const std::map<std::string, uint64_t>& iat )
	{
		for ( auto& [ n, va ] : iat )
		{
			const std::string key = "IAT(" + n + ")";
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)va );
			for ( size_t p; ( p = t.find( key ) ) != std::string::npos; ) t.replace( p, key.size(), b );
		}
		return t;
	}

	// .text 1000H (entry; a TLS callback at 1800H), .rdata 2000H (import descriptors 2000H, ILT 2100H, IAT 2200H, hint / names
	// 2400H, DLL names 2700H, IMAGE_TLS_DIRECTORY64 2800H, callback array 2840H), .data 3000H (variables; TLS index 3080H, the TLS
	// template [3100H, 3110H) + 10H zero bytes)
	std::vector<uint8_t> w5_build( const w5_spec& s, std::string& err )
	{
		w4_pe p;
		p.image_base = w5_base;
		p.entry_rva = 0x1000;
		const auto iat = w5_iat_map( s );
		std::vector<uint8_t> text, cb;
		if ( !emu::assemble( w5_fill( s.entry_asm, iat ), w5_base + 0x1000, text, err ) ) return {};
		if ( text.size() > 0x800 )
		{
			err = "entry code longer than 800H";
			return {};
		}
		if ( !s.callback_asm.empty() )
		{
			if ( !emu::assemble( s.callback_asm, w5_base + 0x1800, cb, err ) ) return {};
			text.resize( 0x800, 0xCC );
			text.insert( text.end(), cb.begin(), cb.end() );
		}
		std::vector<uint8_t> rdata( 0x900, 0 );
		auto p32 = [ & ]( size_t at, uint32_t v ) { std::memcpy( rdata.data() + at, &v, 4 ); };
		auto p64 = [ & ]( size_t at, uint64_t v ) { std::memcpy( rdata.data() + at, &v, 8 ); };
		size_t ilt = 0x100, iatp = 0x200, hn = 0x400, dn = 0x700;
		for ( size_t i = 0; i < s.imports.size(); i++ )
		{
			const auto& mod = s.imports[ i ];
			p32( 20 * i + 0, uint32_t( 0x2000 + ilt ) );    // OriginalFirstThunk
			p32( 20 * i + 12, uint32_t( 0x2000 + dn ) );    // Name
			p32( 20 * i + 16, uint32_t( 0x2000 + iatp ) );  // FirstThunk
			std::memcpy( rdata.data() + dn, mod.dll.c_str(), mod.dll.size() + 1 );
			dn += mod.dll.size() + 1;
			for ( auto& n : mod.names )
			{
				p64( ilt, 0x2000 + hn );
				p64( iatp, 0x2000 + hn );
				ilt += 8;
				iatp += 8;
				hn += 2;   // Hint 0
				std::memcpy( rdata.data() + hn, n.c_str(), n.size() + 1 );
				hn = ( hn + n.size() + 2 ) & ~size_t( 1 );
			}
			ilt += 8;
			iatp += 8;
		}
		if ( !s.imports.empty() ) p.dirs[ 1 ] = { 0x2000, uint32_t( 20 * ( s.imports.size() + 1 ) ) };
		std::vector<uint8_t> data( 0x200, 0 );
		if ( s.tls )
		{
			p64( 0x800, w5_base + 0x3100 );
			p64( 0x808, w5_base + 0x3110 );
			p64( 0x810, w5_base + 0x3080 );
			p64( 0x818, s.tls_callback ? w5_base + 0x2840 : 0 );
			p32( 0x820, 0x10 );
			p64( 0x840, w5_base + 0x1800 );
			p.dirs[ 9 ] = { 0x2800, 0x28 };
			const uint64_t t0 = 0x1122334455667788ull, t1 = 0x99AABBCCDDEEFF00ull;
			const uint32_t not_yet = 0xFFFFFFFF;   // the index variable before the loader writes 0
			std::memcpy( data.data() + 0x100, &t0, 8 );
			std::memcpy( data.data() + 0x108, &t1, 8 );
			std::memcpy( data.data() + 0x80, &not_yet, 4 );
		}
		p.sections = { { ".text", 0x1000, text, w4_scn_text }, { ".rdata", 0x2000, rdata, w4_scn_rdata }, { ".data", 0x3000, data, w4_scn_data } };
		return w4_build( p );
	}

	std::unique_ptr<emu::win::process> w5_create( const std::vector<uint8_t>& file, emu::env_boundary b, std::string& err, uint64_t max_insns = 200000 )
	{
		emu::engine_config ec;
		ec.profile = emu::cpu_profile_choice::guest;
		emu::win::process_options po;
		po.boundary = b;
		po.max_insns = max_insns;
		po.image_name = "w5";
		std::vector<std::string> log;
		return emu::win::process::create( ec, w5_facts(), po, file, log, err );
	}

	struct w5_run
	{
		std::unique_ptr<emu::win::process> p;
		emu::win::run_result r;
		std::string error;
		bool ok = false;
	};
	w5_run w5_go( const std::vector<uint8_t>& file, emu::env_boundary b )
	{
		w5_run w;
		w.p = w5_create( file, b, w.error );
		if ( w.p ) w.ok = w.p->run( w.r, w.error );
		return w;
	}

	emu::nt::reply w5_call( emu::win::process& p, const char* name, std::initializer_list<uint64_t> a, bool stack_ok = true )
	{
		emu::nt::call c;
		c.name = name;
		size_t i = 0;
		for ( auto v : a ) c.args[ i++ ] = v;
		c.stack_args = stack_ok;
		const auto it = p.syscalls().by_name.find( name );
		if ( it != p.syscalls().by_name.end() ) c.number = it->second;
		return p.kern().dispatch( c );
	}
	void w5_put( emu::win::process& p, uint64_t va, uint64_t v )
	{
		std::string err;
		CHECK( p.eng().write( va, &v, 8, err ), "guest write " + err );
	}
	bool w5_ok( const emu::nt::reply& r ) { return r.kind == emu::nt::reply_kind::done && r.status == emu::nt::status::success; }
	bool w5_status( const emu::nt::reply& r, uint32_t st ) { return r.kind == emu::nt::reply_kind::done && r.status == st; }
	bool w5_stop( const emu::nt::reply& r ) { return r.kind == emu::nt::reply_kind::stop; }

	struct w5_mbi { uint64_t base, alloc_base; uint32_t alloc_protect; uint16_t partition, pad; uint64_t region; uint32_t state, protect, type, pad2; };
	w5_mbi w5_query( emu::win::process& p, uint64_t addr, emu::nt::reply& r )
	{
		w5_mbi m{};
		r = w5_call( p, "NtQueryVirtualMemory", { emu::nt::current_process, addr, 0, w5_scratch + 0x100, 0x30, w5_scratch + 0x180 } );
		std::string err;
		if ( w5_ok( r ) ) p.eng().read( w5_scratch + 0x100, &m, sizeof( m ), err );
		return m;
	}

	const char* w5_trivial = "mov eax, 0x2C; mov r10, -1; mov edx, 0x2A; syscall; ud2";

	// NtAllocateVirtualMemory / NtFreeVirtualMemory / NtQueryVirtualMemory: reserve, commit, decommit, release, query, the documented
	// errors and the loud stops; the engine's region table stays equal to Unicorn's
	void t_nt_kernel_vm()
	{
		std::string err;
		auto p = w5_create( w5_build( { w5_trivial }, err ), emu::env_boundary::sys, err );
		CHECK( p != nullptr, "process: " + err );
		if ( !p ) return;
		namespace N = emu::nt;
		const uint64_t S = w5_scratch, cp = N::current_process;
		// 1. reserve + commit 3000H read-write at a system-chosen address
		w5_put( *p, S, 0 );
		w5_put( *p, S + 8, 0x3000 );
		auto r = w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit | N::mem::reserve, N::page::readwrite } );
		const uint64_t base = w4_read( *p, S, 8 ), size = w4_read( *p, S + 8, 8 );
		std::cout << "  allocate: " << r.text << "; base " << std::hex << base << " size " << size << std::dec << "\n";
		CHECK( w5_ok( r ) && base && !( base & 0xFFFF ) && base >= 0x10000 && size == 0x3000 && r.writes.size() == 2, "reserve+commit at a 64 KiB boundary, size 3000H" );
		CHECK( p->eng().accessible( base, 0x3000, UC_PROT_READ | UC_PROT_WRITE ) && !p->eng().accessible( base, 0x3000, UC_PROT_EXEC ), "committed read-write, not executable" );
		CHECK( w4_read( *p, base + 0x2FF8, 8 ) == 0, "a committed page reads zero" );
		// 2. query the middle page: one run of 2 pages committed read-write
		auto m = w5_query( *p, base + 0x1234, r );
		CHECK( w5_ok( r ) && m.base == base + 0x1000 && m.alloc_base == base && m.alloc_protect == N::page::readwrite && m.region == 0x2000 && m.state == N::mem::commit &&
				   m.protect == N::page::readwrite && m.type == N::mem::private_ && w4_read( *p, S + 0x180, 8 ) == 0x30,
			   "MemoryBasicInformation of the middle page: " + r.text );
		// 3. decommit the middle page, then three runs
		w5_put( *p, S, base + 0x1000 );
		w5_put( *p, S + 8, 0x1000 );
		r = w5_call( *p, "NtFreeVirtualMemory", { cp, S, S + 8, N::mem::decommit } );
		CHECK( w5_ok( r ) && !p->eng().accessible( base + 0x1000, 1, UC_PROT_READ ) && p->eng().accessible( base + 0x2000, 1, UC_PROT_READ ), "decommit: " + r.text );
		m = w5_query( *p, base, r );
		CHECK( w5_ok( r ) && m.region == 0x1000 && m.state == N::mem::commit, "run 1: the first page" );
		m = w5_query( *p, base + 0x1000, r );
		CHECK( w5_ok( r ) && m.region == 0x1000 && m.state == N::mem::reserve && m.protect == 0, "run 2: reserved, Protect 0" );
		// 4. commit it again as execute-read at its address (MEM_COMMIT alone inside the reservation)
		w5_put( *p, S, base + 0x1000 );
		w5_put( *p, S + 8, 0x10 );
		const auto rc = w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit, N::page::execute_read } );
		CHECK( w5_ok( rc ) && w4_read( *p, S, 8 ) == base + 0x1000 && w4_read( *p, S + 8, 8 ) == 0x1000, "MEM_COMMIT inside the reservation, rounded to the page: " + rc.text );
		m = w5_query( *p, base + 0x1000, r );
		CHECK( w5_ok( r ) && m.protect == N::page::execute_read && m.state == N::mem::commit, "re-committed execute-read" );
		CHECK( p->eng().accessible( base + 0x1000, 0x1000, UC_PROT_READ | UC_PROT_EXEC ), "execute-read page: readable + executable" );
		// 5. the loud stops of undocumented combinations
		w5_put( *p, S, base );
		w5_put( *p, S + 8, 0x1000 );
		CHECK( w5_stop( w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit, N::page::readonly } ) ), "re-commit with another protection: stop" );
		w5_put( *p, S, 0 );
		w5_put( *p, S + 8, 0 );
		CHECK( w5_stop( w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit | N::mem::reserve, N::page::readwrite } ) ), "RegionSize 0: stop" );
		w5_put( *p, S + 8, 0x1000 );
		CHECK( w5_stop( w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit | N::mem::reserve, N::page::readwrite | N::page::guard } ) ), "PAGE_GUARD: stop (M7)" );
		CHECK( w5_stop( w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit | N::mem::reserve, N::page::writecopy } ) ), "PAGE_WRITECOPY: stop" );
		CHECK( w5_stop( w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, 0x80000, N::page::readwrite } ) ), "MEM_RESET: stop" );
		CHECK( w5_stop( w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 1, S + 8, N::mem::commit | N::mem::reserve, N::page::readwrite } ) ), "ZeroBits: stop" );
		CHECK( w5_stop( w5_call( *p, "NtAllocateVirtualMemory", { 0x1234, S, 0, S + 8, N::mem::commit | N::mem::reserve, N::page::readwrite } ) ), "another process handle: stop" );
		CHECK( w5_stop( w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit | N::mem::reserve, N::page::readwrite }, false ) ), "stack arguments unreadable: stop" );
		// 6. documented status values
		CHECK( w5_status( w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit | N::mem::reserve, 3 } ), N::status::invalid_page_protection ), "Protect 3: STATUS_INVALID_PAGE_PROTECTION" );
		CHECK( w5_status( w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit | N::mem::reserve, 0 } ), N::status::invalid_page_protection ), "Protect 0: STATUS_INVALID_PAGE_PROTECTION" );
		w5_put( *p, S, w5_base + 0x1000 );
		w5_put( *p, S + 8, 0x1000 );
		r = w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::reserve, N::page::readwrite } );
		CHECK( w5_status( r, N::status::conflicting_addresses ) && w4_read( *p, S, 8 ) == w5_base + 0x1000, "reserve over the image: STATUS_CONFLICTING_ADDRESSES, nothing written: " + r.text );
		for ( uint64_t bad : { 0x10ull, uint64_t( emu::win::guest_kuser_va ), emu::win::guest_kernel_gdt_va, w5_base + 0x2000, 0xFFFFFFFFFFFFFFF8ull } )
		{
			r = w5_call( *p, "NtAllocateVirtualMemory", { cp, bad, 0, S + 8, N::mem::commit | N::mem::reserve, N::page::readwrite } );
			CHECK( w5_status( r, N::status::access_violation ) && r.writes.empty(), "BaseAddress pointer " + std::to_string( bad ) + ": STATUS_ACCESS_VIOLATION, nothing written" );
		}
		// 7. reserve only at a given free address, then release; MEM_TOP_DOWN
		w5_put( *p, S, 0x50001234 );
		w5_put( *p, S + 8, 0x10000 );
		r = w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::reserve, N::page::readwrite } );
		CHECK( w5_ok( r ) && w4_read( *p, S, 8 ) == 0x50000000 && w4_read( *p, S + 8, 8 ) == 0x12000, "reserve at 50001234H: base rounded down to 64 KiB, size to the page end: " + r.text );
		m = w5_query( *p, 0x50000000, r );
		CHECK( w5_ok( r ) && m.state == N::mem::reserve && m.region == 0x12000 && !p->eng().accessible( 0x50000000, 1, UC_PROT_READ ), "reserved, no access" );
		w5_put( *p, S, 0x50000000 );
		w5_put( *p, S + 8, 0 );
		r = w5_call( *p, "NtFreeVirtualMemory", { cp, S, S + 8, N::mem::release } );
		CHECK( w5_ok( r ) && w4_read( *p, S + 8, 8 ) == 0x12000, "release: the size released returned" );
		m = w5_query( *p, 0x50000000, r );
		CHECK( w5_ok( r ) && m.state == N::mem::free_ && m.protect == N::page::noaccess && m.alloc_base == 0, "after release: MEM_FREE" );
		w5_put( *p, S, 0 );
		w5_put( *p, S + 8, 0x1000 );
		r = w5_call( *p, "NtAllocateVirtualMemory", { cp, S, 0, S + 8, N::mem::commit | N::mem::reserve | N::mem::top_down, N::page::readwrite } );
		const uint64_t top = w4_read( *p, S, 8 );
		CHECK( w5_ok( r ) && top > 0x7FF000000000ull && top + 0x10000 - 1 <= 0x7FFFFFFEFFFFull, "MEM_TOP_DOWN: the highest free 64 KiB in the user range" );
		// 8. release errors and queries of environment / out-of-range addresses
		w5_put( *p, S, base );
		w5_put( *p, S + 8, 0x1000 );
		CHECK( w5_stop( w5_call( *p, "NtFreeVirtualMemory", { cp, S, S + 8, N::mem::release } ) ), "MEM_RELEASE with a size: stop" );
		w5_put( *p, S, base + 0x1000 );
		w5_put( *p, S + 8, 0 );
		CHECK( w5_stop( w5_call( *p, "NtFreeVirtualMemory", { cp, S, S + 8, N::mem::release } ) ), "MEM_RELEASE not at the base: stop" );
		CHECK( w5_stop( w5_call( *p, "NtFreeVirtualMemory", { cp, S, S + 8, 0x1 } ) ), "FreeType 1: stop" );
		w5_query( *p, w5_base + 0x1000, r );
		CHECK( w5_stop( r ) && r.text.find( "environment region" ) != std::string::npos, "query of the image: stop (no cited source for Windows' answer)" );
		w5_query( *p, 0x7FFFFFFF0000ull, r );
		CHECK( w5_stop( r ), "query above the user range: stop" );
		m = w5_query( *p, 0x20000000, r );
		CHECK( w5_ok( r ) && m.state == N::mem::free_ && m.base == 0x20000000 && m.region > 0x10000000, "free range: MEM_FREE up to the next region" );
		CHECK( w5_stop( w5_call( *p, "NtQueryVirtualMemory", { cp, base, 2, S + 0x100, 0x30, 0 } ) ), "MemoryInformationClass 2: stop" );
		CHECK( w5_stop( w5_call( *p, "NtQueryVirtualMemory", { cp, base, 0, S + 0x100, 0x20, 0 } ) ), "short buffer: stop" );
		CHECK( w5_status( w5_call( *p, "NtQueryVirtualMemory", { cp, base, 0, w5_base + 0x2000, 0x30, 0 } ), N::status::access_violation ), "read-only buffer: STATUS_ACCESS_VIOLATION" );
		CHECK( w5_ok( w5_call( *p, "NtQueryVirtualMemory", { cp, base, 0, S + 0x100, 0x30, 0 } ) ), "ReturnLength NULL is optional" );
		w5_put( *p, S, base );
		w5_put( *p, S + 8, 0 );
		r = w5_call( *p, "NtFreeVirtualMemory", { cp, S, S + 8, N::mem::release } );
		CHECK( w5_ok( r ) && !p->eng().accessible( base, 1, UC_PROT_READ ), "release of the first allocation" );
		for ( auto& l : p->kern().describe() ) std::cout << "  " << l << "\n";
		CHECK( p->kern().allocations().size() == 1, "one allocation left (the MEM_TOP_DOWN one)" );
		CHECK( p->eng().check_regions( err ), "engine region table = Unicorn's: " + err );
	}

	// NtQueryInformationProcess: the documented classes of winternl.h; the rest stops
	void t_nt_kernel_process_info()
	{
		std::string err;
		auto p = w5_create( w5_build( { w5_trivial }, err ), emu::env_boundary::sys, err );
		CHECK( p != nullptr, "process: " + err );
		if ( !p ) return;
		namespace N = emu::nt;
		const uint64_t S = w5_scratch, cp = N::current_process;
		for ( uint64_t i = 0; i < 8; i++ ) w5_put( *p, S + 8 * i, 0xEEEEEEEEEEEEEEEEull );
		auto r = w5_call( *p, "NtQueryInformationProcess", { cp, 0, S, 0x30, S + 0x40 } );
		std::cout << "  class 0: " << r.text << "\n";
		CHECK( w5_ok( r ) && w4_read( *p, S + 8, 8 ) == p->peb() && w4_read( *p, S + 32, 8 ) == emu::win::guest_process_id && w4_read( *p, S, 8 ) == 0 &&
				   w4_read( *p, S + 40, 8 ) == 0 && w4_read( *p, S + 0x40, 4 ) == 0x30,
			   "ProcessBasicInformation: PebBaseAddress, UniqueProcessId, the reserved members 0, ReturnLength 30H" );
		for ( auto [ cls, len ] : { std::pair<uint64_t, uint64_t>{ 7, 8 }, { 26, 8 }, { 29, 4 } } )
		{
			w5_put( *p, S, 0xEEEEEEEEEEEEEEEEull );
			r = w5_call( *p, "NtQueryInformationProcess", { cp, cls, S, len, 0 } );
			CHECK( w5_ok( r ) && ( w4_read( *p, S, 8 ) & ( len == 8 ? ~0ull : 0xFFFFFFFFull ) ) == 0, "class " + std::to_string( cls ) + ": 0 (" + r.text + ")" );
		}
		CHECK( w5_stop( w5_call( *p, "NtQueryInformationProcess", { cp, 0, S, 0x20, 0 } ) ), "class 0 with length 20H: stop (status not documented)" );
		for ( uint64_t cls : { 1ull, 27ull, 30ull, 31ull, 0x100000000ull + 5 } )
			CHECK( w5_stop( w5_call( *p, "NtQueryInformationProcess", { cp, cls, S, 8, 0 } ) ), "class " + std::to_string( cls & 0xFFFFFFFF ) + ": stop (not documented in winternl.h)" );
		CHECK( w5_status( w5_call( *p, "NtQueryInformationProcess", { cp, 7, 0x10, 8, 0 } ), N::status::access_violation ), "unmapped buffer: STATUS_ACCESS_VIOLATION" );
		CHECK( w5_status( w5_call( *p, "NtQueryInformationProcess", { cp, 7, S, 8, emu::win::guest_kuser_va } ), N::status::access_violation ), "read-only ReturnLength: STATUS_ACCESS_VIOLATION" );
		CHECK( w5_stop( w5_call( *p, "NtQueryInformationProcess", { 4, 7, S, 8, 0 } ) ), "another process handle: stop" );
		CHECK( w5_stop( w5_call( *p, "NtQueryInformationProcess", { cp, 7, S, 8, 0 }, false ) ), "stack argument unreadable: stop" );
	}

	// NtClose: a handle no service created = STATUS_INVALID_HANDLE (ZwClose), a pseudo handle stops; NtTerminateProcess; an
	// unimplemented / unknown service
	void t_nt_kernel_close_terminate()
	{
		std::string err;
		auto p = w5_create( w5_build( { w5_trivial }, err ), emu::env_boundary::sys, err );
		CHECK( p != nullptr, "process: " + err );
		if ( !p ) return;
		namespace N = emu::nt;
		for ( uint64_t h : { 0ull, 4ull, 0x1234ull, 0x7FFFFFFFFFFFFFFFull } )
			CHECK( w5_status( w5_call( *p, "NtClose", { h } ), N::status::invalid_handle ), "NtClose( " + std::to_string( h ) + " ): STATUS_INVALID_HANDLE" );
		for ( uint64_t h : { N::current_process, N::current_thread, ~0ull - 5 } ) CHECK( w5_stop( w5_call( *p, "NtClose", { h } ) ), "NtClose of a pseudo handle: stop" );
		auto r = w5_call( *p, "NtTerminateProcess", { N::current_process, 0x1234567800000077ull } );
		CHECK( r.kind == N::reply_kind::terminated && r.exit_status == 0x77, "NtTerminateProcess( NtCurrentProcess() ): terminated, ExitStatus = the low 32 bits" );
		CHECK( w5_stop( w5_call( *p, "NtTerminateProcess", { 0, 1 } ) ) && w5_stop( w5_call( *p, "NtTerminateProcess", { 0x40, 1 } ) ), "NtTerminateProcess of NULL / a handle: stop" );
		r = w5_call( *p, "NtCreateFile", {} );
		CHECK( w5_stop( r ) && r.text.find( "not implemented in milestone M5" ) != std::string::npos, "a known, unimplemented service: stop" );
		emu::nt::call u;
		u.number = 0x7AB;
		CHECK( w5_stop( p->kern().dispatch( u ) ), "an unknown service (no name): stop" );
	}

	// the T4 shape: direct SYSCALLs from the image (NtQueryInformationProcess 0 / 7 - the second with EAX bits 31:16 set),
	// NtAllocateVirtualMemory RWX, a stub WRITTEN by the guest into that memory and CALLED (SYSCALL from allocated memory: plan
	// 1.K.6), NtFreeVirtualMemory, NtTerminateProcess - in both modes, identical observable results
	std::string w5_t4_asm()
	{
		char b[ 4096 ];
		const uint64_t D = w5_data;
		std::snprintf( b, sizeof( b ),
					   "sub rsp, 0x88\n"
					   "mov r10, -1; xor edx, edx; mov r8, 0x%llx; mov r9d, 0x30; mov rax, 0x%llx; mov [rsp+0x28], rax; mov eax, 0x%x; syscall\n"
					   "test eax, eax; jne bad1\n"
					   "mov rax, 0x%llx; mov rcx, gs:[0x60]; cmp [rax+8], rcx; jne bad2; mov rcx, gs:[0x40]; cmp [rax+0x20], rcx; jne bad2\n"
					   "mov r10, -1; mov edx, 7; mov r8, 0x%llx; mov r9d, 8; mov qword ptr [rsp+0x28], 0; mov eax, 0xABCD0000; or eax, 0x%x; syscall\n"
					   "test eax, eax; jne bad3; mov rax, 0x%llx; cmp qword ptr [rax], 0; jne bad3\n"
					   "mov rax, 0x%llx; mov qword ptr [rax], 0; mov qword ptr [rax+8], 0x1000\n"
					   "mov r10, -1; mov rdx, 0x%llx; xor r8d, r8d; mov r9, 0x%llx; mov qword ptr [rsp+0x28], 0x3000; mov qword ptr [rsp+0x30], 0x40; mov eax, 0x%x; syscall\n"
					   "test eax, eax; jne bad4\n"
					   "mov rax, 0x%llx; mov rbx, [rax]; mov dword ptr [rbx], 0xB8D18B4C; mov dword ptr [rbx+4], 0x%x; mov word ptr [rbx+8], 0x050F; mov byte ptr [rbx+10], 0xC3\n"
					   "mov rcx, -1; mov rdx, rbx; xor r8d, r8d; mov r9, 0x%llx; mov qword ptr [rsp+0x20], 0x30; mov rax, 0x%llx; mov [rsp+0x28], rax; call rbx\n"
					   "test eax, eax; jne bad5\n"
					   "mov rax, 0x%llx; cmp [rax], rbx; jne bad6; cmp [rax+8], rbx; jne bad6; cmp dword ptr [rax+0x10], 0x40; jne bad6; cmp qword ptr [rax+0x18], 0x1000; jne bad6\n"
					   "cmp dword ptr [rax+0x20], 0x1000; jne bad6; cmp dword ptr [rax+0x24], 0x40; jne bad6; cmp dword ptr [rax+0x28], 0x20000; jne bad6\n"
					   "mov rax, 0x%llx; mov qword ptr [rax+8], 0\n"
					   "mov r10, -1; mov rdx, 0x%llx; mov r8, 0x%llx; mov r9d, 0x8000; mov eax, 0x%x; syscall\n"
					   "test eax, eax; jne bad7\n"
					   "mov edx, 0x2D; jmp done\n"
					   "bad1: mov edx, 0x101; jmp done\n"
					   "bad2: mov edx, 0x102; jmp done\n"
					   "bad3: mov edx, 0x103; jmp done\n"
					   "bad4: mov edx, 0x104; jmp done\n"
					   "bad5: mov edx, 0x105; jmp done\n"
					   "bad6: mov edx, 0x106; jmp done\n"
					   "bad7: mov edx, 0x107\n"
					   "done: mov r10, -1; mov eax, 0x%x; syscall; ud2\n",
					   (unsigned long long)( D + 0x100 ), (unsigned long long)( D + 0x180 ), w5_n_qip, (unsigned long long)( D + 0x100 ), (unsigned long long)( D + 0x140 ), w5_n_qip,
					   (unsigned long long)( D + 0x140 ), (unsigned long long)( D + 0x40 ), (unsigned long long)( D + 0x40 ), (unsigned long long)( D + 0x48 ), w5_n_alloc,
					   (unsigned long long)( D + 0x40 ), w5_n_qvm, (unsigned long long)( D + 0x1C0 ) - 0x100, (unsigned long long)( D + 0x1F8 ) - 0x100,
					   (unsigned long long)( D + 0x1C0 ) - 0x100, (unsigned long long)( D + 0x40 ), (unsigned long long)( D + 0x40 ), (unsigned long long)( D + 0x48 ), w5_n_free,
					   w5_n_term );
		return b;
	}

	void t_win_env_syscall_layer_run()
	{
		std::string err;
		const auto f = w5_build( { w5_t4_asm() }, err );
		CHECK( !f.empty(), "T4-shaped image: " + err );
		if ( f.empty() ) return;
		std::vector<std::string> first;
		for ( auto b : { emu::env_boundary::sys, emu::env_boundary::hle } )
		{
			auto w = w5_go( f, b );
			CHECK( w.p && w.ok, "process: " + w.error );
			if ( !w.p ) continue;
			std::cout << "  " << emu::name_of( b ) << ": " << emu::win::name_of( w.r.stop ) << " - " << w.r.detail << "; exit " << std::hex << w.r.exit_status << std::dec << ", "
					  << w.r.syscalls.size() << " system calls\n";
			for ( auto& s : w.r.syscalls ) std::cout << "    " << s.name << " at " << std::hex << s.at << " rax " << s.rax << std::dec << ": " << s.outcome << "\n";
			CHECK( w.r.stop == emu::win::stop_kind::terminated && w.r.exit_status == 0x2D, "exit status 2DH (every check of the guest passed)" );
			CHECK( w.r.syscalls.size() == 6 && w.r.syscall_insns == 6 && w.r.syscalls_handled == 6, "6 SYSCALLs, all handled by the kernel layer" );
			if ( w.r.syscalls.size() == 6 )
			{
				CHECK( w.r.syscalls[ 1 ].rax == 0xABCD0000ull + w5_n_qip && w.r.syscalls[ 1 ].number == w5_n_qip && w.r.syscalls[ 1 ].answered, "EAX bits 31:16 set: dispatched on EAX AND FFFFH (plan 6.4.a)" );
				const uint64_t at = w.r.syscalls[ 3 ].at;
				CHECK( w.r.syscalls[ 3 ].name == "NtQueryVirtualMemory" && !( at >= w5_base && at < w5_base + 0x4000 ) && at >= 0x10000 && at < 0x1000000,
					   "the SYSCALL of the guest-written stub came from allocated memory, not from the image (plan 1.K.6)" );
			}
			const auto obs = w.p->observable( w.r );
			if ( b == emu::env_boundary::sys ) first = obs;
			else CHECK( obs == first, "--Sys and --HLE: identical observable results (" + std::to_string( obs.size() ) + " lines)" );
		}
	}

	// the T2 shape under --HLE: kernel32.dll GetCurrentProcessId / GetModuleHandleW( NULL ) / ExitProcess through the stubs; an
	// unimplemented import stops when CALLED; --Sys refuses (LdrInitializeThunk)
	void t_win_env_hle_imports()
	{
		w5_spec s;
		s.imports = { { "KERNEL32.dll", { "GetCurrentProcessId", "GetModuleHandleW", "ExitProcess", "Sleep" } } };
		s.entry_asm = "sub rsp, 0x28\n"
					  "mov rax, IAT(GetCurrentProcessId); call qword ptr [rax]; mov rcx, gs:[0x40]; cmp eax, ecx; jne bad1\n"
					  "xor ecx, ecx; mov rax, IAT(GetModuleHandleW); call qword ptr [rax]; mov rcx, gs:[0x60]; cmp rax, [rcx+0x10]; jne bad2\n"
					  "mov ecx, 0x2B; jmp done\n"
					  "bad1: mov ecx, 0x101; jmp done\n"
					  "bad2: mov ecx, 0x102\n"
					  "done: mov rax, IAT(ExitProcess); call qword ptr [rax]; ud2\n";
		std::string err;
		const auto f = w5_build( s, err );
		CHECK( !f.empty(), "T2-shaped image: " + err );
		if ( f.empty() ) return;
		auto w = w5_go( f, emu::env_boundary::hle );
		CHECK( w.p && w.ok, "process: " + w.error );
		if ( w.p )
		{
			for ( auto& a : w.r.api_calls ) std::cout << "  api " << a.api << " -> " << a.outcome << "\n";
			CHECK( w.r.stop == emu::win::stop_kind::terminated && w.r.exit_status == 0x2B, "--HLE: exit status 2BH: " + w.r.detail );
			CHECK( w.r.api_calls.size() == 3 && w.r.api_calls[ 0 ].api == "KERNEL32.dll!GetCurrentProcessId" && w.r.api_calls[ 2 ].api == "KERNEL32.dll!ExitProcess" &&
					   w.r.syscalls.empty(),
				   "three API calls through the stubs, no SYSCALL" );
			CHECK( w4_read( *w.p, w5_base + 0x2200, 8 ) == emu::hle::module_va && w4_read( *w.p, w5_base + 0x2218, 8 ) == emu::hle::module_va + 3 * emu::hle::stub_size,
				   "IAT slots bound to the stubs (also the unimplemented one)" );
		}
		auto sys = w5_go( f, emu::env_boundary::sys );
		std::cout << "  --Sys: " << ( sys.p ? "accepted" : sys.error.substr( 0, 160 ) ) << "\n";
		CHECK( !sys.p && sys.error.find( "LdrInitializeThunk" ) != std::string::npos && sys.error.find( "KERNEL32.dll" ) != std::string::npos, "--Sys refuses the import (no cited source)" );
		// calling the unimplemented import: a loud stop naming it
		w5_spec u = s;
		u.entry_asm = "sub rsp, 0x28; mov ecx, 5; mov rax, IAT(Sleep); call qword ptr [rax]; ud2";
		const auto fu = w5_build( u, err );
		auto wu = w5_go( fu, emu::env_boundary::hle );
		CHECK( wu.p && wu.ok && wu.r.stop == emu::win::stop_kind::unimplemented_api && wu.r.detail.find( "KERNEL32.dll!Sleep" ) != std::string::npos, "unimplemented API: " + wu.r.detail );
		if ( wu.p ) CHECK( has_line( wu.p->state_dump( wu.r ), "last API call (--HLE): #1 KERNEL32.dll!Sleep" ), "the state dump names the API call" );
		// GetModuleHandleA with a name: stop
		u.imports = { { "kernel32.dll", { "GetModuleHandleA" } } };
		u.entry_asm = "sub rsp, 0x28; mov rcx, 0x140003000; mov rax, IAT(GetModuleHandleA); call qword ptr [rax]; ud2";
		auto wn = w5_go( w5_build( u, err ), emu::env_boundary::hle );
		CHECK( wn.p && wn.r.stop == emu::win::stop_kind::unimplemented_api, "GetModuleHandleA( name ): stop" );
	}

	// the T3 shape under --HLE: the TLS callback runs once before the entry with ( image base, DLL_PROCESS_ATTACH, 0 ); the TLS
	// block holds the template + zero fill; index 0 written; --Sys refuses
	void t_win_env_hle_tls()
	{
		w5_spec s;
		s.tls = true;
		s.callback_asm = "mov rax, 0x140003000; inc dword ptr [rax]; mov [rax+8], rcx; mov [rax+0x10], rdx; mov [rax+0x18], r8; ret";
		s.entry_asm = "mov rax, 0x140003000\n"
					  "cmp dword ptr [rax], 1; jne bad1\n"
					  "cmp qword ptr [rax+0x10], 1; jne bad2\n"
					  "mov rcx, 0x140000000; cmp [rax+8], rcx; jne bad3\n"
					  "mov rcx, gs:[0x58]; mov edx, dword ptr [rax+0x80]; mov rcx, [rcx+rdx*8]\n"
					  "mov rdx, 0x1122334455667788; cmp [rcx], rdx; jne bad4; cmp qword ptr [rcx+0x10], 0; jne bad5; cmp qword ptr [rcx+0x18], 0; jne bad5\n"
					  "mov edx, 0x2E; jmp done\n"
					  "bad1: mov edx, 0x101; jmp done\n"
					  "bad2: mov edx, 0x102; jmp done\n"
					  "bad3: mov edx, 0x103; jmp done\n"
					  "bad4: mov edx, 0x104; jmp done\n"
					  "bad5: mov edx, 0x105\n"
					  "done: mov r10, -1; mov eax, 0x2C; syscall; ud2\n";
		std::string err;
		const auto f = w5_build( s, err );
		CHECK( !f.empty(), "T3-shaped image: " + err );
		if ( f.empty() ) return;
		auto w = w5_go( f, emu::env_boundary::hle );
		CHECK( w.p && w.ok, "process: " + w.error );
		if ( w.p )
		{
			std::cout << "  --HLE: " << emu::win::name_of( w.r.stop ) << " exit " << std::hex << w.r.exit_status << std::dec << ", start " << std::hex << w.p->start_va() << std::dec << "\n";
			CHECK( w.r.stop == emu::win::stop_kind::terminated && w.r.exit_status == 0x2E, "exit status 2EH: the callback ran once with (base, 1, 0) and the TLS block is right" );
			CHECK( w.p->start_va() == emu::hle::trampoline_va && w4_read( *w.p, w5_base + 0x3018, 8 ) == 0 && w4_read( *w.p, w5_base + 0x3080, 4 ) == 0, "trampoline start, Reserved 0, index 0" );
		}
		auto sys = w5_go( f, emu::env_boundary::sys );
		CHECK( !sys.p && sys.error.find( "has a TLS directory" ) != std::string::npos, "--Sys refuses TLS: " + sys.error.substr( 0, 120 ) );
		// TLS without callbacks: no trampoline, the block still set up
		w5_spec n = s;
		n.tls_callback = false;
		n.callback_asm.clear();
		n.entry_asm = "mov rcx, gs:[0x58]; mov rcx, [rcx]; mov rdx, 0x99AABBCCDDEEFF00; mov edx, 0x2E; mov r8, 0x99AABBCCDDEEFF00; cmp [rcx+8], r8; je ok; mov edx, 0x109\n"
					  "ok: mov r10, -1; mov eax, 0x2C; syscall; ud2";
		auto wn = w5_go( w5_build( n, err ), emu::env_boundary::hle );
		CHECK( wn.p && wn.ok && wn.r.exit_status == 0x2E && wn.p->start_va() == wn.p->entry(), "TLS without callbacks: the entry starts directly, the block holds the template" );
	}

	// AGENT_RULES "Host safety": "Fuzz the layer." 4000 calls with random services and random arguments (pointers into every kind
	// of region and outside: unmapped, read-only, kernel, wrapping, huge sizes): every call ends answered / terminated / stopped,
	// an answered STATUS_SUCCESS wrote its OUT parameters into writable user memory, the host process is unaffected and the
	// engine's region table stays exact
	void t_win_env_syscall_fuzz()
	{
		std::string err;
		auto p = w5_create( w5_build( { w5_trivial }, err ), emu::env_boundary::sys, err );
		CHECK( p != nullptr, "process: " + err );
		if ( !p ) return;
		uint64_t x = 0x9E3779B97F4A7C15ull;
		auto rnd = [ & ]() { x ^= x >> 12; x ^= x << 25; x ^= x >> 27; return x * 0x2545F4914F6CDD1Dull; };
		const char* names[] = { "NtTerminateProcess", "NtClose", "NtAllocateVirtualMemory", "NtFreeVirtualMemory", "NtQueryVirtualMemory", "NtQueryInformationProcess",
								"NtCreateFile", "" };
		const uint64_t ptrs[] = { 0, 1, 0x10, 0xFFF, w5_scratch, w5_scratch + 0x8, w5_scratch + 0xFFF8, emu::win::guest_stack_top - 4, emu::win::guest_kuser_va, w5_base + 0x2000,
								  w5_base + 0x3000, emu::win::guest_kernel_gdt_va, 0x7FFFFFFEFFFDull, 0xFFFFFFFFFFFFF000ull, 0x8000000000000000ull, emu::win::guest_peb_va + 0xFF8 };
		uint64_t answered = 0, ok = 0, stopped = 0, terminated = 0, violations = 0;
		for ( int i = 0; i < 4000; i++ )
		{
			// fresh values behind the scratch pointers: small / page / huge sizes, allocation bases
			const uint64_t sizes[] = { 0, 1, 0x1000, 0x1234, 0x10000, 0x40000000, ~0ull, 0x7FFFFFFFFFFFull };
			w5_put( *p, w5_scratch, rnd() % 4 ? 0 : ( rnd() % 2 ? 0x30000000 + ( rnd() & 0xFFFFF ) : rnd() ) );
			w5_put( *p, w5_scratch + 8, sizes[ rnd() % 8 ] );
			emu::nt::call c;
			c.name = names[ rnd() % 8 ];
			c.number = uint32_t( rnd() & 0xFFF );
			for ( auto& a : c.args )
			{
				switch ( rnd() % 5 )
				{
					case 0: a = ptrs[ rnd() % ( sizeof( ptrs ) / 8 ) ]; break;
					case 1: a = rnd(); break;
					case 2: a = rnd() % 64; break;
					case 3: { const uint32_t v[] = { 0x1000, 0x2000, 0x3000, 0x103000, 0x4000, 0x8000, 0x80000, 0x30 }; a = v[ rnd() % 8 ]; break; }
					default: { const uint32_t v[] = { 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x104, 3, 0 }; a = v[ rnd() % 11 ]; break; }
				}
			}
			if ( rnd() % 3 == 0 ) c.args[ 0 ] = emu::nt::current_process;
			c.stack_args = rnd() % 8 != 0;
			if ( rnd() % 2 == 0 )   // half the calls well-formed (then one argument perturbed in a quarter of them): the answered paths get exercised too
			{
				const uint64_t cp = emu::nt::current_process, S = w5_scratch;
				const uint32_t types[] = { 0x3000, 0x103000, 0x2000, 0x1000 }, prots[] = { 2, 4, 0x10, 0x20, 0x40, 1 }, frees[] = { 0x8000, 0x4000 };
				const uint64_t bases[] = { 0, 0x30000000, 0x10000 };
				const std::string n = c.name;
				if ( n == "NtAllocateVirtualMemory" )
				{
					w5_put( *p, S, bases[ rnd() % 3 ] );
					w5_put( *p, S + 8, 0x1000 * ( 1 + rnd() % 8 ) );
					c.args = { cp, S, 0, S + 8, types[ rnd() % 4 ], prots[ rnd() % 6 ] };
				}
				else if ( n == "NtFreeVirtualMemory" )
				{
					uint64_t b = 0;
					if ( !p->kern().allocations().empty() ) b = p->kern().allocations().begin()->first;
					w5_put( *p, S, b );
					w5_put( *p, S + 8, rnd() % 2 ? 0 : 0x1000 );
					c.args = { cp, S, S + 8, frees[ rnd() % 2 ], 0, 0 };
				}
				else if ( n == "NtQueryVirtualMemory" )
				{
					uint64_t a = rnd() % 2 ? 0x20000000 : ( p->kern().allocations().empty() ? 0x40000000 : p->kern().allocations().begin()->first );
					c.args = { cp, a, 0, S + 0x100, 0x30, rnd() % 2 ? S + 0x180 : 0 };
				}
				else if ( n == "NtQueryInformationProcess" )
				{
					const uint32_t cls[] = { 0, 7, 26, 29 }, lens[] = { 0x30, 8, 8, 4 };
					const size_t k = size_t( rnd() % 4 );
					c.args = { cp, cls[ k ], S + 0x200, lens[ k ], rnd() % 2 ? S + 0x280 : 0, 0 };
				}
				c.stack_args = true;
				if ( rnd() % 4 == 0 ) c.args[ size_t( rnd() % 6 ) ] = ptrs[ rnd() % ( sizeof( ptrs ) / 8 ) ];
			}
			const auto r = p->kern().dispatch( c );
			if ( r.kind == emu::nt::reply_kind::done )
			{
				answered++;
				if ( r.status == emu::nt::status::success )
				{
					ok++;
					CHECK( !r.writes.empty() || c.name == std::string( "NtClose" ), "a STATUS_SUCCESS answer wrote its OUT parameters" );
				}
				else if ( !r.writes.empty() )
				{
					violations++;
					CHECK( false, "an error status with OUT parameters written: " + c.name + " " + r.text );
				}
			}
			else if ( r.kind == emu::nt::reply_kind::terminated )
			{
				terminated++;
				CHECK( c.name == std::string( "NtTerminateProcess" ) && c.args[ 0 ] == emu::nt::current_process, "only NtTerminateProcess( NtCurrentProcess() ) terminates" );
			}
			else
			{
				stopped++;
				CHECK( !r.text.empty(), "a stop names its reason" );
			}
			if ( i % 250 == 0 ) CHECK( p->eng().check_regions( err ), "engine region table = Unicorn's after call " + std::to_string( i ) + ": " + err );
			if ( p->kern().allocations().size() > 64 )   // keep the guest memory bounded: release everything
			{
				std::vector<uint64_t> bases;
				for ( auto& [ b, a ] : p->kern().allocations() ) bases.push_back( b );
				for ( auto b : bases )
				{
					w5_put( *p, w5_scratch, b );
					w5_put( *p, w5_scratch + 8, 0 );
					CHECK( w5_ok( w5_call( *p, "NtFreeVirtualMemory", { emu::nt::current_process, w5_scratch, w5_scratch + 8, emu::nt::mem::release } ) ), "release" );
				}
			}
		}
		std::cout << "  fuzz: 4000 calls, " << answered << " answered (" << ok << " STATUS_SUCCESS), " << terminated << " terminated, " << stopped << " stopped, "
				  << p->kern().allocations().size() << " allocations left, " << std::hex << p->eng().mapped_bytes() << std::dec << " bytes mapped\n";
		CHECK( answered + terminated + stopped == 4000 && violations == 0 && ok > 50 && stopped > 100, "every call answered / terminated / stopped" );
		CHECK( p->eng().check_regions( err ), "engine region table exact at the end: " + err );
	}

	// host safety of the import reader: mutated import tables never crash pe::imports; every outcome is a list or a refusal
	void t_pe_imports_fuzz()
	{
		w5_spec s;
		s.imports = { { "KERNEL32.dll", { "GetCurrentProcessId", "ExitProcess" } }, { "user32.dll", { "MessageBoxA" } } };
		s.entry_asm = "ret";
		std::string err;
		const auto good = w5_build( s, err );
		emu::pe::image img;
		std::vector<emu::pe::import_module> mods;
		CHECK( emu::pe::parse( good, img, err ) && emu::pe::imports( good, img, mods, err ) && mods.size() == 2 && mods[ 0 ].entries.size() == 2 &&
				   mods[ 1 ].entries[ 0 ].name == "MessageBoxA" && mods[ 0 ].entries[ 1 ].iat_rva == 0x2208,
			   "the import table reads back: " + err );
		// the .rdata raw data: after the 400H headers and the .text raw data
		const auto& sec = img.sections;
		const uint64_t rd_off = sec.size() > 1 ? sec[ 1 ].raw_offset : 0x600;
		uint64_t x = 0x1234567887654321ull, lists = 0, refused = 0;
		auto rnd = [ & ]() { x ^= x >> 12; x ^= x << 25; x ^= x >> 27; return x * 0x2545F4914F6CDD1Dull; };
		for ( int i = 0; i < 3000; i++ )
		{
			auto f = good;
			const int flips = 1 + int( rnd() % 5 );
			for ( int k = 0; k < flips; k++ ) f[ size_t( rd_off + rnd() % 0x800 ) ] = uint8_t( rnd() % 3 ? rnd() : 0xFF );
			emu::pe::image im;
			std::vector<emu::pe::import_module> m;
			std::string why;
			if ( !emu::pe::parse( f, im, why ) ) { refused++; continue; }
			if ( emu::pe::imports( f, im, m, why ) ) lists++;
			else
			{
				refused++;
				CHECK( !why.empty() && m.empty(), "a refusal names its reason and returns nothing" );
			}
		}
		std::cout << "  import fuzz: 3000 mutated tables, " << lists << " read, " << refused << " refused\n";
		CHECK( lists + refused == 3000 && refused > 100, "every mutation ended in a list or a refusal" );
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
		{ "pe_parse_refusals", t_pe_parse_refusals },
		{ "pe_fuzz", t_pe_fuzz },
		{ "pe_map_relocate", t_pe_map_relocate },
		{ "sysdll_syscall_table", t_sysdll_syscall_table },
		{ "win_env_layout", t_win_env_layout },
		{ "win_env_terminate", t_win_env_terminate },
		{ "win_env_loud_stops", t_win_env_loud_stops },
		{ "win_env_refusals", t_win_env_refusals },
		{ "win_env_no_passthrough_runtime", t_win_env_no_passthrough_runtime },
		{ "nt_kernel_vm", t_nt_kernel_vm },
		{ "nt_kernel_process_info", t_nt_kernel_process_info },
		{ "nt_kernel_close_terminate", t_nt_kernel_close_terminate },
		{ "win_env_syscall_layer_run", t_win_env_syscall_layer_run },
		{ "win_env_hle_imports", t_win_env_hle_imports },
		{ "win_env_hle_tls", t_win_env_hle_tls },
		{ "win_env_syscall_fuzz", t_win_env_syscall_fuzz },
		{ "pe_imports_fuzz", t_pe_imports_fuzz },
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
