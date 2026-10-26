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
// Plan 3.M5 part 2 (U2030-U2035; nt_kernel_sys.cpp, each with its source there): NtContinue (a resume of the CONTEXT),
//   NtQueryPerformanceCounter (the virtual clock: one 100 ns tick per instruction, 10 MHz - a stand-in for milestone M6),
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

#include <array>
#include <initializer_list>
#include <cstdint>
#include <map>
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
		uint64_t min_app_address = 0, max_app_address = 0;
		uint32_t page_size = 0x1000, allocation_granularity = 0x10000;
		uint64_t active_processor_mask = 0;
		uint64_t process_id = 0, thread_id = 0, peb = 0, teb = 0;
		uint64_t total_phys = 0;             // U2032: the platform profile's TotalPhys
		uint32_t processors = 0;             // U2032: dwNumberOfProcessors
		uint64_t rng_seed = 0;               // P39 (U2052): engine_config::rng_seed (decision D8) - the deterministic process cookie
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
	enum class reply_kind { done, terminated, stop, resume };
	struct reply
	{
		reply_kind kind = reply_kind::stop;
		uint32_t status = 0;                 // done: the NTSTATUS returned in RAX
		uint32_t exit_status = 0;            // terminated
		uint64_t context = 0;                // resume: the guest CONTEXT record (probed readable, 4D0H bytes)
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

	private:
		reply svc_terminate_process( const call& c );
		reply svc_close( const call& c );
		reply svc_allocate_virtual_memory( const call& c );
		reply svc_free_virtual_memory( const call& c );
		reply svc_query_virtual_memory( const call& c );
		reply svc_query_information_process( const call& c );

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
		bool dispatch_more( const call& c, reply& r );   // nt_kernel_sys.cpp: the services of the --Sys process start
		reply svc_protect_virtual_memory( const call& c );
		reply svc_event( const call& c );
		reply svc_query_system_information( const call& c );
		bool query_process_more( const call& c, reply& r );
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
		reply create_measured_object( const call& c, uint64_t handle_ptr, uint32_t access, uint64_t object_attributes, const char* kind, uint64_t value,
									 std::initializer_list<uint32_t> accesses, const char* measured );
		bool query_system_measured( const call& c, reply& r );   // NtQuerySystemInformation classes C5H, 37H
		uint64_t etw_notification_event_ = 0;                   // NtTraceControl 1BH: the registered event (0 = none)
		uint64_t hv_page_ = 0;                                  // the hypervisor shared page (class C5H), mapped at the first query
	};
}
