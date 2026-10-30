// emu::engine (plan 3.M2, ledger U1952): the Emulator lib's engine core - one Unicorn x86-64 engine of the fork, configured
// the way emu-oep / emu-snippet will use it (milestones M3-M5 build on it).
//
// open( config ) does, in this order, and logs every step (verbose log, emu::engine::log):
//   1. the CPU profile of decision 3.0.3: --host (DEFAULT) = the captured bare-host CPUID profile, --guest = the captured
//      VMware-guest profile, --max = Unicorn's UC_CPU_X86_MAX model without a profile; --cpuid-runtime is refused (M2 has
//      no host-CPU path; it needs an explicit allow-listed one, decision 3.0.3) - see emu/profile.hpp
//   2. uc_open( UC_ARCH_X86, UC_MODE_64 ), CPU model UC_CPU_X86_MAX (as emu-alltest)
//   3. the CPUID profile installed AT uc_open (plan 1.J.47.a, fork U1580): before any memory map or register access, so the
//      engine starts with the profile's OS-environment values (fork U1530 / U1531: reset XCR0 from the profile's leaf 0DH);
//      strict is the fork's default with a profile (U435: features the profile hides are #UD); the profile's microcode line
//      (U1473) - both only possible before the engine initialises
//   4. RDRAND / RDSEED source (decision D8, fork U835): --seeded (DEFAULT; deterministic, seed config.rng_seed: since U2091 the
//      fork's CTR_DRBG of NIST SP 800-90A, AES-128) or --HostSeed (the host DRNG, executed by the fork)
//   4b. U2092 (plan 3.M6): the virtual time base (fork U2090 UC_CTL_X86_VCLOCK virtual, start config.tsc_start, rate
//      config.tsc_per_insn ticks per instruction) - RDTSC / RDTSCP / the TSC MSRs never read the host clock
//   5. the reset machine state (RIP, RSP, RFLAGS, CR0, CR4, XCR0, strict, profile rows, XINUSE model) read back and logged
// U1973 (plan 3.M3): the settings emu-alltest applies to its Unicorn engine, in emu-alltest's order (at_engine.hpp
// open_configured): CPU model, the opt-ins (AVX-512, AMX, AVX10, APX, platform features), the RNG source, the CPUID profile,
// strict, microcode, then (the engine initialises) SMM revision, IA32_FEATURE_CONTROL, XCR0, CR0, XINUSE model - so emu-snippet
// replays an emu-alltest case on the same machine without a second engine set-up path.
// After open, install_cpuid_profile() passes a later profile change to the fork: accepted before the first instruction,
// UC_ERR_ARG once guest code ran (a strict profile is part of the machine the guest starts on, 1.J.47.a).
//
// Memory map: map / unmap whole page-aligned regions (4 KiB), named, bounds-checked with 64-bit arithmetic (no overflow, no
// overlap, total at most config.max_mapped_bytes); read / write only inside mapped regions, at most max_io_bytes per call.
// regions() is the engine's own table; check_regions() compares it with uc_mem_regions. U2350 (P54, user decision 2026-10-29):
// regions() returns the table by const reference (no copy per call - the traced run called it once per instruction through
// process::where(), P53 cost #1); a caller that maps / unmaps / protects while it needs the table keeps its own copy (auto x =).
// emulate( begin, until, count ) = uc_emu_start without a timeout: Unicorn's timeout is host time, and the host-safety rule
// allows time only from the one virtual time base (milestone M6) - bounds come from the instruction count.
// Host safety (AGENT_RULES "Host safety"): guest bytes only ever reach Unicorn's guest memory (uc_mem_write); nothing here
// executes host code made from guest bytes, calls the host kernel or reads host time; the engine is closed (uc_close) in the
// destructor, which also frees Unicorn's translation buffer.
#pragma once

#include <emu/options.hpp>
#include <emu/profile.hpp>

