// emu-snippet (plan 3.M3 = emu-snippet v1, plan 3.A 2.7; ledgers U1940 (M1 skeleton), U1975 (v1)): emulates an isolated
// piece of x86-64 code on the Emulator lib (emu::snippet::run on emu::engine / emu::reg) and reports what it did.
//
//   emu-snippet --hex BYTES | --asm TEXT | --state FILE.json [state] [stops] [checks] [output] [options]
//   emu-snippet --batch FILE.jsonl [--quiet]
//   (emu-snippet --help prints every flag)
//
// Input: raw hex bytes, an assembly listing (Keystone), or a JSON spec (code, regions, registers, stops, expectations). The
// default CPU profile is --host, the captured bare-host profile (decision 3.0.3). Output: the stop reason, the checks, the
// optional per-instruction trace and the final state. A SYSCALL / SYSENTER / INT n stops the run before it executes and is
// reported (--syscall stop, the default); control leaving the executable mapped memory stops and is reported (--external
// stop). Nothing is ever forwarded to the host and no guest byte runs on the host CPU (AGENT_RULES "Host safety"); files are
// only read, through emu::read_input_file. Exit: 0 every check passed, 1 a check differs, 2 usage, 3 a snippet could not be
// set up or run.
#include <emu/assemble.hpp>
#include <emu/input_file.hpp>
#include <emu/json.hpp>
#include <emu/options.hpp>
#include <emu/snippet.hpp>

