// emu-oep (plan 3.M1 / 3.A 2.2, ledger U1940; plan 3.M4, ledger U1984): runs a 64-bit PE purely in emulation inside the
// minimum Windows environment of the Emulator lib (emu::win: image mapped as data, PEB / TEB / KUSER from the profile and the
// environment's capture, GS = TEB, SYSCALL handled by the emulated kernel layer). The goal - a packed PE to its OEP - is plan
// 5.C; bounded pure-emulation runs of the sample start with milestone M5 (decision 3.0.4). Plan 3.M5 (U2013): the kernel layer of
// emu::nt answers the services of M5, --HLE binds imports to the API stubs of emu::hle and runs TLS callbacks; --Sys refuses images
// with imports / TLS (the real LdrInitializeThunk start is not modelled, notes\eread\P35_phase3_m5.md). INPUT is only ever read as a file
// and written into guest memory: never loaded, mapped executable or run on the host, never under a debugger (AGENT_RULES
// "Host safety"). Nothing is written or created (--out-dir is recorded only).
//
//   emu-oep [--host | --guest | --max] [--Sys | --HLE] [--seeded | --HostSeed] [--max-insns N] [--image-base ADDR]
//           [--sysdlls DIR] [--dry-run] [--out-dir DIR] INPUT
//   emu-oep (--guest | --host) [--sysdlls DIR] --syscall-number NAME
//   exit 0 = the guest terminated through NtTerminateProcess, 1 = another stop (fault, instruction bound, return from the
//   entry), 2 = usage, 3 = set-up error (input, capture, refused feature), 4 = unknown / unimplemented system service or --HLE
//   API (loud stop with a state dump)
#include <emu/engine.hpp>
#include <emu/input_file.hpp>
#include <emu/options.hpp>
#include <emu/profile.hpp>
#include <emu/win_env.hpp>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	std::string hex( uint64_t v )
	{
		char b[ 32 ];
		std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
		return b;
	}
}

int main( int argc, char** argv )
{
	std::vector<std::string> args( argv + 1, argv + argc );
	const auto r = emu::parse_options( emu::tool_kind::oep, args );
	if ( !r.ok )
	{
		std::cout << "[emu-oep] error: " << r.error << "\n" << emu::usage( emu::tool_kind::oep );
		return 2;
	}
	const auto& o = r.options;
	if ( o.help )
	{
		std::cout << emu::usage( emu::tool_kind::oep );
		return 0;
	}
	for ( auto& l : emu::describe( o ) ) std::cout << "[emu-oep] " << l << "\n";
	if ( o.dry_run )
	{
		std::cout << "[emu-oep] --dry-run: options parsed; nothing opened, nothing created, nothing emulated\n";
		return 0;
	}

	// the environment of the CPU profile (decision 3.0.8): its DLL capture and the platform profile
	std::string data_dir, error;
	if ( !emu::default_data_dir( data_dir, error ) )
	{
		std::cout << "[emu-oep] error: " << error << "\n";
		return 3;
	}
	const auto env = emu::win::environment_of( o.profile );
	emu::win::environment_facts facts;
	std::vector<std::string> flog;
	const bool facts_ok = emu::win::load_environment_facts( env, o.sysdlls_dir, data_dir, facts, flog, error );
	for ( auto& l : flog ) std::cout << "[emu-oep] " << l << "\n";
	if ( !facts_ok )
	{
		std::cout << "[emu-oep] error: " << error << "\n";
		return 3;
	}
	if ( !o.syscall_query.empty() )
	{
		const auto it = facts.syscalls.by_name.find( o.syscall_query );
		if ( it == facts.syscalls.by_name.end() )
		{
			std::cout << "[emu-oep] error: " << o.syscall_query << " is not a system-service stub of the environment's ntdll\n";
			return 3;
		}
		std::cout << "[emu-oep] syscall " << o.syscall_query << " = " << hex( it->second ) << " (" << it->second << ") from the stub in "
				  << facts.capture_dir << "\\ntdll.dll\n";
		return 0;
	}

	// the image: read as data (bounded), never loaded or executed on the host
	std::string bytes;
	if ( !emu::read_input_file( o.input_path, emu::pe::max_image_file_bytes, bytes, error ) )
	{
		std::cout << "[emu-oep] error: " << error << "\n";
		return 3;
	}
	const std::vector<uint8_t> file( bytes.begin(), bytes.end() );
	std::cout << "[emu-oep] input read as data: " << file.size() << " bytes\n";

	emu::engine_config ec = emu::config_from( o );
	ec.data_dir = data_dir;
	emu::win::process_options po;
	po.boundary = o.boundary;
	po.image_base_given = o.image_base_given;
	po.image_base = o.image_base;
	if ( o.max_insns_given ) po.max_insns = o.max_insns;
	po.image_name = "image";
	std::vector<std::string> plog;
	auto p = emu::win::process::create( ec, facts, po, file, plog, error );
	for ( auto& l : plog ) std::cout << "[emu-oep] " << l << "\n";
	if ( !p )
	{
		std::cout << "[emu-oep] error: " << error << "\n";
		return 3;
	}
	emu::win::run_result rr;
	const size_t before = p->log().size();
	const bool ran = p->run( rr, error );
	for ( size_t i = before; i < p->log().size(); i++ ) std::cout << "[emu-oep] " << p->log()[ i ] << "\n";
	if ( !ran )
	{
		std::cout << "[emu-oep] error: " << error << "\n";
		return 3;
	}
	for ( auto& l : p->observable( rr ) ) std::cout << "[emu-oep] observable: " << l << "\n";
	const bool loud = rr.stop == emu::win::stop_kind::unknown_syscall || rr.stop == emu::win::stop_kind::unimplemented_syscall ||
					  rr.stop == emu::win::stop_kind::unimplemented_api;   // U2013 (plan 3.M5)
	if ( rr.stop != emu::win::stop_kind::terminated )
		for ( auto& l : p->state_dump( rr ) ) std::cout << "[emu-oep] " << l << "\n";
	if ( rr.stop == emu::win::stop_kind::terminated )
	{
		char b[ 128 ];
		std::snprintf( b, sizeof( b ), "guest exit status 0x%08x; the emu-oep process continues and exits 0", rr.exit_status );
		std::cout << "[emu-oep] result: " << b << "\n";
		return 0;
	}
	std::cout << "[emu-oep] result: " << emu::win::name_of( rr.stop ) << ( loud ? " (loud stop, exit 4)" : " (exit 1)" ) << "\n";
	return loud ? 4 : 1;
}
