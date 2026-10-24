// emu-oep (plan 3.M1 / 3.A 2.2, ledger U1940): runs a packed PE to its OEP purely in emulation (plan 5.C). Milestone M1 is
// the skeleton: it parses and prints the options (emu::parse_options, Emulator lib) and stops. It opens nothing - not the
// INPUT, not --out-dir - and emulates nothing. Bounded pure-emulation runs of the sample arrive with milestone M5
// (decision 3.0.4); the sample never runs natively or under a debugger (AGENT_RULES "Host safety").
//
//   emu-oep [--host | --guest | --max | --cpuid-runtime] [--Sys | --HLE] [--seeded | --HostSeed] [--max-insns N]
//           [--out-dir DIR] INPUT
//   exit 0 = options valid (printed), 2 = usage error
#include <emu/options.hpp>

#include <iostream>
#include <string>
#include <vector>

int main( int argc, char** argv )
{
	std::vector<std::string> args( argv + 1, argv + argc );
	const auto r = emu::parse_options( emu::tool_kind::oep, args );
	if ( !r.ok )
	{
		std::cout << "[emu-oep] error: " << r.error << "\n" << emu::usage( emu::tool_kind::oep );
		return 2;
	}
	if ( r.options.help )
	{
		std::cout << emu::usage( emu::tool_kind::oep );
		return 0;
	}
	for ( auto& l : emu::describe( r.options ) ) std::cout << "[emu-oep] " << l << "\n";
	std::cout << "[emu-oep] M1 skeleton: options parsed; nothing opened, nothing created, nothing emulated "
				 "(PE mapping arrives with milestone M4, bounded sample runs with M5)\n";
	return 0;
}
