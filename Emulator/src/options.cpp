// emu::options (plan 3.M1, ledger U1940; plan 3.M3, ledger U1975: the emu-snippet v1 flags): see include/emu/options.hpp. Pure
// string handling: no file, no directory, no host query.
#include <emu/options.hpp>

#include <cstdio>

namespace emu
{
	namespace
	{
		std::string lower( std::string_view s )
		{
			std::string r( s );
			for ( auto& c : r )
				if ( c >= 'A' && c <= 'Z' ) c = char( c - 'A' + 'a' );
			return r;
		}

		std::string hex64( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", ( unsigned long long ) v );
			return b;
		}

		int hex_digit( char c )
		{
			if ( c >= '0' && c <= '9' ) return c - '0';
			if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
			if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
			return -1;
		}

		parse_result fail( std::string msg )
		{
			parse_result r;
			r.ok = false;
			r.error = std::move( msg );
			return r;
		}
	}

	const char* milestone() { return "M3"; }   // U1952: the engine core (plan 3.M2); U1975: emu-snippet v1 (plan 3.M3)

	bool parse_u64( std::string_view text, uint64_t& out )
	{
		out = 0;
		if ( text.empty() ) return false;
		uint64_t v = 0;
		if ( text.size() > 2 && text[ 0 ] == '0' && ( text[ 1 ] == 'x' || text[ 1 ] == 'X' ) )
		{
			std::string_view h = text.substr( 2 );
			if ( h.empty() || h.size() > 64 ) return false;
			for ( char c : h )
			{
				if ( c == '\'' || c == '_' ) continue;
				int d = hex_digit( c );
				if ( d < 0 ) return false;
				if ( v >> 60 ) return false;     // the next shift would drop a set bit
				v = ( v << 4 ) | uint64_t( d );
			}
		}
		else
		{
			for ( char c : text )
			{
				if ( c == '\'' || c == '_' ) continue;
				if ( c < '0' || c > '9' ) return false;
				uint64_t d = uint64_t( c - '0' );
				if ( v > ( UINT64_MAX - d ) / 10 ) return false;
				v = v * 10 + d;
			}
		}
		out = v;
		return true;
	}

	bool parse_hex_bytes( std::string_view text, std::vector<uint8_t>& out, std::string& error )
	{
		out.clear();
		int hi = -1;
		for ( size_t i = 0; i < text.size(); i++ )
		{
			char c = text[ i ];
			if ( c == ' ' || c == ',' || c == '\t' )
			{
				if ( hi >= 0 ) { error = "odd number of hex digits before position " + std::to_string( i ); out.clear(); return false; }
				continue;
			}
			int d = hex_digit( c );
			if ( d < 0 ) { error = std::string( "not a hex digit at position " ) + std::to_string( i ); out.clear(); return false; }
			if ( hi < 0 ) { hi = d; continue; }
			if ( out.size() >= max_hex_bytes ) { error = "more than " + std::to_string( max_hex_bytes ) + " bytes"; out.clear(); return false; }
			out.push_back( uint8_t( ( hi << 4 ) | d ) );
			hi = -1;
		}
		if ( hi >= 0 ) { error = "odd number of hex digits"; out.clear(); return false; }
		if ( out.empty() ) { error = "no bytes"; return false; }
		return true;
	}

	const char* name_of( tool_kind v ) { return v == tool_kind::oep ? "emu-oep" : "emu-snippet"; }
	const char* name_of( cpu_profile_choice v )
	{
		switch ( v )
		{
			case cpu_profile_choice::host: return "host";
			case cpu_profile_choice::guest: return "guest";
			case cpu_profile_choice::max: return "max";
			case cpu_profile_choice::runtime_cpuid: return "cpuid-runtime";
		}
		return "?";
	}
	const char* name_of( env_boundary v ) { return v == env_boundary::sys ? "Sys" : "HLE"; }
	const char* name_of( rng_source v ) { return v == rng_source::seeded ? "seeded" : "HostSeed"; }
	const char* name_of( snippet_input_kind v )
	{
		switch ( v )
		{
			case snippet_input_kind::none: return "none";
			case snippet_input_kind::hex: return "hex";
			case snippet_input_kind::asm_text: return "asm";
			case snippet_input_kind::image: return "image";
		}
		return "?";
	}

