// emu::nt (plan 3.M5 = 3.B 6.4 a / b / d, ledger U2007): the emulated Windows kernel layer behind SYSCALL - one layer for
// --Sys and --HLE (D3, plan 6.1: the HLE API reimplementations call into this same layer). Everything happens in the engine's
// guest memory and in this object; nothing is forwarded to the host kernel (AGENT_RULES "Host safety", plan 6.4.d), no handler
// is named Nt* / Zw* (no_host_passthrough pt_nt_symbol: the guest's service names appear only as strings).
//
// Dispatch: the SYSCALL hook (win_env.cpp) passes the guest's call - EAX AND FFFFH (plan 6.4.a), the service name the
// environment's own ntdll stub gives that number (plan 6.4.b, decision 3.0.8), R10 / RDX / R8 / R9 and the stack arguments
// [RSP+28H] / [RSP+30H] - to dispatch(). The SYSCALL may come from ANY address (plan 1.K.6: a direct system call from the
// image's own code or from allocated memory is the same call - the kernel never checks where it came from).
//
// Rule for every service (AGENT_RULES "Host safety": "An unknown or unimplemented service fails loudly ... never a silent
// success with OUT parameters left unwritten"): a call is either answered with the documented semantics (status in RAX, every
// OUT parameter written) or STOPPED loudly with the reason (reply::kind stop). Where the public documentation does not state
// what Windows answers for a combination (an argument value, an error case), the combination is a loud stop, never a guess.
// Guest pointers are probed like a kernel probes user buffers (inside the profile's user range, mapped, readable / writable;
// 64-bit arithmetic, no wrap): a bad pointer is STATUS_ACCESS_VIOLATION and nothing is written.
//
// Services of milestone M5 (each with its source; prototypes from the Windows SDK / WDK 10.0.26100.0 headers, semantics from
// the Microsoft documentation of the routine named):
//   NtTerminateProcess        km\ntddk.h ZwTerminateProcess - ProcessHandle NtCurrentProcess() only (as M4)
//   NtClose                   km\wdm.h ZwClose - "STATUS_INVALID_HANDLE if Handle is not a valid handle"; no service of M5
//                             creates a handle, so every handle except the pseudo handles is invalid; a pseudo handle = stop
//   NtAllocateVirtualMemory   km\ntifs.h ZwAllocateVirtualMemory (+ the VirtualAlloc rules for rounding / commit)
//   NtFreeVirtualMemory       km\ntifs.h ZwFreeVirtualMemory (+ VirtualFree)
//   NtQueryVirtualMemory      km\ntifs.h ZwQueryVirtualMemory, MemoryBasicInformation only; um\winnt.h MEMORY_BASIC_INFORMATION
//   NtQueryInformationProcess um\winternl.h NtQueryInformationProcess: ProcessBasicInformation (0), ProcessDebugPort (7),
//                             ProcessWow64Information (26), ProcessBreakOnTermination (29) - the classes winternl.h
//                             documents with their semantics; every other class (e.g. 1EH / 1FH) = stop
//   NtQueryInformationThread  P55 (U2362): ThreadBasicInformation (0) of NtCurrentThread() only, MEASURED (tm_nt nqit; phnt ntpsapi.h
//                             THREAD_BASIC_INFORMATION); other handles / classes = stop
//   NtSetInformationThread    P56 (U2366): class 4 ThreadAffinityMask of NtCurrentThread(), MEASURED (tm_nt nsit; the new mask = the
//                             value AND the process mask, 0 = STATUS_INVALID_PARAMETER; Length other than 8 = STATUS_INFO_LENGTH_MISMATCH)
//                             and class 11H ThreadHideFromDebugger, MEASURED (tm_nt nhfd: Length 0 = STATUS_SUCCESS, the buffer ignored;
//                             any other Length = STATUS_INFO_LENGTH_MISMATCH)
// Plan 3.M5 part 2 (U2030-U2035; nt_kernel_sys.cpp, each with its source there): NtContinue (a resume of the CONTEXT),
//   NtQueryPerformanceCounter (U2094, plan 3.M6: the one virtual time base, emu/vclock.hpp; M5's stand-in counted one 100 ns tick
//   per instruction), NtQuerySystemTime / NtDelayExecution / time-outs of NtWaitForSingleObject (U2094: tm_nt case time),
//   NtProtectVirtualMemory, NtAllocateVirtualMemoryEx, NtCreateEvent / NtSetEvent / NtResetEvent / NtClearEvent /
//   NtWaitForSingleObject (one thread), NtQuerySystemInformation 0 / 3EH / 72H (C0H answered STATUS_INVALID_INFO_CLASS: no layout
//   source), NtQueryInformationProcess 22H / 24H, NtQueryVirtualMemory classes 4 / 6 and image / mapped views, NtOpenKey(Ex) /
//   NtQueryValueKey on the environment's CAPTURED registry subset (absent = STATUS_OBJECT_NAME_NOT_FOUND, not captured = stop),
//   NtClose of the handle table; reservations are address space only (committed pages are mapped), MEM_RELEASE of a part.
// Status values: shared\ntstatus.h. Memory constants: um\winnt.h MEM_* / PAGE_* (own names below: no_native_check rule
// exec_page_protection forbids PAGE_EXECUTE* identifiers in emulator code - they would read as host executable memory).
//
// Virtual memory model (private allocations only): a reservation is mapped in the engine with no access (an access is a
// fault = STATUS_ACCESS_VIOLATION in Windows); committing a page gives it the requested protection and zero contents;
// decommit returns it to no access (zeroed); release unmaps the reservation. Reservations are aligned to the allocation
// granularity and sized in pages (the platform profile's GetNativeSystemInfo: dwPageSize, dwAllocationGranularity); the
// address of an allocation without a requested base is the lowest (MEM_TOP_DOWN: highest) free one in the user range - a
// deterministic choice (Windows randomizes; not modelled). The environment's own regions (image, stack, PEB, TEB, KUSER,
// the emulator's kernel and HLE pages) are occupied address space; NtQueryVirtualMemory on them is a loud stop in M5
// (Windows' answers for image / stack / PEB pages have no cited source yet).
#pragma once

