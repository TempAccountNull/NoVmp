// emu::win (plan 3.M4 = 3.B 6.1 / 6.2 / 6.6 part, ledger U1983): the minimum Windows x64 user environment for ONE PE image,
// built entirely in the engine's guest memory from the captures and profiles - nothing is read from or forwarded to the host
// (AGENT_RULES "Host safety"; decision 3.0.1: ported from emu_extentions\win_env.hpp only where it fits the rules, the rest
// rewritten - see notes\eread\P34_phase3_m4.md).
//
// What a run (process::create + process::run) builds:
//   image     the PE mapped AS DATA (emu::pe::map): headers R, sections with their protections, relocations when not at the
//             preferred base (--image-base). Plan 3.M5 (U2012): under --HLE the imports are bound to the API stubs of emu::hle
//             and a TLS directory gets the loader's TLS start (index 0, the thread's TLS block from the template, the callbacks
//             with DLL_PROCESS_ATTACH before the entry); under --Sys an image with imports or TLS is REFUSED: the real
//             ntdll!LdrInitializeThunk would need the kernel's process-creation state, which no cited source documents (P35);
//             delay imports are refused in both modes
//   KUSER     KUSER_SHARED_DATA at the user address MM_SHARED_USER_DATA_VA = 7FFE0000H, read-only, SYNTHESIZED field by
//             field (never a copy of the host page - the old code's memcpy of the live host page is NOT ported)
//   PEB, TEB  one process, one thread; GS base = the TEB (the Windows x64 user convention: TEB.NtTib.Self at gs:[30H])
//   stack     SizeOfStackReserve of the image (at most F00000H), fully committed, [RSP] = a return sentinel at the entry
//   kernel    two emulator pages above lpMaximumApplicationAddress (the profile's 7FFFFFFEFFFFH): a GDT with the Windows x64
//             selectors and the system-call entry (IA32_LSTAR) holding one SYSRETQ. User mode is entered by SYSRETQ
//             (CPL 3, CS 33H, SS 2BH); DS / ES / GS = 2BH and FS = 53H are loaded by MOV in a start stub before it.
//             Without paging in the engine a high-half (kernel) linear address is cut to the 52-bit physical width
//             (probed: a read of FFFFF80000000000H faults at FF80000000000H), so the pages sit in the low half.
//   syscalls  SYSCALL executes as the SDM defines it (fork U901: RCX := RIP, R11 := RFLAGS, CPL 0, RIP := IA32_LSTAR); the
//             UC_X86_INS_SYSCALL hook then IS the kernel: it dispatches on EAX AND FFFFH (plan 6.4.a; bit 12 = the win32k
//             table, unknown: no win32u capture) with the numbers parsed from the environment's ntdll capture (plan 6.4.b,
//             decision 3.0.8) to emu::nt::kernel (plan 3.M5, U2007: the services and their sources are listed in
//             nt_kernel.hpp), from ANY address (plan 1.K.6: a direct SYSCALL from the image or from allocated memory). An
//             unknown number, a known but unimplemented service, or an argument combination without a documented answer
//             STOPS the run loudly with a state dump (plan 6.4, AGENT_RULES "Host safety": never a silent success). The
//             SYSRETQ at LSTAR returns to user mode with the status in RAX. INT 2EH and SYSENTER are faults / #UD (stopped,
//             never forwarded).
// --Sys / --HLE (D3, plan 6.1): recorded in every log line set; both share the one kernel layer (the --HLE APIs call into
// it), so an image both modes run gives the same observable result (checked by the suites).
// Plan 3.M5 part 2 (U2025-U2031, emu/win_process.hpp): the process state at the entry - the environment's system DLL images
// (ntdll, kernel32, kernelbase of the capture) mapped as data at fixed slots (MEM_IMAGE views), PEB Ldr with the three module
// lists, RTL_USER_PROCESS_PARAMETERS + environment block (placeholders), the process heap (_HEAP Flags HEAP_GROWABLE, ForceFlags
// 0), NtGlobalFlag 0 and the kernel-side PEB members of MmCreatePeb. --Sys --loader (opt-in, U2031): the kernel maps ntdll only,
// adds the NLS and API set views and starts the thread in the real ntdll!LdrInitializeThunk( CONTEXT ) by IRETQ; the real loader
// then runs in emulation until the first system service without a source (P36). Under --HLE an instruction fetched from a system
// image stops loudly naming the export.
//
// Layouts (cited per field in win_env.cpp; checked by Emulator\tools\check_win_layout.py and static_asserts):
//   TEB   Windows SDK / WDK 10.0.26100.0 shared\ksamd64.inc "Te*" offsets (TeSelf 30H, TeClientId 40H, TePeb 60H, ...);
//         um\winnt.h NT_TIB; um\winternl.h TEB.ProcessEnvironmentBlock / TlsSlots
//   PEB   ksamd64.inc PeBeingDebugged 2H / PeProcessParameters 20H; winternl.h PEB.BeingDebugged / Ldr / ProcessParameters;
//         ImageBaseAddress 10H, ProcessHeap 30H, OSMajorVersion 118H, OSMinorVersion 11CH, OSBuildNumber 120H, OSPlatformId
//         124H: the Intel Pin kit's adaptor\windows\ntdll.h PEB (an independent layout, notes\sdeida\06_pinrt_runtime.md
//         section 3; field offsets recomputed by check_win_layout.py)
//   KUSER WDK km\ntddk.h KUSER_SHARED_DATA C_ASSERT(FIELD_OFFSET(...)) lines (and the ksamd64.inc "Us*" offsets)
//   GDT   ksamd64.inc KGDT64_* (R0 code 10H, R0 data 18H, R3 compat code 20H, R3 data 28H, R3 code 30H, R3 compat TEB 50H),
//         RPL_MASK 3; descriptor format 253668-093 Vol3A 3.4.5 (L flag p. 3-12)
//   SYSCALL / SYSRET  253667-093 Vol2B SYSCALL Operation p. 4-700, SYSRET Operation p. 4-710; FS / GS loads in 64-bit mode
//         253668-093 Vol3A 3.4.4 p. 3-9 ("load a standard 32-bit base ... bits above the standard 32 bits are cleared")
// Values: the OS version / product type / system root from the environment's capture manifest; processor count, physical
// memory, processor architecture and the user address range from the platform profile (views\environment.txt); the time fields
// (TickCountMultiplier, InterruptTime, SystemTime, TimeZoneBias, QpcFrequency, TickCount) from the one virtual time base (U2095,
// plan 3.M6, emu/vclock.hpp: refreshed when the guest reads them); ProcessorFeatures stay 0 until their first consumer (none in M5).
#pragma once

