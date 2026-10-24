// emu::deps (plan 3.M1, ledger U1940): see include/emu/deps.hpp. Header macros only; no Unicorn / Capstone / Keystone call.
#include <emu/deps.hpp>

namespace emu
{
	std::string deps_summary()
	{
		static_assert( sizeof( win::image_t<true> ) > 0, "linux-pe PE32+ image type" );
		return "unicorn " + std::to_string( UC_API_MAJOR ) + "." + std::to_string( UC_API_MINOR ) + " (headers), capstone " +
			   std::to_string( CS_API_MAJOR ) + "." + std::to_string( CS_API_MINOR ) + " (headers), keystone " +
			   std::to_string( KS_API_MAJOR ) + "." + std::to_string( KS_API_MINOR ) + " (headers), linux-pe (headers)";
	}
}
