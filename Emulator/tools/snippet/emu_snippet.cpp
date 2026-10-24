// emu-snippet (plan 3.M1 / 3.A 2.7, ledger U1940): emulates isolated pieces of code on the same Emulator lib as emu-oep.
// Milestone M1 is the skeleton: it parses and prints the options (emu::parse_options) and stops - the executor, the
// per-instruction trace and the --expect checks arrive with milestone M3. It opens nothing (not --image, not --out-dir)
// and emulates nothing; a snippet's bytes are never executed natively (AGENT_RULES "Host safety").
//
//   emu-snippet (--hex BYTES | --asm TEXT | --image PATH --rva RVA) [--stop-at ADDR] [--stop-ret-depth N]
//               [--expect REG=VALUE ...] [--host | --guest | --max | --cpuid-runtime] [--Sys | --HLE]
//               [--seeded | --HostSeed] [--max-insns N] [--out-dir DIR]
//   exit 0 = options valid (printed), 2 = usage error
#include <emu/options.hpp>

#include <iostream>
#include <string>
#include <vector>

int main( int argc, char** argv )
{
	std::vector<std::string> args( argv + 1, argv + argc );
	const auto r = emu::parse_options( emu::tool_kind::snippet, args );
	if ( !r.ok )
	{
		std::cout << "[emu-snippet] error: " << r.error << "\n" << emu::usage( emu::tool_kind::snippet );
		return 2;
	}
	if ( r.options.help )
	{
		std::cout << emu::usage( emu::tool_kind::snippet );
		return 0;
	}
	for ( auto& l : emu::describe( r.options ) ) std::cout << "[emu-snippet] " << l << "\n";
	std::cout << "[emu-snippet] M1 skeleton: options parsed; nothing opened, nothing created, nothing emulated "
				 "(the executor arrives with milestone M3)\n";
	return 0;
}