#include <unicorn/unicorn.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace emu
{
	inline constexpr uint64_t page_size = 0x1000;
	inline constexpr uint64_t max_io_bytes = 64ull << 20;   // one read / write call

	struct engine_config
	{
		cpu_profile_choice profile = cpu_profile_choice::host;   // decision 3.0.3: --host is the default
		rng_source rng = rng_source::seeded;                     // decision D8: --seeded is the default
		uint64_t rng_seed = 0;                                   // the seeded model's seed (fork U835 default 0)
		std::string data_dir;                                    // "" = default_data_dir() (Emulator\data of this build)
		uint64_t max_mapped_bytes = 1ull << 30;                  // 1 GiB of guest memory at most
		bool echo_log = false;                                   // also print every log line to stdout as it is written
		// U1973 (plan 3.M3): the machine settings emu-snippet needs to replay an emu-alltest run (each one is an emu-alltest
		// option with the same fork control; 0 / -1 / "" = not written, the fork's default):
		int cpu_model = UC_CPU_X86_MAX;                          // the built-in model (emu-alltest: always UC_CPU_X86_MAX)
		std::string cpuid_file;                                  // an explicit CPUID profile file (emu-alltest --cpuid / --host-core E):
																 // overrides the profile choice's file; with --max it adds a profile
		int strict = -1;                                         // UC_CTL_X86_CPUID_STRICT: -1 = the fork's default (strict with a profile)
		int avx512 = 0, amx = 0, avx10 = 0, apx = 0;             // UC_CTL_X86_AVX512 / AMX / AVX10 / APX opt-ins (before the engine initialises)
		int x86_features = 0;                                    // UC_CTL_X86_FEATURES (U1181)
		uint32_t smm_revision = 0;                               // UC_CTL_X86_SMM_REVISION (U1465)
		uint64_t feature_control = 0;                            // IA32_FEATURE_CONTROL (MSR 3AH) at the start (U1661)
		uint64_t xcr0 = 0;                                       // XCR0 at the start (uc_reg_write)
		uint64_t cr0 = 0;                                        // CR0 at the start (uc_reg_write; U1962: refused like MOV to CR0)
		int xinuse_model = 0;                                    // UC_CTL_X86_XINUSE_MODEL (U1481)
		// U2092 (plan 3.M6): the one virtual time base - every engine runs the fork's TSC in UC_X86_VCLOCK_VIRTUAL mode (never the
		// host clock): the TSC at the first instruction is tsc_start + tsc_per_insn (emu-snippet: the value emu-alltest exported;
		// win_env restarts it at the process start, emu/vclock.hpp)
		uint64_t tsc_start = 0;
		uint64_t tsc_per_insn = 1;                               // emu::vclock::ticks_per_insn
	};

	// the engine settings of a parsed command line (profile, rng); the rest keeps the defaults
	engine_config config_from( const run_options& o );

	struct mapped_region
	{
		uint64_t base = 0;
		uint64_t size = 0;
		uint32_t prot = 0;   // UC_PROT_*
		std::string name;
	};

	class engine
	{
	public:
		static std::unique_ptr<engine> open( const engine_config& cfg, std::string& error );
		~engine();
		engine( const engine& ) = delete;
		engine& operator=( const engine& ) = delete;

		uc_engine* handle() const { return uc_; }
		const engine_config& config() const { return cfg_; }
		const cpuid_profile& profile() const { return profile_; }   // no rows for --max
		bool has_profile() const { return !profile_.leaves.empty(); }

		// a CPUID profile change after open (1.J.47.a): the fork's answer is returned and logged (UC_ERR_ARG for a strict
		// profile once guest code ran)
		uc_err install_cpuid_profile( const std::vector<uc_x86_cpuid>& leaves );

		// U2092: BACKING (optional) = an emulator-owned host buffer of SIZE bytes (readable / writable, never executable) that holds
		// the region's bytes (uc_mem_map_ptr): the environment updates it directly (KUSER_SHARED_DATA's time fields at a guest read);
		// the buffer must outlive the engine
		bool map( uint64_t base, uint64_t size, uint32_t prot, const std::string& name, std::string& error, void* backing = nullptr );
		bool unmap( uint64_t base, uint64_t size, std::string& error );   // exactly one mapped region
		// U2006 (plan 3.M5): the emulated kernel's virtual-memory services work on parts of regions. protect / unmap_range need
		// every page of [base, +size) mapped (page aligned); the region table is split the way uc_mem_protect / uc_mem_unmap split
		// Unicorn's (check_regions stays exact). accessible = every byte of [address, +length) mapped with all NEED bits (the
		// probe of a guest pointer; length 0 = true).
		bool protect( uint64_t base, uint64_t size, uint32_t prot, std::string& error );
		bool unmap_range( uint64_t base, uint64_t size, std::string& error );
		bool accessible( uint64_t address, uint64_t length, uint32_t need ) const;
		uint64_t mapped_bytes() const { return mapped_bytes_; }
		const std::vector<mapped_region>& regions() const { return regions_; }   // U2350: by reference (valid until the next map / unmap / protect)
		bool check_regions( std::string& error ) const;
		bool read( uint64_t address, void* out, uint64_t length, std::string& error ) const;
		bool write( uint64_t address, const void* in, uint64_t length, std::string& error );
		std::vector<std::string> describe_memory_map() const;

		uc_err emulate( uint64_t begin, uint64_t until, uint64_t count );
		bool code_ran() const { return ran_; }

		const std::vector<std::string>& log() const { return log_; }
		void log_line( const std::string& line );

	private:
		engine() = default;
		bool inside_mapped( uint64_t address, uint64_t length, std::string& error ) const;
		bool page_range( uint64_t base, uint64_t size, const char* what, std::string& error ) const;   // U2006

		uc_engine* uc_ = nullptr;
		engine_config cfg_;
		cpuid_profile profile_;
		std::vector<mapped_region> regions_;   // sorted by base, non-overlapping
		uint64_t mapped_bytes_ = 0;
		bool ran_ = false;
		std::vector<std::string> log_;
	};
}