#include <emu/engine.hpp>
#include <emu/ctr_drbg.hpp>   // P47 (U2166)

#include <array>
#include <initializer_list>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace emu::nt
{
	namespace status   // shared\ntstatus.h
	{
		inline constexpr uint32_t success = 0x00000000, pending = 0x00000103, info_length_mismatch = 0xC0000004, access_violation = 0xC0000005,
								  invalid_handle = 0xC0000008, invalid_parameter = 0xC000000D, conflicting_addresses = 0xC0000018,
								  invalid_page_protection = 0xC0000045,
								  // U2032 (shared\ntstatus.h)
								  not_committed = 0xC000002D, object_name_not_found = 0xC0000034, object_path_not_found = 0xC000003A, buffer_too_small = 0xC0000023,
								  invalid_info_class = 0xC0000003, not_implemented = 0xC0000002, image_not_at_base = 0x40000003, buffer_overflow = 0x80000005,
								  no_more_entries = 0x8000001A, object_type_mismatch = 0xC0000024, not_supported = 0xC00000BB,
								  invalid_address = 0xC0000141,
								  // P39 (shared\ntstatus.h)
								  not_found = 0xC0000225;
	}
	namespace mem   // um\winnt.h MEM_*
	{
		inline constexpr uint32_t commit = 0x1000, reserve = 0x2000, decommit = 0x4000, release = 0x8000, free_ = 0x10000, private_ = 0x20000,
								  top_down = 0x100000;
	}
	namespace page   // um\winnt.h PAGE_* (values); own names (see above)
	{
		inline constexpr uint32_t noaccess = 0x01, readonly = 0x02, readwrite = 0x04, writecopy = 0x08, execute = 0x10, execute_read = 0x20,
								  execute_readwrite = 0x40, execute_writecopy = 0x80, guard = 0x100, nocache = 0x200, writecombine = 0x400;
	}
	inline constexpr uint64_t current_process = ~0ull, current_thread = ~0ull - 1;   // NtCurrentProcess() = (HANDLE)-1, NtCurrentThread() = -2
	inline constexpr uint64_t mbi_size = 0x30, pbi_size = 0x30;                    // sizeof MEMORY_BASIC_INFORMATION / PROCESS_BASIC_INFORMATION (x64)
	inline constexpr uint64_t context_size = 0x4D0;                                 // sizeof _CONTEXT (x64; guest ntdll.pdb, U2030)

	// UC_PROT_* of a base PAGE_* value (0 = not a base value this layer models)
	bool uc_prot_of_page( uint32_t protect, uint32_t& uc_prot );
	std::string page_text( uint32_t protect );

	// U2034 (plan 3.M5 part 2): the environment's captured registry subset (capture_sysdlls.py --reg; local, never committed): per
	// key (lower-case full path "\registry\machine\...") present / absent and the captured values (REG_* type, data bytes)
	struct registry_value
	{
		uint32_t type = 0;
		std::vector<uint8_t> data;
	};
	struct registry_key
	{
		bool present = false;
		std::map<std::string, registry_value> values;   // lower-case value name
	};

	// the process / platform facts the services answer with (from the platform profile and the environment, never the host)
	struct kernel_facts
	{
		const std::map<std::string, registry_key>* registry = nullptr;   // U2034
		const std::map<std::string, std::vector<uint8_t>>* files = nullptr;   // P39 (U2057): the capture's system files (lower-case name -> bytes)
		std::string image_dir;                                              // P39 (U2058): the image's directory (DOS path; the CurrentDirectory)
		std::string image_name;                                             // P43 (U2107): the image's file name (the one file of that directory)
		std::string system_root;                                            // P39 (U2057): the environment's SystemRoot (e.g. C:\Windows)
		uint64_t min_app_address = 0, max_app_address = 0;
		uint32_t page_size = 0x1000, allocation_granularity = 0x10000;
		uint64_t active_processor_mask = 0;
		uint64_t process_id = 0, thread_id = 0, peb = 0, teb = 0;
		uint64_t total_phys = 0;             // U2032: the platform profile's TotalPhys
		uint32_t processors = 0;             // U2032: dwNumberOfProcessors
		uint64_t rng_seed = 0;               // P39 (U2052): engine_config::rng_seed (decision D8) - the deterministic process cookie
		uint64_t tsc_hz = 0;                 // U2094 (plan 3.M6): the TSC rate of the one virtual time base (emu/vclock.hpp; 0 = no clock)
		// P46 (U2142): the process image for ProcessImageInformation (25H) - its FILE's headers (the first SizeOfHeaders bytes, at most
		// 10000H), the file's size and the base the emulated image is mapped at (win_env; never a host file)
		std::vector<uint8_t> image_headers;
		uint64_t image_file_size = 0, image_mapped_base = 0;
	};

	// one system call as the guest made it
	struct call
	{
		uint32_t number = 0;                 // EAX AND FFFFH
		std::string name;                    // the environment's ntdll name of the number ("" = unknown)
		std::array<uint64_t, 12> args{};     // R10, RDX, R8, R9, [RSP+28H], [RSP+30H], ... [RSP+60H] (U2031: up to 12 arguments)
		bool stack_args = false;             // [RSP+28H] .. [RSP+60H] were readable
		uint64_t insns = 0;                  // U2032: instructions executed when the call was made (the virtual clock)
	};

	// U2030 (plan 3.M5 part 2): resume = NtContinue - the caller (the SYSCALL hook) loads the CPU state from the CONTEXT record at
	// reply::context and returns to user mode there (not through SYSRETQ)
	// P44 (U2118): callback = the kernel calls back into user mode (KeUserModeCallback: ntdll!KiUserCallbackDispatcher with the
	// callback number and an input buffer on the user stack); callback_return = NtCallbackReturn ended the innermost callback - the
	// interrupted service completes with STATUS (the process layer restores the user state of that service's SYSCALL)
	enum class reply_kind { done, terminated, stop, resume, callback, callback_return };
	struct reply
	{
		reply_kind kind = reply_kind::stop;
		uint32_t status = 0;                 // done: the NTSTATUS returned in RAX
		uint32_t exit_status = 0;            // terminated
		uint64_t context = 0;                // resume: the guest CONTEXT record (probed readable, 4D0H bytes)
		uint32_t callback_api = 0;           // callback: the index into PEB KernelCallbackTable (ApiNumber)
		std::vector<uint8_t> callback_input; // callback: the input buffer copied onto the user stack
		std::string text;                    // what happened (done / terminated) or why the call stops (stop)
		std::vector<std::string> writes;     // the OUT parameters written ("[addr] 8 = 0x...")
	};

	struct allocation
	{
		uint64_t base = 0, size = 0;         // the reservation (base aligned to the granularity, size in pages)
		uint32_t allocation_protect = 0;     // the Protect of the reserving call (PAGE_*)
		std::vector<uint32_t> pages;         // per page: 0 = reserved, else the committed PAGE_* protection
	};

	// U2031 (plan 3.M5 part 2): a view of a section in the process - an image (MEM_IMAGE: the process image, ntdll, the DLLs the
	// loader maps) or a data file (MEM_MAPPED: the NLS tables, the API set schema). Per-page protections (PAGE_*); for an image the
	// AllocationProtect is PAGE_EXECUTE_WRITECOPY and unwritten data pages read PAGE_WRITECOPY (Wine 11.0 dlls/ntdll/unix/virtual.c:
	// image views carry VPROT_WRITECOPY | VPROT_EXEC - licensed OSS reference, Windows' behaviour is not documented in detail)
	struct view
	{
		uint64_t base = 0, size = 0;
		uint32_t type = 0;                   // mem::image / mem::mapped
		uint32_t allocation_protect = 0;
		std::vector<uint32_t> pages;         // PAGE_* per page
		std::string name;
	};
	namespace mem2   // um\winnt.h
	{
		inline constexpr uint32_t mapped = 0x40000, image = 0x1000000;
	}

	// P44 (U2122): a region whose bytes are only partly MEASURED (the win32k / user32 shared views, the TEB's CLIENTINFO after the GUI
	// conversion): the process layer stops loudly when the guest reads a byte that is neither measured / modelled nor written by the
	// guest itself before ("an unmeasured field read = loud fail", USER DECISION 2026-10-27)
	struct watch
	{
		uint64_t base = 0, size = 0;
		std::string name;
		std::vector<std::pair<uint64_t, uint64_t>> known;   // [offset, offset + length): measured / modelled bytes
		bool active = true;
	};
	// P45 (U2128): one file-system access of the emulated program (observation only; win_env writes them to <out dir>\fs_access.txt)
	struct fs_access
	{
		uint64_t index = 0;                  // 1, 2, ... in the order of the calls
		uint64_t insns = 0;                  // the instruction count at the call (the virtual time)
		std::string service;                 // "NtOpenFile", ...
		std::string path;                    // the path (the image's directory / the user profile as placeholders) or "<handle ...>"
		std::string args;                    // access / attributes / share / options / class ...
		std::string answer;                  // "status C0000034H" or "STOP (not answered: ...)"
	};

	// P44: the window holding every watched win32k view (fixed placeholder bases inside it; the process layer hooks it once)
	inline constexpr uint64_t win32k_view_window = 0x00007FF000000000ull, win32k_view_window_size = 0x10000000ull;

	class kernel
	{
	public:
		kernel( engine& e, const kernel_facts& f ) : e_( e ), f_( f ) {}

		// U2031: register an image the environment mapped with emu::pe::map at [base, +size) (page protections from the engine
		// regions); map a data view of BYTES (zero-padded to pages) read-only at a free address (top-down like MmCreatePeb's NLS view)
		bool add_image_view( uint64_t base, uint64_t size, const std::string& name, std::string& why );
		bool map_data_view( const std::vector<uint8_t>& bytes, const std::string& name, uint64_t& base, std::string& why );
		const std::map<uint64_t, view>& views() const { return views_; }

		// the call -> the reply (never throws; every guest value is checked)
		reply dispatch( const call& c );

		// the services' virtual-memory core (also the loader's: TLS memory of the --HLE loader is a private allocation)
		reply allocate( uint64_t& base, uint64_t& size, uint32_t type, uint32_t protect, uint64_t limit_low = 0, uint64_t limit_high = 0, uint64_t align = 0 );
		const std::map<uint64_t, allocation>& allocations() const { return allocs_; }
		const kernel_facts& facts() const { return f_; }
		std::vector<std::string> describe() const;

		// a guest buffer as the kernel probes it: inside the user range, mapped, NEED = UC_PROT_READ / WRITE bits
		bool user_buffer( uint64_t address, uint64_t length, uint32_t need ) const;

		// P44 (U2118): the kernel -> user callbacks in progress (innermost last)
		size_t callback_depth() const { return callbacks_.size(); }
		// P44 (U2122): the partly measured regions (the process layer checks every guest read of them)
		const std::vector<watch>& watches() const { return watches_; }
		// P45 (U2128): every file-system access of the emulated program, in order (observation only)
		const std::vector<fs_access>& fs_log() const { return fs_log_; }
		static bool fs_service( const std::string& name );   // a service that names a file / a file handle (recorded in fs_log)
		// P47 (U2166): N random bytes of the environment's seeded generator (CTR_DRBG, plan 3.M6; never host entropy) - the RNG device's
		// answer and the --HLE BCryptGenRandom
		void environment_random( uint8_t* p, size_t n );

	private:
		reply svc_terminate_process( const call& c );
		reply svc_close( const call& c );
		reply svc_allocate_virtual_memory( const call& c );
		reply svc_free_virtual_memory( const call& c );
		reply svc_query_virtual_memory( const call& c );
		reply svc_query_information_process( const call& c );
		reply svc_query_information_thread( const call& c );   // P55 (U2362): ThreadBasicInformation of NtCurrentThread(), measured (tm_nt nqit)
		reply svc_set_thread_affinity( const call& c );        // P56 (U2366): NtSetInformationThread 4 ThreadAffinityMask, measured (tm_nt nsit)
		reply svc_set_thread_hide( const call& c );           // P56 (U2370): NtSetInformationThread 11H ThreadHideFromDebugger, measured (tm_nt nhfd)

		bool put( uint64_t address, const void* data, uint64_t length, reply& r );   // write an OUT parameter (probed before)
		bool get64( uint64_t address, uint64_t& v ) const;
		allocation* owner( uint64_t address, uint64_t length );                    // the allocation holding the whole range
		bool find_free( uint64_t size, bool top_down, uint64_t& base, uint64_t limit_low = 0, uint64_t limit_high = 0, uint64_t align = 0 ) const;
		bool commit_pages( allocation& a, uint64_t first, uint64_t count, uint32_t protect, std::string& why );
		bool decommit_pages( allocation& a, uint64_t first, uint64_t count, std::string& why );

		engine& e_;
		kernel_facts f_;
		std::map<uint64_t, allocation> allocs_;
		std::map<uint64_t, view> views_;     // U2031
		view* view_of( uint64_t address );

	public:
		// U2032 (plan 3.M5 part 2): kernel objects of this process (handle table: 4, 8, ... as Windows hands them out, a multiple
		// of 4 - documented for kernel handles: "the low two bits are reserved"; Wine 11.0 server/handle.c index_to_handle)
		struct object
		{
			std::string kind;                // "event", "section", "directory", "symlink", "key", "file", ...
			std::string name;                // the object name (as opened / created)
			std::string target;              // a symbolic link's target; a section's image file name
			uint64_t value = 0;              // an event's state, a section's size, ...
		};
		const std::map<uint64_t, object>& handles() const { return handles_; }
		uint64_t open_handle( const object& o );

	private:
		std::map<uint64_t, object> handles_;
		uint64_t next_handle_ = 4;
		uint64_t thread_affinity_ = 0;   // P56 (U2366): the main thread's affinity set by NtSetInformationThread 4 (0 = the process mask)
		bool thread_hidden_ = false;   // P56 (U2370): ThreadHideFromDebugger set by NtSetInformationThread 11H (no debugger is emulated; kept for a later query)
		bool dispatch_more( const call& c, reply& r );   // nt_kernel_sys.cpp: the services of the --Sys process start
		reply svc_protect_virtual_memory( const call& c );
		reply svc_event( const call& c );
		reply svc_query_system_information( const call& c );
		reply svc_get_context_thread( const call& c );   // P49 (U2258)
		bool query_process_more( const call& c, reply& r );
		bool query_image_information( const call& c, reply& r );   // P46 (U2142): ProcessImageInformation 25H (nt_kernel_measured.cpp; tm_nt pii, T8)
		reply query_memory_image( const call& c );
		reply query_working_set_ex( const call& c );
		reply svc_open_key( const call& c );
		reply svc_query_value_key( const call& c );
		reply svc_allocate_virtual_memory_ex( const call& c );
		uint32_t page_protect_at( uint64_t address, bool& committed, bool& shared );   // U2033: PAGE_* of a page (0 = free)
		struct object_attributes
		{
			bool present = false;
			uint64_t root = 0;
			uint32_t attributes = 0;
			std::string name;
		};
		bool guest_ustring( uint64_t us, std::string& out, std::string& why );
		bool guest_object_attributes( uint64_t oa, object_attributes& a, std::string& why );
		// P39 (U2050-U2079): the undocumented services of the --Sys loader - phnt prototypes, behaviour measured natively by tm_nt
		// (nt_kernel_measured.cpp; Emulator/data/nt_measured_vmware_guest.txt)
		bool dispatch_measured( const call& c, reply& r );
		reply svc_query_security_attributes_token( const call& c );
		reply svc_create_wait_completion_packet( const call& c );
		reply svc_query_system_information_ex( const call& c );
		reply svc_create_io_completion( const call& c );
		reply svc_create_worker_factory( const call& c );
		reply svc_associate_wait_completion_packet( const call& c );
		reply svc_set_information_worker_factory( const call& c );
		reply svc_trace_control( const call& c );
		reply svc_open_namespace_object( const call& c );
		reply svc_query_symbolic_link( const call& c );
		reply svc_open_section( const call& c );
		reply svc_open_file( const call& c );
		reply svc_map_view_of_section( const call& c );
		reply svc_set_information_process( const call& c );
		reply svc_apphelp_cache_control( const call& c );
		reply svc_set_information_virtual_memory( const call& c );
		reply svc_create_section( const call& c );
		reply svc_query_attributes_file( const call& c );                              // P43 (U2107)
		// P45 (U2126-U2130): the emulated file system (nt_kernel_vfs.cpp) - the image, the captured system files, measured misses
		reply dispatch_one( const call& c );                                           // the services (dispatch() records the file-system ones)
		std::vector<fs_access> fs_log_;
		void fs_record( const call& c, const reply& r );
		std::string fs_shown( const std::string& path ) const;
		bool vfs_classify( const object_attributes& oa, std::string& kind, std::string& ctx, std::string& why ) const;
		reply vfs_answer( const object_attributes& oa, const std::string& op, uint64_t ph, uint64_t iosb, uint64_t info, uint64_t info_len, const std::string& what );
		reply vfs_open_file( const call& c, const object_attributes& oa, const std::string& what );
		reply vfs_create_file( const call& c );
		reply vfs_query_attributes( const call& c, const object_attributes& oa, bool full, const std::string& what );
		reply svc_query_full_attributes_file( const call& c );
		reply svc_query_directory_file( const call& c, bool ex );
		// P47 (U2165-U2166): \Device\KsecDD, the one device of the environment (nt_kernel_vfs.cpp; tm_nt ksec)
		bool ksec_open( const call& c, const object_attributes& oa, const std::string& what, reply& r );
		reply svc_device_io_control_file( const call& c );
		bool ksec_request( const call& c, const std::string& what, reply& r );
		bool cng_request( const call& c, const std::string& what, reply& r );   // P47: \Device\CNG
		// p3cm (U2281): \Device\DeviceApi\CMApi - only the requests of cfgmgr32.dll's start-up (nt_kernel_vfs.cpp; tm_nt cm)
		bool cm_open( const call& c, const object_attributes& oa, const std::string& what, reply& r );
		bool cm_request( const call& c, const std::string& what, reply& r );
		reply svc_query_license_value( const call& c );   // p3cm (U2283): NtQueryLicenseValue - only measured value names (tm_nt lic)
		std::unique_ptr<emu::ctr_drbg> dev_rng_;                                // P47 (U2166): the environment's random-byte stream (environment_random)
		reply svc_read_virtual_memory( const call& c );                                // P45 (U2135): NtReadVirtualMemory of the own process (tm_nt rvm)
		reply svc_add_atom( const call& c );                                           // P45 (U2137): NtAddAtomEx (tm_nt atom)
		struct atom_entry
		{
			std::string name;                                  // as first added (printable ASCII; other characters as ?)
			uint32_t usage = 0;                                // the adds not yet deleted
		};
		std::map<uint32_t, atom_entry> atoms_;                  // P45 (U2137): the emulated window station's atom table (this process' atoms only)
		uint64_t atoms_next_ = 0;
		bool sysdir_file( const object_attributes& oa, std::string& file ) const;     // P43 (U2107): a captured file of the emulated system directory
		reply svc_connect_port( const call& c );
		reply svc_query_volume_information_file( const call& c );
		reply create_measured_object( const call& c, uint64_t handle_ptr, uint32_t access, uint64_t object_attributes, const char* kind, uint64_t value,
									 std::initializer_list<uint32_t> accesses, const char* measured );
		bool query_system_measured( const call& c, reply& r );   // NtQuerySystemInformation classes C5H, 37H
		uint64_t etw_notification_event_ = 0;                   // NtTraceControl 1BH: the registered event (0 = none)
		uint64_t hv_page_ = 0;                                  // the hypervisor shared page (class C5H), mapped at the first query
		// P40 (U2064-U2066): the model of a CONNECTED CSR client (nt_kernel_measured.cpp) - what a process its loader connected to the
		// session's CSR server sees (tm_nt csrc.*), never a CSR server: the API port answer, the shared section's view, the NLS files
		reply map_csr_shared_view( const call& c, const std::string& what );
		reply svc_initialize_nls_files( const call& c );
		bool svc_create_file_measured( const call& c, reply& r );   // NtCreateFile: the console-driver frontier (false = not handled here)
		uint64_t csr_port_ = 0;                                 // the API port handle of this process (0 = not connected)
		// P44 (U2118): kernel -> user callbacks - the service waiting for each one (innermost last); nt_kernel_win32k.cpp
		struct pending_callback
		{
			std::string service;                               // the interrupted service ("NtGdiInit", ...)
			uint32_t api = 0;                                  // the callback number
		};
		std::vector<pending_callback> callbacks_;
		reply begin_callback( const std::string& service, uint32_t api, const std::string& text );
		reply svc_callback_return( const call& c );
		reply complete_callback( const pending_callback& p, uint32_t callback_status );
		bool dispatch_win32k( const call& c, reply& r );       // P44: the win32k services (the second table, win32u.dll stubs)
		std::vector<watch> watches_;                            // P44 (U2122)
		bool map_watched_view( uint64_t base, uint64_t size, uint64_t committed, const std::vector<uint8_t>& content, const std::string& name,
							   const std::vector<std::pair<uint64_t, uint64_t>>& known, std::string& why );
		reply svc_alpc_send_wait_receive_port( const call& c );   // P44 (U2119): the CSR request of user32's start (USERSRV connect)
		bool user_connected_ = false;                           // P44: the USERSRV connection made (the shared-info views mapped)
		std::map<uint64_t, uint32_t> key_tags_;                 // P46 (U2152): HandleTags set on a key handle (NtSetInformationKey 5)
		std::map<uint64_t, uint64_t> key_notify_;               // P46 (U2154): a pending NtNotifyChangeKey: key handle -> its event handle
		std::map<std::string, uint32_t> wm_names_;              // P46 (U2160): registered window-message names -> their atom (placeholders)
		uint32_t wm_next_ = 0xC200;                             // P46 (U2160): the next placeholder message atom (C000H..FFFFH shape)
		uint64_t user_si_view_ = 0, user_he_view_ = 0;          // P44: the user shared-info view (R1), the user handle table view (R2)
		bool gui_process_ = false, gui_thread_ = false;         // P44 (U2120): the GUI conversion of the process / the thread done
		uint64_t gdi_view_ = 0, desk_view_ = 0;                 // P44: the GDI shared memory view (R3), the desktop heap view (R4)
		uint64_t winsta_handle_ = 0, desktop_handle_ = 0;       // P44: the process' window station / the thread's desktop (InitThreadCallback)
		reply gui_convert( const call& c );                     // P44 (U2120): the first win32k service of the thread
		reply svc_unmap_view_of_section( const call& c );       // P44 (U2121): NtUnmapViewOfSection
		bool dispatch_user_start( const call& c, reply& r );    // P44 (U2121): NtDuplicateToken, NtQueryKey 7, ThreadZeroTlsCell
		// P41 (U2069-U2089): the model of a process started WITHOUT a console (a DETACHED_PROCESS start; tm_nt con.*)
		bool console_host_set_ = false;                         // ProcessConsoleHostProcess (31H) set by the process (kernelbase)
		uint64_t console_host_ = 0;                             // its value (measured: 1 in a detached process)
		reply svc_write_file( const call& c );                  // NtWriteFile: the no-console standard handle 0 (tm_nt con.ntwf)
		reply svc_open_process_token( const call& c );          // NtOpenProcessToken (tm_nt tok.open)
		reply svc_query_information_token( const call& c );     // NtQueryInformationToken 0CH / 1DH / 2AH (tm_nt tok.*)
		reply svc_access_check( const call& c );                // NtAccessCheck (P41)
		reply svc_create_mutant( const call& c );               // NtCreateMutant (P41)
		std::map<std::string, uint64_t> named_objects_;         // P41: named objects created in this environment (full name -> handle count)
		std::map<std::string, int64_t> mutant_state_;           // P41: a mutant's signal state (1 = free; <= 0 = owned by the one thread)
		uint64_t unnamed_mutants_ = 0;                          // p3cm (U2288): the unnamed mutants created (their internal names)
		reply svc_mutant_wait_release( const call& c );         // NtWaitForSingleObject on a mutant, NtReleaseMutant (tm_nt mut.*)
		reply svc_named_decode( const call& c );                // P41: decode stop of a named-object service (OA = argument 2)
		reply svc_wil_semaphore( const call& c );               // P41: the WIL staging semaphores (tm_nt wil.*)
		reply svc_query_wnf_state_data( const call& c );        // P41: NtQueryWnfStateData
		reply svc_wnf_more( const call& c );                    // P41: NtSetWnfProcessNotificationEvent, NtSubscribeWnfStateChange
		reply svc_set_timer2( const call& c );                  // P41: NtSetTimer2
		bool exit_status_set_ = false;                           // P41: the first NtTerminateProcess( NULL ) fixed the exit status
		uint32_t exit_status_ = 0;
		uint64_t wnf_event_ = 0;                                // P41: the process' WNF notification event (0 = none)
		bool wil_name( const object_attributes& oa, std::string& full, std::string& why );   // P41: the measured WIL staging name shape
		// U2094 (plan 3.M6): the one virtual time base - NtQueryPerformanceCounter / NtQuerySystemTime read it, waits that time out
		// advance it (NtDelayExecution, NtWaitForSingleObject with a time-out; tm_nt case time)
		reply svc_time( const call& c );
		// the 100-ns interval a LARGE_INTEGER time-out / delay at GUEST pointer P waits (negative relative, positive absolute system
		// time, 0 none); false = a reply to return (bad pointer, a wait that never ends, no clock) in R
		bool wait_interval( uint64_t p, const std::string& what, uint64_t& hns, reply& r );
		bool time_now( uint64_t& qpc, uint64_t& system_time, std::string& why );
		bool time_advance( uint64_t hns, std::string& why );
	};
}
