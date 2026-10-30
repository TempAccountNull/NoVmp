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

	// P57 (U2374): the PER-LOGICAL-PROCESSOR CPUID table of a profile - Emulator\data\cpuid_lp_<profile name without "cpuid_">, written by
	// Emulator\tools\gen_cpuid_lp.py from the capture's raw per-LP rows (each logical processor pinned). Lines: "start N" (the LP the
	// profile's rows come from), "lp N apic X" (one per LP, N = 0, 1, ... in order), "row N LEAF SUB EAX EBX ECX EDX" (the profile row as
	// LP N answers it, only where it differs from the start LP outside the APIC fields); '#' lines are comments. Strict like the profile
	// parser (bounded, the line number in the error). SDM 253665-093 Vol1 Chapter 21: 01H EBX[31:24] Initial APIC ID (pp. 21-9 / 21-10),
	// 0BH / 1FH EDX x2APIC ID (pp. 21-39 / 21-71), the Domain "Logical Processor" / "Core" fields (04H p. 21-21, 18H p. 21-63, 1AH p. 21-66).
	struct cpuid_lp_row
	{
		uint32_t leaf = 0, subleaf = 0;
		uint32_t r[ 4 ] = {};   // EAX, EBX, ECX, EDX
	};
	struct cpuid_lp_table
	{
		bool loaded = false;
		unsigned start = 0;
		std::vector<uint32_t> apic;                    // by LP number: its APIC ID (01H EBX[31:24] = the low 8 bits, 0BH / 1FH EDX = all 32)
		std::vector<std::vector<cpuid_lp_row>> rows;   // by LP number
		std::string path;
		uint64_t file_bytes = 0;
	};
	bool parse_cpuid_lp_table( std::string_view text, cpuid_lp_table& out, std::string& error );
	bool load_cpuid_lp_file( const std::string& path, cpuid_lp_table& out, std::string& error );
	// the per-LP file of a choice ("cpuid_lp_i5-13600k_bare_host.txt" ...), "" for --max / --cpuid-runtime
	std::string cpuid_lp_file_name( cpu_profile_choice choice );

	// Emulator\data of the repository the running executable belongs to: the executables live in build\x64\<cfg>\tests
	// (emu-oep, emu-snippet, emu-selftest, like emu-alltest), so the folder is <exe dir>\..\..\..\..\Emulator\data;
	// false when it does not exist
	bool default_data_dir( std::string& out, std::string& error );
}
