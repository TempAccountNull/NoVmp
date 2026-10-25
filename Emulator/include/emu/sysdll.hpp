// emu::win sysdll (plan 3.M4, ledger U1981): the facts of the EMULATED Windows environment, read only from captures and
// profiles - never from the running machine (AGENT_RULES "Host safety": no host pass-through).
//
// Decision 3.0.8: the --Sys DLLs come per emulated environment (the VMware guest now; a bare-host DLL capture through a host
// package later), local only, never committed; the syscall numbers come from THAT environment's ntdll. The capture is made by
// Emulator\tools\capture_sysdlls.py (run in the environment it captures; it copies ntdll.dll and writes manifest.txt with the
// OS version facts) into <capture root>\sysdlls\<environment>\ - outside the repository. The emulator only READS that folder
// (emu::read_input_file, size-bounded) and never the running machine's System32.
//
// Environments (decision 3.0.3 / the dual-environment rule): --guest = vmware_guest, --host = bare_host (no DLL capture yet:
// refused loudly until the host package brings one), --max = none (no Windows environment: refused for a PE run).
//
// Syscall table (plan 6.4.b, "numbers discovered from the ntdll stubs, never hardcoded"; old emu_extentions\win_env.hpp
// build_syscall_table :2239-2257, PORTED with checks): every export named Nt* whose code starts with
//   4C 8B D1 B8 imm32      mov r10, rcx ; mov eax, imm32
// is a system-service stub with number imm32. Exports named Nt* that do not start so are counted (not stubs); two different
// names with one number, a number with bit 12 or above set (not the NT service table), or no stub at all is an error.
//
// Platform facts (views\environment.txt of the platform profile Emulator\data\platform\<environment>, the synthetic profile of
// plan 1.J.49): dwNumberOfProcessors, TotalPhys, wProcessorArchitecture, lpMinimum / lpMaximumApplicationAddress.
#pragma once

#include <emu/options.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace emu::win
{
	enum class environment { none, vmware_guest, bare_host };
	const char* name_of( environment e );
	environment environment_of( cpu_profile_choice p );   // guest -> vmware_guest, host -> bare_host, else none

	inline constexpr uint64_t max_ntdll_bytes = 64ull << 20;     // ntdll.dll is about 2 MiB
	inline constexpr uint64_t max_manifest_bytes = 64ull << 10;
	inline constexpr uint64_t max_platform_view_bytes = 1ull << 20;

	struct capture_manifest
	{
		std::string environment;          // "vmware_guest" / "bare_host"
		std::string captured;             // the capture date (text)
		uint32_t os_major = 0, os_minor = 0, os_build = 0;
		uint32_t product_type = 0;        // NT_PRODUCT_TYPE (shared\ntdef.h: NtProductWinNt = 1, LanManNt, Server)
		std::string system_root;          // e.g. C:\Windows (the KUSER NtSystemRoot string)
		uint64_t ntdll_size = 0;
		std::string ntdll_sha256;         // checked by the harness (win_env_t1.py); the lib checks the size
	};

	// the capture folder of ENV: EXPLICIT when given, else the first ancestor folder of DATA_DIR that holds
	// capture\sysdlls\<env>\manifest.txt
	bool locate_capture( environment env, const std::string& explicit_dir, const std::string& data_dir, std::string& dir, std::string& error );
	bool load_manifest( const std::string& dir, environment env, capture_manifest& m, std::string& error );

	struct syscall_table
	{
		std::map<uint32_t, std::string> by_number;   // service number (bits 11:0) -> Nt name
		std::map<std::string, uint32_t> by_name;
		uint64_t nt_exports = 0;                     // exports named Nt*
		uint64_t stubs = 0;                          // of them: system-service stubs
	};
	bool parse_syscall_table( const std::vector<uint8_t>& ntdll_file, syscall_table& t, std::string& error );

	struct platform_facts
	{
		uint32_t processors = 0;                // dwNumberOfProcessors
		uint64_t total_phys = 0;                // TotalPhys (bytes)
		uint32_t processor_architecture = 0;    // wProcessorArchitecture (winnt.h PROCESSOR_ARCHITECTURE_AMD64 = 9)
		uint64_t min_app_address = 0, max_app_address = 0;
		std::string source;                     // the file read
	};
	bool load_platform_facts( environment env, const std::string& data_dir, platform_facts& p, std::string& error );
}
