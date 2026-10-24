// emu::deps (plan 3.M1 / 3.A 2.2, ledger U1940): the Emulator lib builds against the fork's Unicorn, Capstone 6, Keystone and
// linux-pe (deps pristine, referenced by path from the solution; no CMake). M1 only includes the headers - the compile-time
// proof that the include paths and versions are the ones Phase 1 verified; nothing is called or linked from them yet (the
// engine arrives with milestone M2).
#pragma once

#include <unicorn/unicorn.h>
#include <capstone/capstone.h>
#include <keystone/keystone.h>
#include <linuxpe>

#include <string>

static_assert( UC_API_MAJOR == 2, "the Emulator lib expects the fork's Unicorn 2 API (plan 1.F, D9)" );
static_assert( CS_API_MAJOR == 6, "the Emulator lib expects Capstone 6 (plan 2.3)" );
static_assert( KS_API_MAJOR == 0, "the Emulator lib expects Keystone 0.9.x" );

namespace emu
{
	// "unicorn 2.1 / capstone 6.x / keystone 0.9 / linux-pe" from the headers (no library call)
	std::string deps_summary();
}