	std::string usage( tool_kind tool )
	{
		std::string common =
			"  profile   --host (default: the captured bare-host CPUID profile) | --guest (captured VMware guest) | --max (coverage)\n"
			"            | --cpuid-runtime (the running machine's CPUID; explicit only, decision 3.0.3)\n"
			"  boundary  --Sys (default: syscall-level emulation of the real system DLLs) | --HLE (API-level)\n"
			"  rng       --seeded (default, deterministic) | --HostSeed (hardware DRNG)\n"
			"  --max-insns N   instruction bound (decimal or 0x hex, 64-bit)\n"
			"  --out-dir DIR   output folder (recorded; milestone M1 creates nothing)\n"
			"  -h, --help\n";
		if ( tool == tool_kind::oep )
			return "usage: emu-oep [options] INPUT\n"
				   "  INPUT     the PE image to emulate (recorded; milestone M1 opens nothing, emulation arrives with M4 / M5)\n" + common;
		return "usage: emu-snippet (--hex BYTES | --asm TEXT | --state FILE.json | --batch FILE.jsonl) [state] [stops] [checks] [output] [options]\n"
			   "  code      --hex BYTES (\"48 31 c0 c3\") | --asm TEXT (Keystone, statements separated by ;) | --at ADDR (default 0x140001000)\n"
			   "            --image PATH --rva RVA: a PE image mapped as data - arrives with milestone M4 (refused in M3)\n"
			   "  state     --set REG=VALUE (scalars 0x.. / decimal; xmm / ymm / zmm / st as hex bytes, low byte first) | --map ADDR:SIZE[:rwx]\n"
			   "            | --mem ADDR=HEXBYTES | --state FILE.json (a snippet spec; flags given beside it are added)\n"
			   "            without --set rsp a 64 KiB stack is mapped and [rsp] = the return sentinel (returning ends the run)\n"
			   "  stops     --stop-at ADDR | --stop-ret-depth N | --max-insns N (default 1000000) | --syscall stop|cpu (default stop: SYSCALL /\n"
			   "            SYSENTER / INT n stop before they execute and are reported; never forwarded to the host) | --external stop|fault\n"
			   "  checks    --expect REG=VALUE | --expect-mem ADDR=HEXBYTES | --expect-fault none|#UD|#GP(0)|N | --expect-strict\n"
			   "  output    --trace (per instruction: address, bytes, disassembly, changed registers / flags, memory reads / writes)\n"
			   "            | --dump-mem ADDR:LEN | --quiet\n"
			   "  batch     --batch FILE.jsonl: one JSON spec per line (+ {\"blob\":NAME,\"hex\":...} lines); summary \"snippets: N, ...\"\n"
			   "  engine    --avx512 | --amx | --apx | --avx10 N (opt-ins, as emu-alltest)\n"
			   "  exit      0 every check passed, 1 a check differs, 2 usage, 3 a snippet could not be set up or run\n" + common;
	}

