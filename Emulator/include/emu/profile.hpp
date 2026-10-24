// emu::profile (plan 3.M2, ledger U1951): the captured CPUID profiles the engine installs at uc_open (decision 3.0.3).
//
//   --host  (default)  Emulator\data\cpuid_i5-13600k_bare_host.txt   the bare-host capture (physical i5-13600K, P-core LP 0)
//   --guest            Emulator\data\cpuid_i5-13600k.txt             the VMware-guest capture
//   --max              no file: Unicorn's built-in UC_CPU_X86_MAX model, no profile
//   --cpuid-runtime    the running machine's CPUID: refused in M2 (needs an explicit, allow-listed host-CPU path)
//
// File format (the one emu-alltest reads, Emulator\tests\alltest\at_cases.hpp load_cpuid_profile / load_cpuid_microcode):
// one row "LEAF SUBLEAF EAX EBX ECX EDX" per line, hexadecimal without prefix; '#' lines are comments; one optional line
// "#@ microcode REV platform-id N" (REV hex, N 0..7, U1473). This parser is STRICT where emu-alltest skips silently: a row
// that is not exactly six hex fields of at most 8 digits, a repeated (leaf, sub-leaf), a malformed or repeated microcode line,
// more than max_profile_rows rows or a file above max_profile_bytes is an error with the line number (host-safety rule,
// AGENT_RULES "Host safety": every input is bounded and fails loudly). The file is only read: nothing is executed or written.
// No CPUID value is ever printed by this code (the profiles are real captures; logs show counts and file names only).
#pragma once

#include <emu/options.hpp>

#include <unicorn/unicorn.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace emu
{
	inline constexpr uint64_t max_profile_bytes = 1u << 20;   // 1 MiB; the captured profiles are below 9 KiB
	inline constexpr size_t max_profile_rows = 4096;

	struct cpuid_profile
	{
		std::vector<uc_x86_cpuid> leaves;   // file order
		bool has_microcode = false;         // "#@ microcode REV platform-id N" present
		uint32_t microcode_rev = 0;
		int microcode_platform_id = 0;
		std::string path;                   // the file read ("" for --max)
		uint64_t file_bytes = 0;
	};

	// parse the text of a profile file (see above); false with a reason ("line N: ...") on the first problem
	bool parse_cpuid_profile( std::string_view text, cpuid_profile& out, std::string& error );

	// read PATH (at most max_profile_bytes) and parse it; out.path / out.file_bytes are set
	bool load_cpuid_profile_file( const std::string& path, cpuid_profile& out, std::string& error );

	// the profile file of a choice (file name only), "" for --max / --cpuid-runtime
	const char* profile_file_name( cpu_profile_choice choice );

	// what the choice is, for the verbose log (no value)
	const char* profile_description( cpu_profile_choice choice );

	// Emulator\data of the repository the running executable belongs to: the executables live in build\x64\<cfg>\tests
	// (emu-oep, emu-snippet, emu-selftest, like emu-alltest), so the folder is <exe dir>\..\..\..\..\Emulator\data;
	// false when it does not exist
	bool default_data_dir( std::string& out, std::string& error );
}