#include <emu/engine.hpp>
#include <emu/hle.hpp>
#include <emu/nt_kernel.hpp>
#include <emu/options.hpp>
#include <emu/pe_image.hpp>
#include <emu/reg.hpp>
#include <emu/sysdll.hpp>
#include <emu/win_process.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace emu::win
{
	// ---- guest addresses of the environment (synthesized; the KUSER address is the documented one)
	inline constexpr uint64_t guest_kuser_va = 0x7FFE0000;                 // ksamd64.inc MM_SHARED_USER_DATA_VA (:986)
	inline constexpr uint64_t guest_kernel_gdt_va = 0x00007FFFFFFF0000ull;  // the emulator's kernel pages (see above)
	inline constexpr uint64_t guest_kernel_entry_va = 0x00007FFFFFFF1000ull;
	inline constexpr uint64_t guest_return_sentinel = 0x00007FFFFFFFF000ull; // never mapped: returning from the entry ends the run
	inline constexpr uint64_t guest_kernel_stack_va = 0x00007FFFFFFF2000ull;  // U2030: the emulator kernel's stack page (IRETQ frames)
	inline constexpr uint64_t guest_stack_top = 0x1000000ull;
	inline constexpr uint64_t guest_stack_max_reserve = 0xF00000ull;
	inline constexpr uint64_t guest_peb_va = 0x1010000ull;
	inline constexpr uint64_t guest_teb_va = 0x1020000ull;
	inline constexpr uint64_t guest_teb_size = 0x2000;                       // TeTlsExpansionSlots 1780H + 8 fits
	inline constexpr uint64_t guest_process_id = 0x1000, guest_thread_id = 0x1004;   // synthesized client id
	inline constexpr uint64_t guest_initial_rflags = 0x202;                  // IF (ksamd64.inc EFLAGS_IF_MASK 200H) + bit 1

	// ---- field offsets (sources in win_env.cpp; names never start with Nt / Zw: no_host_passthrough)
	namespace layout
	{
		// TEB (ksamd64.inc Te* :661-694)
		inline constexpr uint64_t teb_exception_list = 0x0000, teb_stack_base = 0x0008, teb_stack_limit = 0x0010, teb_self = 0x0030,
								  teb_client_id = 0x0040, teb_tls_pointer = 0x0058, teb_peb = 0x0060, teb_last_error = 0x0068,
								  teb_deallocation_stack = 0x1478, teb_tls_slots = 0x1480;
		// PEB
		inline constexpr uint64_t peb_being_debugged = 0x002, peb_image_base_address = 0x010, peb_ldr = 0x018, peb_process_parameters = 0x020,
								  peb_process_heap = 0x030, peb_os_major_version = 0x118, peb_os_minor_version = 0x11C, peb_os_build_number = 0x120,
								  peb_os_csd_version = 0x122, peb_os_platform_id = 0x124;
		// KUSER_SHARED_DATA (ntddk.h C_ASSERT :9540-9634)
		inline constexpr uint64_t kuser_tick_count_multiplier = 0x004, kuser_interrupt_time = 0x008, kuser_system_time = 0x014,
								  kuser_time_zone_bias = 0x020, kuser_image_number_low = 0x02C, kuser_image_number_high = 0x02E,
								  kuser_system_root = 0x030, kuser_build_number = 0x260, kuser_product_type = 0x264,
								  kuser_product_type_is_valid = 0x268, kuser_native_processor_architecture = 0x26A,
								  kuser_major_version = 0x26C, kuser_minor_version = 0x270, kuser_processor_features = 0x274,
								  kuser_kd_debugger_enabled = 0x2D4, kuser_number_of_physical_pages = 0x2E8, kuser_safe_boot_mode = 0x2EC,
								  kuser_system_call = 0x308, kuser_active_processor_count = 0x3C0, kuser_active_group_count = 0x3C4;
		inline constexpr uint64_t kuser_system_root_chars = 260;   // WCHAR NtSystemRoot[260] (ntddk.h; next field at 238H)
		// U2095 (plan 3.M6): KUSER time fields (public-symbol layout, emu/win_fields.hpp: TickCountLowDeprecated 0H, QpcFrequency 300H,
		// TickCount 320H (KSYSTEM_TIME / TickCountQuad), TickCountPad 32CH)
		inline constexpr uint64_t kuser_tick_count_low_deprecated = 0x000, kuser_qpc_frequency = 0x300, kuser_tick_count = 0x320;
		// GDT (ksamd64.inc KGDT64_* :1017-1024, RPL_MASK :1170)
		inline constexpr uint16_t gdt_r0_code = 0x10, gdt_r0_data = 0x18, gdt_r3_cmcode = 0x20, gdt_r3_data = 0x28, gdt_r3_code = 0x30,
								  gdt_sys_tss = 0x40, gdt_r3_cmteb = 0x50, rpl3 = 3;
		inline constexpr uint64_t gdt_limit = 0x57;   // up to and including the R3 compat-TEB descriptor (50H..57H)
	}

	// everything the guest observes about the OS, from the captures / profiles (never from the running machine)
	struct environment_facts
	{
		environment env = environment::none;
		uint32_t os_major = 0, os_minor = 0, os_build = 0, product_type = 0;
		std::string system_root;
		uint32_t processors = 0, processor_architecture = 0;
		uint64_t total_phys = 0, min_app_address = 0, max_app_address = 0;
		uint32_t page_size = 0x1000, allocation_granularity = 0x10000;   // U2011
		uint64_t active_processor_mask = 0;
		syscall_table syscalls;
		syscall_table win32k_syscalls;   // P44 (U2117): the capture's win32u.dll stubs (empty without win32u.dll in the capture)
		std::string capture_dir, platform_source;   // where they came from (for the log)
		// U2024 (plan 3.M5 part 2): the capture's manifest and its further files (lower-case name -> bytes), read as data once;
		// the system DLL images and NLS tables of the environment (empty for a capture made before M5 part 2 / synthetic facts)
		capture_manifest manifest;
		std::shared_ptr<const std::map<std::string, std::vector<uint8_t>>> files;
		std::shared_ptr<const std::map<std::string, nt::registry_key>> registry;   // U2034: the captured registry subset (may be empty)
	};
	// the capture manifest + ntdll syscall table of ENV, and the platform profile facts
	bool load_environment_facts( environment env, const std::string& capture_dir_explicit, const std::string& data_dir, environment_facts& f,
								 std::vector<std::string>& log, std::string& error );

	struct process_options
	{
		env_boundary boundary = env_boundary::sys;
		bool image_base_given = false;
		uint64_t image_base = 0;
		uint64_t max_insns = 100000000;   // every run is bounded (no host time)
		std::string image_name = "image";
		std::string image_file_name = "image.exe";   // U2025: the image's base name in the loader data / process parameters
		bool loader = false;                         // U2031: --Sys --loader: start in the real ntdll!LdrInitializeThunk (opt-in)
		// U2020 (plan 3.M5 part 2): instrumentation, off by default (it observes, never changes, what the guest sees)
		bool counters = false;                         // probe counters + the last trace_tail instructions, summary at the stop
		uint32_t trace_tail = 32;
		std::string trace_path;                        // "" = no per-instruction trace file; else one line per instruction
		uint64_t trace_max_bytes = 1ull << 30;         // the trace file stops growing here (disk rule), marked truncated
		// U2040: register-level trace (observation only): each line + RAX..R15 / RFLAGS before the instruction + its memory reads /
		// writes; trace_last > 0 keeps only the last trace_last lines in memory and writes them when the run stops
		bool trace_regs = false;
		uint64_t trace_last = 0;
		uint64_t trace_from = 0;   // P58 (U2383): trace lines only from this instruction count on (0 = from the start)
		// P41 (U2084): the console capture - the folder of console_capture.txt ("" = the attempts are only listed, no file); the file is
		// created at the first attempt (a run without one creates nothing); at most console_max_bytes (disk rule), then marked truncated
		std::string console_dir;
		uint64_t console_max_bytes = 64ull << 20;
		// P57 (U2373, plan 5.C 8.4 / 8.5): the OEP stop and the dump - observation only (a write hook records which pages the guest's
		// instructions wrote; nothing the guest sees changes). stop_written_exec: stop BEFORE the first instruction on such a page;
		// stop_at: stop BEFORE the first execution of that address. dump_dir: at the stop, write the emulated memory as data files into
		// that existing folder (process::write_dump) - a sample's dump is a derivative of it: it stays local, it is never loaded or run.
		bool stop_written_exec = false;
		bool stop_at_given = false;
		uint64_t stop_at = 0;
		std::string dump_dir;
		uint64_t dump_max_bytes = 1ull << 30;   // per file (the disk rule)
		// P58 (U2383): watched ranges [address, +length): every guest write into them is recorded in run_result::watch_records (observation)
		std::vector<std::pair<uint64_t, uint64_t>> watch_writes;
		bool watch_value_given = false;   // P58 (U2383): record only writes of this value
		uint64_t watch_value = 0;
	};

	enum class stop_kind { none, terminated, unknown_syscall, unimplemented_syscall, unimplemented_api, fault, count, returned, error, oep_stop };
	const char* name_of( stop_kind k );

	struct syscall_record
	{
		uint64_t index = 0;          // 1, 2, ...
		uint64_t at = 0;             // the SYSCALL instruction's address (user mode)
		uint64_t rax = 0;            // EAX as the guest left it (the full register)
		uint32_t number = 0;         // EAX AND FFFFH
		std::string name;            // from the environment's ntdll ("" = unknown)
		std::array<uint64_t, 12> args{};  // R10, RDX, R8, R9, [RSP+28H], [RSP+30H] (+ [RSP+38H] .. [RSP+60H], U2031)
		bool stack_args_read = false;
		std::string outcome;
		bool answered = false;          // U2012: the kernel layer answered (status in RAX)
		uint32_t status = 0;
		std::vector<std::string> writes;   // the OUT parameters the service wrote
	};

	struct api_record   // U2012: one --HLE API call (a stub reached)
	{
		uint64_t index = 0, stub = 0, rax = 0;
		std::string api, outcome;
		std::array<uint64_t, 4> args{};   // RCX, RDX, R8, R9
	};

	// P41 (U2084): one console / standard-handle output ATTEMPT of the emulated process (observation only - what it got is unchanged):
	// --HLE: WriteFile / WriteConsoleA / W through the API stubs; --Sys: execution entering kernelbase's WriteFile (on a standard handle
	// or handle 0), WriteConsoleA / W, OutputDebugStringA / W (the result read at the return), and NtWriteFile on such a handle outside them
	struct console_attempt
	{
		uint64_t index = 0, insn = 0;   // 1, 2, ...; the instruction count at the call
		std::string via;                // "kernelbase.dll!WriteFile", "KERNEL32.dll!WriteConsoleW" (--HLE), "NtWriteFile" ...
		uint64_t handle = 0;
		uint64_t length = 0;            // bytes / characters the program passed
		std::string text;               // decoded, at most 512 characters, control characters escaped
		std::string result;             // what the program got
	};

	// U2020 (plan 3.M5 part 2, demand item 1 of oep_progress.md run 1): the probe counters of one run (process_options::counters).
	// Observation only: the hooks read what the guest executed / read; nothing the guest observes changes (no value is altered,
	// no timing exists - the bound is the instruction count). Instructions are recognised from their bytes after the legacy /
	// REX prefixes (253667-093 Vol2B: CPUID 0F A2, RDTSC 0F 31, RDTSCP 0F 01 F9; RDPID F3 0F C7 /7 with ModRM.mod = 11B); the
	// CPUID answer is the register state before the next instruction (EAX / EBX / ECX / EDX, 253667-093 Vol2A CPUID "Operation").
	// Reads of the environment's structures (PEB, TEB, KUSER_SHARED_DATA, the process parameters and loader data the environment
	// builds) are counted per member, named by the public-symbol field tables (emu/win_fields.hpp).
	struct cpuid_probe
	{
		uint32_t leaf = 0, subleaf = 0;
		uint64_t count = 0, first_at = 0;
		bool answered = false;
		uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;   // the first answer
	};
	struct read_probe
	{
		std::string region, field;
		uint64_t count = 0, first_at = 0;
	};
	struct trace_tail_entry
	{
		uint64_t index = 0, rip = 0;
		uint32_t size = 0;
		uint8_t bytes[ 16 ] = {};
	};
	struct probe_counters
	{
		bool enabled = false;
		std::map<std::pair<uint32_t, uint32_t>, cpuid_probe> cpuid;   // (leaf, sub-leaf as ECX)
		uint64_t cpuid_total = 0, rdtsc = 0, rdtscp = 0, rdpid = 0;
		std::map<std::string, read_probe> reads;                     // "PEB.BeingDebugged" ...
		uint64_t reads_total = 0;
		std::map<std::string, uint64_t> syscalls, apis;              // by name
		std::map<int, uint64_t> exceptions;                          // by vector
		std::vector<trace_tail_entry> tail;                          // the last instructions (ring, oldest first after the run)
		uint64_t trace_lines = 0, trace_bytes = 0;
		bool trace_truncated = false;
	};

	// P49 (plan 3.M7, U2250): one CPU event of the user-mode program and what the emulated kernel made of it (observation; the same
	// record in --Sys and --HLE). vector = the CPU vector delivered through the emulated IDT (14 = a #PF from an unmapped / protected
	// access), how = "dispatched" (KiUserExceptionDispatcher / the --HLE dispatcher), "silent" (INT 2DH services 1 / 3 / 4 / 5: no
	// exception, measured TX1 e18 / e31 / e34 / e35)
	struct exception_event
	{
		uint64_t index = 0, insn = 0;
		int vector = -1;
		uint32_t error_code = 0;
		bool has_error_code = false;
		uint64_t frame_rip = 0, frame_rflags = 0, frame_rsp = 0;   // what the CPU pushed (the #PF path: the state at the fault, RF = 1)
		uint32_t code = 0;                                          // ExceptionCode
		uint64_t address = 0, context_rip = 0;                      // ExceptionAddress, CONTEXT.Rip
		uint32_t parameters = 0;
		uint64_t info[ 2 ] = {};
		std::string how;
	};

	struct run_result
	{
		stop_kind stop = stop_kind::none;
		std::string detail;
		uint32_t exit_status = 0;              // stop_kind::terminated
		uint64_t insns = 0;                    // instructions executed (user code + the environment's start stub / SYSRETQ)
		uint64_t syscall_insns = 0;            // SYSCALL instructions the guest executed (seen by the code hook)
		uint64_t syscalls_handled = 0;         // handled by this kernel layer (the UC_X86_INS_SYSCALL hook)
		std::vector<syscall_record> syscalls;
		std::vector<api_record> api_calls;     // U2012
		int fault_vector = -1;
		int64_t fault_error_code = -1;
		uint64_t fault_address = 0;            // #PF: the address; returned: the sentinel
		uint64_t user_rip = 0, user_rflags = 0, user_rsp = 0;   // at a syscall stop: the SYSCALL instruction, R11, RSP
		reg::dump final;
		bool have_final = false;
		probe_counters probes;                 // U2020: filled when process_options::counters
		std::vector<console_attempt> console;  // P41 (U2084): the output attempts, in order
		std::string console_file;              // the capture file written ("" = none: no attempt, or no folder)
		bool console_truncated = false;
		// P45 (U2128): the file-system accesses of the emulated program (emu::nt::kernel::fs_log) and their file <out dir>\fs_access.txt
		uint64_t fs_accesses = 0;
		std::string fs_file;                   // "" = none: no access, or no folder
		bool fs_truncated = false;
		std::vector<exception_event> exceptions;   // P49 (U2250): the CPU events of the run, in order
		// P57 (U2373): the OEP stop (stop_kind::oep_stop) - the instruction it stopped before (counted in insns, not executed), how it was
		// found, and for a written page: the page's first guest write (instruction count, RIP) and its write count
		uint64_t oep_insn = 0, oep_rip = 0, oep_page_first_insn = 0, oep_page_first_rip = 0, oep_page_writes = 0;
		std::string oep_how;
		uint64_t written_pages = 0;            // pages the guest's instructions wrote (with the write hook of stop_written_exec / dump_dir)
		std::vector<std::string> watch_records;   // P58 (U2383): the writes into the watched ranges, in order (at most 10000)
	};

	class process
	{
	public:
		// open the engine, map the image and build the environment; false (nullptr) with the reason
		static std::unique_ptr<process> create( const engine_config& ec, const environment_facts& facts, const process_options& po,
												const std::vector<uint8_t>& pe_file, std::vector<std::string>& log, std::string& error );
		~process();
		process( const process& ) = delete;
		process& operator=( const process& ) = delete;

		bool run( run_result& r, std::string& error );

		engine& eng() { return *e_; }
		const std::vector<std::string>& log() const { return log_; }
		uint64_t image_base() const { return map_.base; }
		uint64_t entry() const { return map_.entry; }
		uint64_t peb() const { return guest_peb_va; }
		uint64_t teb() const { return guest_teb_va; }
		uint64_t user_rsp() const { return user_rsp_; }
		const pe::image& img() const { return img_; }
		const syscall_table& syscalls() const { return facts_.syscalls; }
		const syscall_table& win32k_syscalls() const { return facts_.win32k_syscalls; }   // P44 (U2117)
		nt::kernel& kern() { return *kernel_; }                       // U2012: the one kernel layer
		hle::module& hle() { return *hle_; }
		uint64_t start_va() const { return start_va_; }               // the entry, or the TLS start trampoline (--HLE)
		const pe::tls_directory& tls() const { return tls_; }

		// the result lines that must not depend on --Sys / --HLE (the suites compare them between the two modes)
		std::vector<std::string> observable( const run_result& r ) const;
		// the loud-failure state dump: stop, syscall record, user-mode RIP / RFLAGS, register dump, memory map
		std::vector<std::string> state_dump( const run_result& r ) const;
		// the environment fields written (name = value), part of observable()
		const std::vector<std::string>& environment_lines() const { return env_lines_; }
		// U2020: the probe-counter summary of a run with process_options::counters ("probe: ..." lines; empty otherwise)
		std::vector<std::string> probe_summary( const run_result& r ) const;
		// P57 (U2373): the call stack at the stop (the x64 unwind through each module's .pdata, a leaf = [RSP]) and a return-address scan
		// of the stack (qwords that point into a module right after a CALL) - text lines
		std::vector<std::string> call_stack( const run_result& r );
		// P58 (U2383): the return addresses on the stack [RSP, +SPAN): qwords pointing into a module right after a CALL (win_dump.cpp)
		std::vector<std::string> return_scan( uint64_t rsp, uint64_t span, size_t max_lines );
		// P57 (U2373): process_options::dump_dir - the dump at the stop (files listed in win_env.cpp write_dump); LINES = what was written
		bool write_dump( const run_result& r, std::vector<std::string>& lines, std::string& error );
		// P41 (U2084): the console capture's summary (the file, the attempt count, each attempt) and the process parameters' console
		// fields as the process left them (ConsoleHandle, StandardInput / Output / Error of PEB->ProcessParameters at the stop)
		std::vector<std::string> console_summary( const run_result& r ) const;
		// U2020: ADDRESS as "<region> +off" (the engine region holding it), for traces and summaries
		std::string where( uint64_t address ) const;
		// U2020: the environment structure and member ADDRESS lies in (PEB / TEB / KUSER / ...); false outside them
		bool name_field( uint64_t address, std::string& region, std::string& field ) const;
		size_t tail_capacity() const { return po_.counters ? po_.trace_tail : 0; }
		uint64_t trace_limit() const { return po_.trace_max_bytes; }

		struct hook_ctx;   // the hooks' state (win_env.cpp)
		// P44 (U2118): enter / leave a kernel -> user callback (nt::reply_kind::callback / callback_return) at a SYSCALL hook
		bool callback_transfer( uc_engine* uc, hook_ctx& c, const nt::reply& rep, const std::string& service, std::string& why );

		// U2095 (plan 3.M6): the TSC rate of the run's virtual time base (emu/vclock.hpp) and where it came from
		uint64_t tsc_hz() const { return tsc_hz_; }
		const std::string& tsc_source() const { return tsc_source_; }
		// U2095: rewrite KUSER_SHARED_DATA's time fields (InterruptTime, SystemTime, TickCount) from the virtual time base - called when the
		// guest reads them (a UC_HOOK_MEM_READ on those fields) and at the start; false = the clock could not be read
		bool refresh_kuser_time( uc_engine* uc );

	private:
		process() = default;
		bool build( const environment_facts& facts, const process_options& po, const std::vector<uint8_t>& pe_file, std::string& error );

		// U2095: the KUSER_SHARED_DATA page's bytes (an emulator-owned buffer the engine maps read-only for the guest, engine::map BACKING):
		// declared before e_ so that the engine is closed first
		std::unique_ptr<uint8_t[]> kuser_page_;
		std::unique_ptr<engine> e_;
		pe::image img_;
		pe::mapping map_;
		environment_facts facts_;
		process_options po_;
		uint64_t user_rsp_ = 0, stack_base_ = 0, stack_reserve_ = 0, start_stub_ = 0, start_sysret_ = 0;
		std::vector<std::string> log_;
		std::vector<std::string> env_lines_;
		std::unique_ptr<hook_ctx> hc_;
		std::unique_ptr<nt::kernel> kernel_;     // U2012
		std::unique_ptr<hle::module> hle_;
		pe::tls_directory tls_;
		uint64_t start_va_ = 0;
		std::vector<module_image> modules_;      // U2025: the process image + the system DLL images, load order
		std::vector<module_image> scan_mods_;    // P58 (U2383): return_scan's module list (every image view), cached
		size_t scan_regions_ = 0;
		process_state pstate_;                   // U2025-U2027: loader data, process parameters, heap
		bool loader_start_ = false;              // U2031: --Sys through the real ntdll!LdrInitializeThunk
		uint64_t start_context_ = 0, iretq_stub_ = 0, start_rsp_ = 0, ntdll_base_ = 0;
		uint64_t rng_seed_ = 0;   // P39 (U2052): engine_config::rng_seed (the kernel's process cookie)
		uint64_t tsc_hz_ = 0;     // U2095 (plan 3.M6): the virtual time base's TSC rate
		std::string tsc_source_;
		bool build_kernel_views( const environment_facts& f, std::vector<std::string>& lines, std::string& error );
		// P49 (plan 3.M7, U2250-U2256; win_exception.cpp): the exception path - the emulated IDT / TSS and the kernel stubs, KUSER XState
		bool build_exceptions( std::vector<std::string>& lines, std::string& error );
		uint64_t idt_va_ = 0, tss_va_ = 0, ksave_va_ = 0, idt_stub_va_ = 0, save_stub_ = 0, save_stub_end_ = 0, restore_stub_ = 0,
				 restore_stub_end_ = 0;
		uint64_t xstate_user_mask_ = 0;     // the user XSAVE components a frame carries (XCR0 without x87 / SSE: the legacy area is FltSave)
		uint64_t xstate_user_size_ = 0;     // the compacted XSAVE size of those (header 40H + the components; Vol1 13.4.3)
		bool xstate_ok_ = false;            // the profile gives the layout and XSAVEC (CPUID.(EAX=0DH, ECX=1):EAX[1])
		uint64_t ki_user_exception_dispatcher_ = 0;   // --Sys: ntdll!KiUserExceptionDispatcher
		// the x64 unwind of one frame (win_exception.cpp): G = RAX .. R15 in / out, RIP in = the control PC, out = the caller's
		bool virtual_unwind( uint64_t image_base, uint64_t control_pc, uint64_t function_entry, uint64_t* g, uint64_t& rip, uint64_t& handler, uint64_t& handler_data,
							 uint64_t& establisher, std::string& why );

	public:
		bool loader_start() const { return loader_start_; }
		// U2030: load the CPU state of the guest CONTEXT at CTX (NtContinue) and return to user mode through IRETQ; false = why not
		bool resume_context( uc_engine* uc, uint64_t ctx, std::string& why );
		uint64_t iretq_stub() const { return iretq_stub_; }
		// U2031: a module the kernel mapped later (a DLL view of the loader) - for naming addresses
		void add_module( module_image m ) { modules_.push_back( std::move( m ) ); }
		const std::vector<module_image>& modules() const { return modules_; }
		const process_state& state() const { return pstate_; }
		// P41 (U2084): HANDLE is one of the process' standard handles (the current process parameters) or 0
		bool std_handle( uc_engine* uc, uint64_t handle ) const;

		// P49 (plan 3.M7, win_exception.cpp): the hooks of the exception path - an IDT gate stub reached (the CPU delivered VECTOR at CPL 0),
		// the save / restore stubs' end, a #PF from an unmapped / protected access (the memory hook), the --HLE dispatcher's return point;
		// each returns false with WHY = a loud stop
		bool exception_gate( uc_engine* uc, hook_ctx& c, unsigned vector, std::string& why );
		bool exception_saved( uc_engine* uc, hook_ctx& c, std::string& why );
		bool exception_saved_body( uc_engine* uc, hook_ctx& c, std::string& why );
		bool context_restored( uc_engine* uc, hook_ctx& c, std::string& why );
		bool page_fault( uc_engine* uc, hook_ctx& c, uint64_t rip, uint64_t rflags, uint64_t address, int access, std::string& why );
		bool hle_dispatch_return( uc_engine* uc, hook_ctx& c, std::string& why );
		// NtContinue / the end of a dispatch: CONTEXT at CTX with CONTEXT_XSTATE - FltSave / the XSAVE area through FXRSTOR64 / XRSTOR64 in a kernel
		// stub, then IRETQ (false = not this path: CONTEXT_XSTATE absent; WHY non-empty = a loud stop)
		bool restore_full_context( uc_engine* uc, hook_ctx& c, uint64_t ctx, std::string& why );
		uint64_t idt_stub_va() const { return idt_stub_va_; }
		uint64_t save_stub_end() const { return save_stub_end_; }
		uint64_t restore_stub_end() const { return restore_stub_end_; }
	};
}
