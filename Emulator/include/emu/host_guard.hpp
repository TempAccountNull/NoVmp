// emu::host_guard (plan 3.M1, ledger U1941): the RUNTIME half of the no-native-execution check (suite no_native_check,
// AGENT_RULES "Host safety"). It only reads the own process's memory map (VirtualQuery) and the own process's readable
// memory; it never changes a protection, allocates executable memory or calls into the host kernel beyond VirtualQuery.
//
//   committed_regions()        every committed region of the own process (64-bit base / size, protection, type)
//   foreign_executable()       committed executable regions that are NOT part of a mapped image (MEM_PRIVATE / MEM_MAPPED):
//                              where native code that is not ours (shellcode, an RWX heap, a JIT buffer) would live
//   find_pattern( p, n, exec ) addresses of a byte pattern in committed readable memory, executable regions only (exec =
//                              true: "do guest bytes sit in host-executable memory?") or every readable region (exec = false,
//                              the positive control: the scanner must find the pattern where it is)
//
// M1 expectation (emu-selftest case no_native_runtime): no foreign executable region at all, and a guest-code marker held
// in a buffer is found in readable memory but never in executable memory. From milestone M2 on the engine's own TCG code
// buffer (Unicorn's translator output, never guest bytes) is a foreign executable region the case must name explicitly.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace emu::host_guard
{
	struct region
	{
		uint64_t base = 0;
		uint64_t size = 0;
		uint32_t protect = 0;    // PAGE_* of the region
		uint32_t type = 0;       // MEM_IMAGE / MEM_MAPPED / MEM_PRIVATE
	};

	std::vector<region> committed_regions();
	std::vector<region> foreign_executable();
	bool is_executable( uint32_t protect );
	bool is_readable( uint32_t protect );   // readable without a fault: not NOACCESS, not GUARD
	std::vector<uint64_t> find_pattern( const uint8_t* pattern, size_t length, bool executable_only );
	std::string describe( const region& r );
}