#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
	constexpr uint64_t max_state_file = 64ull << 20;
	constexpr uint64_t max_batch_line = 64ull << 20;
	constexpr uint64_t max_batch_file = 4ull << 30;

	int usage_error( const std::string& why )
	{
		std::cout << "[emu-snippet] error: " << why << "\n" << emu::usage( emu::tool_kind::snippet );
		return 2;
	}

	// the command-line flags onto a spec (a --state file's spec first, the flags added to it)
	bool apply_flags( const emu::run_options& o, emu::snippet::spec& s, std::string& error )
	{
		using namespace emu::snippet;
		if ( o.profile_given ) s.config.profile = o.profile;
		if ( o.rng_given ) s.config.rng = o.rng;
		const auto oc = emu::config_from( o );
		if ( oc.avx512 ) s.config.avx512 = oc.avx512;
		if ( oc.amx ) s.config.amx = oc.amx;
		if ( oc.apx ) s.config.apx = oc.apx;
		if ( oc.avx10 ) s.config.avx10 = oc.avx10;
		if ( o.code_address_given ) s.code_address = o.code_address;
		if ( o.snippet_input == emu::snippet_input_kind::hex ) s.code = o.snippet_bytes;
		else if ( o.snippet_input == emu::snippet_input_kind::asm_text )
		{
			if ( !emu::assemble( o.snippet_asm, s.code_address, s.code, error ) ) { error = "--asm: " + error; return false; }
		}
		for ( auto& m : o.maps )
		{
			region r;
			if ( !parse_map( m, r, error ) ) { error = "--map: " + error; return false; }
			s.regions.push_back( r );
		}
		for ( auto& m : o.mems )
		{
			mem_bytes b;
			if ( !parse_mem_assignment( m, b, error ) ) { error = "--mem: " + error; return false; }
			region* in = nullptr;
			for ( auto& r : s.regions )
				if ( b.address >= r.base && b.address - r.base + b.bytes.size() <= r.size ) in = &r;
			if ( !in ) { error = "--mem " + m + ": not inside a --map region (map it first)"; return false; }
			in->data.push_back( b );
		}
		for ( auto& a : o.sets )
		{
			field_value f;
			if ( !parse_assignment( a, f, error ) ) { error = "--set: " + error; return false; }
			s.regs.push_back( f );
		}
		if ( o.stop_at_given ) { s.stop_at_given = true; s.stop_at = o.stop_at; }
		if ( o.stop_ret_depth_given ) { s.ret_depth_given = true; s.ret_depth = o.stop_ret_depth; }
		if ( o.max_insns_given ) s.max_insns = o.max_insns;
		if ( o.syscall == "cpu" ) s.syscall = syscall_policy::cpu;
		else if ( o.syscall == "stop" ) s.syscall = syscall_policy::stop;
		if ( o.external == "fault" ) s.external = external_policy::fault;
		else if ( o.external == "stop" ) s.external = external_policy::stop;
		if ( o.trace ) s.trace = true;
		for ( auto& d : o.dump_mems )
		{
			const size_t c = d.find( ':' );
			range r;
			if ( c == std::string::npos || !emu::parse_u64( d.substr( 0, c ), r.address ) || !emu::parse_u64( d.substr( c + 1 ), r.size ) || r.size == 0 ||
				 r.size > emu::max_io_bytes )
			{
				error = "--dump-mem ADDR:LEN, got \"" + d + "\"";
				return false;
			}
			s.dump_mem.push_back( r );
		}
		for ( auto& e : o.expect_raw )
		{
			field_value f;
			if ( !parse_assignment( e, f, error ) ) { error = "--expect: " + error; return false; }
			s.expect.regs.push_back( f );
			s.expect.present = true;
		}
		for ( auto& m : o.expect_mems )
		{
			mem_bytes b;
			if ( !parse_mem_assignment( m, b, error ) ) { error = "--expect-mem: " + error; return false; }
			s.expect.mem.push_back( b );
			s.expect.present = true;
		}
		if ( !o.expect_fault.empty() )
		{
			if ( !parse_fault( o.expect_fault, s.expect.vector, s.expect.error_code, error ) ) { error = "--expect-fault: " + error; return false; }
			s.expect.fault_given = true;
			s.expect.present = true;
		}
		if ( o.expect_strict )
		{
			s.expect.strict = true;
			s.expect.present = true;
		}
		if ( s.expect.present && s.expect.label.empty() ) s.expect.label = "expect";
		return true;
	}

	int run_one( const emu::run_options& o )
	{
		emu::snippet::spec s;
		std::string err;
		if ( !o.state_file.empty() )
		{
			std::string text;
			emu::json::value v;
			if ( !emu::read_input_file( o.state_file, max_state_file, text, err ) || !emu::json::parse( text, v, err ) || !emu::snippet::spec_from_json( v, s, err ) )
			{
				std::cout << "[emu-snippet] error: --state " << o.state_file << ": " << err << "\n";
				return 2;
			}
			if ( s.name.empty() ) s.name = o.state_file;
		}
		else
		{
			s.name = "command line";
			s.config = emu::config_from( o );
			s.auto_stack = true;
		}
		if ( !apply_flags( o, s, err ) ) return usage_error( err );
		if ( s.code.empty() ) return usage_error( "no code (--hex / --asm, or \"code\" in the --state file)" );
		if ( !o.quiet )
			for ( auto& l : emu::describe( o ) ) std::cout << "[emu-snippet] " << l << "\n";
		emu::snippet::result r;
		const bool ok = emu::snippet::run( s, r );
		if ( !o.quiet )
			for ( auto& l : r.log ) std::cout << "[emu-snippet] log: " << l << "\n";
		std::cout << "[emu-snippet] " << s.name << "\n";
		for ( auto& l : emu::snippet::format_result( s, r, !o.quiet ) ) std::cout << "[emu-snippet] " << l << "\n";
		if ( !ok )
		{
			std::cout << "[emu-snippet] ERROR: " << r.error << "\n";
			return 3;
		}
		const bool differ = ( r.expect_checked && !r.expect_ok ) || ( r.reference_checked && !r.reference_ok );
		std::cout << "[emu-snippet] result: " << ( differ ? "DIFF" : r.expect_checked || r.reference_checked ? "PASS" : "RAN (no checks)" ) << "\n";
		return differ ? 1 : 0;
	}

	int run_batch( const emu::run_options& o )
	{
		std::map<std::string, std::vector<uint8_t>> blobs;
		uint64_t n = 0, errors = 0, exp_n = 0, exp_diff = 0, ref_n = 0, ref_diff = 0, blob_n = 0;
		std::string err;
		const bool read = emu::read_input_lines(
			o.batch_file, max_batch_line, max_batch_file,
			[ & ]( std::string_view line, uint64_t line_no ) {
				size_t k = 0;
				while ( k < line.size() && ( line[ k ] == ' ' || line[ k ] == '\t' ) ) k++;
				if ( k == line.size() || line[ k ] == '#' ) return true;
				emu::json::value v;
				std::string e;
				const std::string where = o.batch_file + ":" + std::to_string( line_no );
				if ( !emu::json::parse( line, v, e ) )
				{
					errors++;
					std::cout << "[" << line_no << "] ERROR " << where << ": " << e << "\n";
					return true;
				}
				if ( const emu::json::value* b = v.find( "blob" ) )
				{
					const emu::json::value* h = v.find( "hex" );
					std::vector<uint8_t> bytes;
					if ( v.keys.size() != 2 || !b->is( emu::json::kind::string ) || !h || !h->is( emu::json::kind::string ) || !emu::parse_hex_bytes( h->text, bytes, e ) )
					{
						errors++;
						std::cout << "[" << line_no << "] ERROR " << where << ": a blob line is {\"blob\": NAME, \"hex\": BYTES} " << e << "\n";
						return true;
					}
					blobs[ b->text ] = bytes;
					blob_n++;
					return true;
				}
				emu::snippet::spec s;
				n++;
				if ( !emu::snippet::spec_from_json( v, s, e, &blobs ) )
				{
					errors++;
					std::cout << "[" << line_no << "] ERROR " << where << ": " << e << "\n";
					return true;
				}
				if ( s.name.empty() ) s.name = where;
				emu::snippet::result r;
				const bool ok = emu::snippet::run( s, r );
				if ( r.expect_checked ) { exp_n++; exp_diff += !r.expect_ok; }
				if ( r.reference_checked ) { ref_n++; ref_diff += !r.reference_ok; }
				const bool differ = ( r.expect_checked && !r.expect_ok ) || ( r.reference_checked && !r.reference_ok );
				if ( !ok ) errors++;
				if ( !ok || differ || !o.quiet )
				{
					std::cout << "[" << line_no << "] " << ( !ok ? "ERROR" : differ ? "DIFF" : "SAME" ) << " " << s.name << "\n";
					if ( !ok || differ )
						for ( auto& l : emu::snippet::format_result( s, r, false ) ) std::cout << "    " << l << "\n";
				}
				return true;
			},
			err );
		if ( !read )
		{
			std::cout << "[emu-snippet] error: --batch: " << err << "\n";
			return 3;
		}
		std::cout << "snippets: " << n << ", expect checked: " << exp_n << ", expect differing: " << exp_diff << ", reference checked: " << ref_n
				  << ", reference differing: " << ref_diff << ", errors: " << errors << " (blobs: " << blob_n << ")\n";
		if ( errors ) return 3;
		return exp_diff || ref_diff ? 1 : 0;
	}
}

int main( int argc, char** argv )
{
	std::vector<std::string> args( argv + 1, argv + argc );
	const auto r = emu::parse_options( emu::tool_kind::snippet, args );
	if ( !r.ok ) return usage_error( r.error );
	if ( r.options.help )
	{
		std::cout << emu::usage( emu::tool_kind::snippet );
		return 0;
	}
	if ( r.options.snippet_input == emu::snippet_input_kind::image )
		return usage_error( "--image / --rva (a PE image mapped as data) arrive with milestone M4 (plan 3.M4); M3 takes --hex / --asm / --state / --batch" );
	if ( !r.options.batch_file.empty() ) return run_batch( r.options );
	return run_one( r.options );
}
