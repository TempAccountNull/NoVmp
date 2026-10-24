// emu::options (plan 3.M1, ledger U1940): see include/emu/options.hpp. Pure string handling: no file, no directory, no
// host query.
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

	const char* milestone() { return "M1"; }

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
		return "usage: emu-snippet (--hex BYTES | --asm TEXT | --image PATH --rva RVA) [--stop-at ADDR] [--stop-ret-depth N]\n"
			   "                   [--expect REG=VALUE ...] [options]\n"
			   "  --hex BYTES       raw bytes, e.g. \"48 31 c0 c3\"\n"
			   "  --asm TEXT        assembly (assembled with Keystone from milestone M3 on)\n"
			   "  --image PATH      a PE image on disk mapped as data (recorded; M1 opens nothing), with --rva RVA\n" + common;
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
			else if ( tool == tool_kind::snippet && a == "--expect" )
			{
				std::string v;
				if ( !value( v ) ) return fail( "missing value for " + raw );
				size_t eq = v.find( '=' );
				if ( eq == std::string::npos || eq == 0 || eq > 16 ) return fail( "--expect wants REG=VALUE, got \"" + v + "\"" );
				expect_item e;
				e.reg = lower( std::string_view( v ).substr( 0, eq ) );
				for ( char c : e.reg )
					if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) ) return fail( "--expect: bad register name \"" + e.reg + "\"" );
				if ( !( e.reg[ 0 ] >= 'a' && e.reg[ 0 ] <= 'z' ) ) return fail( "--expect: bad register name \"" + e.reg + "\"" );
				if ( !parse_u64( std::string_view( v ).substr( eq + 1 ), e.value ) ) return fail( "--expect: bad 64-bit value in \"" + v + "\"" );
				o.expects.push_back( e );
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
			if ( o.snippet_input == snippet_input_kind::none ) return fail( "missing snippet input (--hex / --asm / --image)" );
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
		d.push_back( std::string( "milestone: " ) + milestone() + " (options only: nothing opened, nothing created, nothing emulated)" );
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
				in += " (" + std::to_string( o.snippet_asm.size() ) + " characters, not assembled in M1)";
			else if ( o.snippet_input == snippet_input_kind::image )
				in += " (" + o.input_path + " at RVA " + hex64( o.image_rva ) + "; recorded, not opened)";
			d.push_back( in );
			d.push_back( "stop-at: " + ( o.stop_at_given ? hex64( o.stop_at ) : std::string( "not given" ) ) );
			d.push_back( "stop-ret-depth: " + ( o.stop_ret_depth_given ? std::to_string( o.stop_ret_depth ) : std::string( "not given" ) ) );
			d.push_back( "expect: " + std::to_string( o.expects.size() ) + " assertion(s)" );
			for ( auto& e : o.expects )
				d.push_back( "  expect " + e.reg + " = " + hex64( e.value ) + " (checked from milestone M3 on)" );
		}
		return d;
	}
}