	parse_result parse_options( tool_kind tool, const std::vector<std::string>& args )
	{
		parse_result r;
		run_options& o = r.options;
		o.tool = tool;
		std::string profile_flag, boundary_flag, rng_flag;
		bool out_dir_given = false, input_given = false;

		auto set_choice = [&]( std::string& seen, const std::string& flag, bool& given, auto& field, auto value ) -> std::string {
			if ( given && seen != flag ) return "conflicting options " + seen + " and " + flag;
			seen = flag;
			given = true;
			field = value;
			return {};
		};

		for ( size_t i = 0; i < args.size(); i++ )
		{
			const std::string& raw = args[ i ];
			std::string a = lower( raw );
			auto value = [&]( std::string& out ) -> bool {
				if ( i + 1 >= args.size() ) return false;
				out = args[ ++i ];
				return true;
			};
			std::string err;

			if ( a == "-h" || a == "--help" ) { o.help = true; continue; }
			else if ( a == "--host" ) err = set_choice( profile_flag, a, o.profile_given, o.profile, cpu_profile_choice::host );
			else if ( a == "--guest" ) err = set_choice( profile_flag, a, o.profile_given, o.profile, cpu_profile_choice::guest );
			else if ( a == "--max" ) err = set_choice( profile_flag, a, o.profile_given, o.profile, cpu_profile_choice::max );
			else if ( a == "--cpuid-runtime" ) err = set_choice( profile_flag, a, o.profile_given, o.profile, cpu_profile_choice::runtime_cpuid );
			else if ( a == "--sys" ) err = set_choice( boundary_flag, a, o.boundary_given, o.boundary, env_boundary::sys );
			else if ( a == "--hle" ) err = set_choice( boundary_flag, a, o.boundary_given, o.boundary, env_boundary::hle );
			else if ( a == "--seeded" ) err = set_choice( rng_flag, a, o.rng_given, o.rng, rng_source::seeded );
			else if ( a == "--hostseed" ) err = set_choice( rng_flag, a, o.rng_given, o.rng, rng_source::host_seed );
			else if ( a == "--max-insns" )
			{
				std::string v;
				if ( !value( v ) ) return fail( "missing value for " + raw );
				if ( o.max_insns_given ) return fail( raw + " given twice" );
				if ( !parse_u64( v, o.max_insns ) ) return fail( "bad 64-bit number for " + raw + ": \"" + v + "\"" );
				if ( o.max_insns == 0 ) return fail( raw + " must be at least 1" );
				o.max_insns_given = true;
			}
			else if ( a == "--out-dir" )
			{
				if ( out_dir_given ) return fail( raw + " given twice" );
				if ( !value( o.out_dir ) || o.out_dir.empty() ) return fail( "missing value for " + raw );
				out_dir_given = true;
			}
			else if ( tool == tool_kind::snippet && ( a == "--hex" || a == "--asm" || a == "--image" ) )
			{
				if ( o.snippet_input != snippet_input_kind::none ) return fail( "more than one snippet input (--hex / --asm / --image)" );
				std::string v;
				if ( !value( v ) || v.empty() ) return fail( "missing value for " + raw );
				if ( a == "--hex" )
				{
					std::string why;
					if ( !parse_hex_bytes( v, o.snippet_bytes, why ) ) return fail( "bad --hex bytes: " + why );
					o.snippet_input = snippet_input_kind::hex;
				}
				else if ( a == "--asm" ) { o.snippet_asm = v; o.snippet_input = snippet_input_kind::asm_text; }
				else { o.input_path = v; o.snippet_input = snippet_input_kind::image; }
			}
			else if ( tool == tool_kind::snippet && ( a == "--rva" || a == "--stop-at" || a == "--stop-ret-depth" ) )
			{
				std::string v;
				if ( !value( v ) ) return fail( "missing value for " + raw );
				uint64_t* field = a == "--rva" ? &o.image_rva : a == "--stop-at" ? &o.stop_at : &o.stop_ret_depth;
				bool* given = a == "--rva" ? &o.image_rva_given : a == "--stop-at" ? &o.stop_at_given : &o.stop_ret_depth_given;
				if ( *given ) return fail( raw + " given twice" );
				if ( !parse_u64( v, *field ) ) return fail( "bad 64-bit number for " + raw + ": \"" + v + "\"" );
				*given = true;
			}
			else if ( tool == tool_kind::snippet && ( a == "--expect" || a == "--set" ) )
			{
				std::string v;
				if ( !value( v ) ) return fail( "missing value for " + raw );
				size_t eq = v.find( '=' );
				if ( eq == std::string::npos || eq == 0 || eq > 16 ) return fail( raw + " wants REG=VALUE, got \"" + v + "\"" );
				expect_item e;
				e.reg = lower( std::string_view( v ).substr( 0, eq ) );
				for ( char c : e.reg )
					if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) ) return fail( raw + ": bad register name \"" + e.reg + "\"" );
				if ( !( e.reg[ 0 ] >= 'a' && e.reg[ 0 ] <= 'z' ) ) return fail( raw + ": bad register name \"" + e.reg + "\"" );
				// U1975: a 64-bit number, hex bytes (low byte first) for the byte fields (xmm / ymm / zmm / st), or SEXP:MANT for st
				const std::string_view val = std::string_view( v ).substr( eq + 1 );
				bool bytes = !val.empty() && val.size() % 2 == 0;
				for ( char c : val )
					if ( hex_digit( c ) < 0 ) bytes = false;
				const bool st_form = e.reg.size() == 3 && e.reg[ 0 ] == 's' && e.reg[ 1 ] == 't' && val.find( ':' ) != std::string_view::npos;
				if ( !parse_u64( val, e.value ) && !bytes && !st_form ) return fail( raw + ": bad value in \"" + v + "\" (a 64-bit number, or hex bytes)" );
				if ( a == "--expect" )
				{
					o.expects.push_back( e );
					o.expect_raw.push_back( v );
				}
				else
					o.sets.push_back( v );
			}
			else if ( tool == tool_kind::snippet && ( a == "--mem" || a == "--map" || a == "--expect-mem" || a == "--dump-mem" || a == "--state" || a == "--batch" ||
													  a == "--expect-fault" || a == "--syscall" || a == "--external" ) )
			{
				std::string v;
				if ( !value( v ) || v.empty() ) return fail( "missing value for " + raw );
				if ( a == "--mem" ) o.mems.push_back( v );
				else if ( a == "--map" ) o.maps.push_back( v );
				else if ( a == "--expect-mem" ) o.expect_mems.push_back( v );
				else if ( a == "--dump-mem" ) o.dump_mems.push_back( v );
				else
				{
					std::string& dst = a == "--state" ? o.state_file : a == "--batch" ? o.batch_file : a == "--expect-fault" ? o.expect_fault : a == "--syscall" ? o.syscall : o.external;
					if ( !dst.empty() ) return fail( raw + " given twice" );
					dst = v;
					if ( a == "--syscall" && v != "stop" && v != "cpu" ) return fail( "--syscall stop|cpu, got \"" + v + "\"" );
					if ( a == "--external" && v != "stop" && v != "fault" ) return fail( "--external stop|fault, got \"" + v + "\"" );
				}
			}
			else if ( tool == tool_kind::snippet && a == "--at" )
			{
				std::string v;
				if ( !value( v ) ) return fail( "missing value for " + raw );
				if ( o.code_address_given ) return fail( raw + " given twice" );
				if ( !parse_u64( v, o.code_address ) ) return fail( "bad 64-bit number for " + raw + ": \"" + v + "\"" );
				o.code_address_given = true;
			}
			else if ( tool == tool_kind::snippet && ( a == "--trace" || a == "--quiet" || a == "--expect-strict" || a == "--avx512" || a == "--amx" || a == "--apx" ) )
			{
				if ( a == "--trace" ) o.trace = true;
				else if ( a == "--quiet" ) o.quiet = true;
				else if ( a == "--expect-strict" ) o.expect_strict = true;
				else if ( a == "--avx512" ) o.avx512 = 1;
				else if ( a == "--amx" ) o.amx = 1;
				else o.apx = 1;
			}
			else if ( tool == tool_kind::snippet && a == "--avx10" )
			{
				std::string v;
				uint64_t n = 0;
				if ( !value( v ) || !parse_u64( v, n ) || n == 0 || n > 0xFFFF ) return fail( "--avx10 N (1, 2, ...)" );
				o.avx10 = int( n );
			}
			else if ( !raw.empty() && raw[ 0 ] == '-' ) return fail( "unknown option \"" + raw + "\"" );
			else if ( tool == tool_kind::oep )
			{
				if ( input_given ) return fail( "more than one INPUT (\"" + o.input_path + "\", \"" + raw + "\")" );
				o.input_path = raw;
				input_given = true;
			}
			else return fail( "unexpected argument \"" + raw + "\"" );

