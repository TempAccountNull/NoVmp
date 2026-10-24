// emu::options (plan 3.M1 / 3.A 2.2, ledger U1940): the command line of the Phase 3 tools emu-oep and emu-snippet,
// parsed once in the Emulator lib so both tools (and emu-selftest) share one parser.
//
// Decisions (PLAN_emulation.md 3.0.x, your answers 2026-10-24 to notes/phase3_start_plan_2026-10-24.md section 6):
//   3.0.3  CPU profile: --host (the captured bare-host profile) is the DEFAULT, --guest (the captured VMware guest profile)
//          optional, --max for coverage; the running machine's CPUID only as the explicit option --cpuid-runtime (supersedes D4).
//   D3     emulation boundary: --Sys (default; real ntdll / kernelbase / kernel32 code, emulated at the syscall boundary) or
//          --HLE (API-level reimplementation).
//   D8     RDRAND / RDSEED source: --seeded (default, deterministic model) or --HostSeed (hardware DRNG).
//   3.0.4  sample runs are bounded (--max-insns) and pure emulation only, from milestone M5 on.
// M1 is "options only": nothing here (or in the tools) opens a file, creates a directory or emulates anything. Every number
// is 64-bit (host-safety rule, AGENT_RULES "Host safety"): parse_u64 refuses overflow, signs and garbage instead of truncating.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace emu
{
	enum class tool_kind { oep, snippet };
	enum class cpu_profile_choice { host, guest, max, runtime_cpuid };
	enum class env_boundary { sys, hle };
	enum class rng_source { seeded, host_seed };
	enum class snippet_input_kind { none, hex, asm_text, image };

	// --expect REG=VALUE (emu-snippet; checked from milestone M3 on, M1 only validates the syntax)
	struct expect_item
	{
		std::string reg;
		uint64_t value = 0;
	};

	struct run_options
	{
		tool_kind tool = tool_kind::oep;

		cpu_profile_choice profile = cpu_profile_choice::host;
		bool profile_given = false;
		env_boundary boundary = env_boundary::sys;
		bool boundary_given = false;
		rng_source rng = rng_source::seeded;
		bool rng_given = false;

		uint64_t max_insns = 0;          // 0 = not given
		bool max_insns_given = false;
		std::string out_dir;             // recorded only (M1 creates nothing)

		// emu-oep: the input image (positional); emu-snippet: --image. Recorded only: M1 never opens it.
		std::string input_path;

		// emu-snippet
		snippet_input_kind snippet_input = snippet_input_kind::none;
		std::vector<uint8_t> snippet_bytes;  // --hex
		std::string snippet_asm;             // --asm (assembled with Keystone from milestone M3 on)
		uint64_t image_rva = 0;              // --rva (with --image)
		bool image_rva_given = false;
		uint64_t stop_at = 0;                // --stop-at ADDRESS
		bool stop_at_given = false;
		uint64_t stop_ret_depth = 0;         // --stop-ret-depth N
		bool stop_ret_depth_given = false;
		std::vector<expect_item> expects;    // --expect REG=VALUE (repeatable)

		bool help = false;
	};

	struct parse_result
	{
		bool ok = false;
		std::string error;               // set when ok == false
		run_options options;
	};

	// Upper bound of --hex input (bytes); larger snippets come from --image (M3).
	inline constexpr size_t max_hex_bytes = 1u << 20;

	parse_result parse_options( tool_kind tool, const std::vector<std::string>& args );

	// decimal or 0x-prefixed hexadecimal, the full 64-bit range; false on an empty string, a sign, a non-digit or overflow
	bool parse_u64( std::string_view text, uint64_t& out );

	// "48 31 c0 c3", "4831c0c3", "48,31,C0,C3": pairs of hex digits, separators ' ' ',' '\t'; false (with a reason) on an odd
	// digit count, a non-hex character, an empty input or more than max_hex_bytes
	bool parse_hex_bytes( std::string_view text, std::vector<uint8_t>& out, std::string& error );

	const char* name_of( tool_kind v );
	const char* name_of( cpu_profile_choice v );
	const char* name_of( env_boundary v );
	const char* name_of( rng_source v );
	const char* name_of( snippet_input_kind v );

	// the verbose configuration lines the tools print before anything else ("key: value")
	std::vector<std::string> describe( const run_options& o );

	std::string usage( tool_kind tool );

	// "M1": the Phase 3 milestone this lib implements (the tools print it)
	const char* milestone();
}
