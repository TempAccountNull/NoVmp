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

	// --expect REG=VALUE (emu-snippet; checked since milestone M3; value = the number when VALUE is one, else 0: byte-field values
	// are hex bytes, kept in run_options::expect_raw)
	struct expect_item
	{
		std::string reg;
		uint64_t value = 0;
	};

	constexpr uint64_t max_trace_last = 4000000;   // U2040: --trace-last N bound (the ring is held in memory)

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

		// U1975 (plan 3.M3, emu-snippet v1): the executor's options; values are kept as given and parsed by emu::snippet
		// (register names / sizes: emu/snippet.hpp)
		std::vector<std::string> expect_raw;   // every --expect as given (REG=VALUE; VALUE a number or hex bytes)
		std::vector<std::string> sets;         // --set REG=VALUE
		std::vector<std::string> mems;         // --mem ADDR=HEXBYTES
		std::vector<std::string> maps;         // --map ADDR:SIZE[:PROT]
		std::vector<std::string> expect_mems;  // --expect-mem ADDR=HEXBYTES
		std::string expect_fault;              // --expect-fault none | #UD | #GP(0) | N
		bool expect_strict = false;            // --expect-strict
		std::string state_file;                // --state FILE.json (read only, through emu::read_input_file)
		std::string batch_file;                // --batch FILE.jsonl
		bool trace = false;                    // --trace
		bool quiet = false;                    // --quiet: verdict lines only
		std::string syscall;                   // --syscall stop | cpu ("" = stop)
		std::string external;                  // --external stop | fault ("" = stop)
		uint64_t code_address = 0;             // --at ADDR (default 140001000H)
		bool code_address_given = false;
		std::vector<std::string> dump_mems;    // --dump-mem ADDR:LEN
		int avx512 = 0, amx = 0, avx10 = 0, apx = 0;   // --avx512 / --amx / --avx10 N / --apx (engine opt-ins, as emu-alltest)

		// U1984 (plan 3.M4): emu-oep runs a PE in the win_env environment
		bool dry_run = false;                  // --dry-run: print the configuration and stop (the M1 behaviour: nothing opened)
		uint64_t image_base = 0;               // --image-base ADDR: map the image here (relocated) instead of its ImageBase
		bool image_base_given = false;
		std::string sysdlls_dir;               // --sysdlls DIR: the environment's DLL capture folder (decision 3.0.8; default: found
		                                       // under the capture root, see emu/sysdll.hpp)
		std::string syscall_query;             // --syscall-number NAME: print NAME's service number from the environment's ntdll
		// U2020 (plan 3.M5 part 2): instrumentation of an emu-oep run, off by default
		bool counters = false;                 // --counters: probe counters (CPUID, RDTSC / RDTSCP / RDPID, PEB / TEB / KUSER reads,
		                                       // system calls, API calls, exceptions) + the last instructions, summary at the stop
		std::string trace_file;                // --trace FILE: one line per instruction into FILE (created; at most 1 GiB)
		// U2040 (the #GP investigation, notes/eread/P37_gp_investigation.md): register-level trace lines, both need --trace
		bool trace_regs = false;               // --trace-regs: + RAX..R15 / RFLAGS before the instruction + its memory reads / writes
		uint64_t trace_last = 0;               // --trace-last N: keep only the last N lines (written at the stop); 0 = every line
		bool trace_last_given = false;
		bool loader = false;                   // U2031: --loader (with --Sys): start in the real ntdll!LdrInitializeThunk (opt-in)
		// P57 (U2373, plan 5.C 8.4 / 8.5): the OEP stop and the dump at the stop (observation only; emu-oep also takes --stop-at ADDR)
		bool stop_written_exec = false;        // --stop-at-written-exec: stop before the first instruction on a page the guest wrote
		std::string dump_dir;                  // --dump-dir DIR (an existing folder): at the stop, the emulated memory as data files

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

	// the Phase 3 milestone this lib implements (the tools print it): "M4" = emu-oep runs a PE in the minimum Windows environment
	// (emu::win, emu::pe, U1981-U1984; M3: emu-snippet v1, U1972-U1975; M2: emu::engine, emu::reg, emu::profile, U1951-U1954)
	const char* milestone();
}