			if ( !err.empty() ) return fail( err );
		}

		if ( o.help ) { r.ok = true; return r; }
		if ( tool == tool_kind::oep && !input_given ) return fail( "missing INPUT" );
		if ( tool == tool_kind::snippet )
		{
			if ( !o.batch_file.empty() )
			{
				// U1975: a batch file carries every setting; only --quiet may stand beside it
				if ( o.snippet_input != snippet_input_kind::none || !o.state_file.empty() || !o.sets.empty() || !o.mems.empty() || !o.maps.empty() ||
					 !o.expects.empty() || !o.expect_mems.empty() || !o.expect_fault.empty() || o.trace || o.stop_at_given || o.stop_ret_depth_given ||
					 o.max_insns_given || o.profile_given || o.rng_given || o.code_address_given || !o.syscall.empty() || !o.external.empty() ||
					 !o.dump_mems.empty() || o.expect_strict || o.avx512 || o.amx || o.apx || o.avx10 )
					return fail( "--batch takes every setting from its file (only --quiet may be given beside it)" );
				r.ok = true;
				return r;
			}
			if ( o.snippet_input == snippet_input_kind::none && o.state_file.empty() ) return fail( "missing snippet input (--hex / --asm / --state / --batch)" );
			if ( o.snippet_input == snippet_input_kind::image && !o.image_rva_given ) return fail( "--image needs --rva" );
			if ( o.snippet_input != snippet_input_kind::image && o.image_rva_given ) return fail( "--rva needs --image" );
		}
		r.ok = true;
		return r;
	}

	std::vector<std::string> describe( const run_options& o )
	{
		std::vector<std::string> d;
		d.push_back( std::string( "tool: " ) + name_of( o.tool ) );
		d.push_back( std::string( "milestone: " ) + milestone() +
					 ( o.tool == tool_kind::oep ? " (emu-oep parses options only: nothing opened, nothing created, nothing emulated)"
												: " (emu-snippet emulates the snippet on the Emulator lib engine; nothing is created or written)" ) );
		d.push_back( std::string( "cpu profile: " ) + name_of( o.profile ) +
					 ( o.profile_given ? " (given)" : " (default, decision 3.0.3: the captured bare-host profile)" ) );
		d.push_back( std::string( "boundary: " ) + name_of( o.boundary ) + ( o.boundary_given ? " (given)" : " (default, D3)" ) );
		d.push_back( std::string( "rng: " ) + name_of( o.rng ) + ( o.rng_given ? " (given)" : " (default, D8)" ) );
		d.push_back( "max-insns: " + ( o.max_insns_given ? std::to_string( o.max_insns ) + " (" + hex64( o.max_insns ) + ")" : std::string( "not given" ) ) );
		d.push_back( "out-dir: " + ( o.out_dir.empty() ? std::string( "(none)" ) : o.out_dir + " (recorded, not created)" ) );
		if ( o.tool == tool_kind::oep )
			d.push_back( "input: " + o.input_path + " (recorded, not opened)" );
		else
		{
			std::string in = std::string( "snippet input: " ) + name_of( o.snippet_input );
			if ( o.snippet_input == snippet_input_kind::hex )
			{
				in += " (" + std::to_string( o.snippet_bytes.size() ) + " bytes:";
				for ( size_t k = 0; k < o.snippet_bytes.size() && k < 16; k++ )
				{
					char b[ 4 ];
					std::snprintf( b, sizeof( b ), " %02x", o.snippet_bytes[ k ] );
					in += b;
				}
				in += o.snippet_bytes.size() > 16 ? " ...)" : ")";
			}
			else if ( o.snippet_input == snippet_input_kind::asm_text )
				in += " (" + std::to_string( o.snippet_asm.size() ) + " characters, assembled with Keystone)";
			else if ( o.snippet_input == snippet_input_kind::image )
				in += " (" + o.input_path + " at RVA " + hex64( o.image_rva ) + "; not opened: arrives with milestone M4)";
			d.push_back( in );
			d.push_back( "stop-at: " + ( o.stop_at_given ? hex64( o.stop_at ) : std::string( "not given" ) ) );
			d.push_back( "stop-ret-depth: " + ( o.stop_ret_depth_given ? std::to_string( o.stop_ret_depth ) : std::string( "not given" ) ) );
			if ( !o.state_file.empty() ) d.push_back( "state file: " + o.state_file );
			d.push_back( "state: " + std::to_string( o.sets.size() ) + " register(s), " + std::to_string( o.maps.size() ) + " map(s), " + std::to_string( o.mems.size() ) +
						 " memory write(s)" );
			d.push_back( std::string( "syscall: " ) + ( o.syscall.empty() ? "stop (default)" : o.syscall ) + ", external: " + ( o.external.empty() ? "stop (default)" : o.external ) );
			d.push_back( "expect: " + std::to_string( o.expects.size() ) + " assertion(s)" + ( o.expect_strict ? " (strict)" : "" ) );
			for ( auto& e : o.expect_raw )
				d.push_back( "  expect " + e );
		}
		return d;
	}
}
