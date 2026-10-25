// emu::win (plan 3.M4, ledger U1983): see include/emu/win_env.hpp. Everything the guest observes is written here into guest
// memory from the facts (captures / profiles); no host query, no host system call, no host KUSER page (AGENT_RULES "Host
// safety"; checked by the suites no_native_check and no_host_passthrough).
#include <emu/win_env.hpp>
#include <emu/assemble.hpp>
#include <emu/input_file.hpp>
#include <emu/output_file.hpp>
#include <emu/win_fields.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>    // winnt.h NT_TIB (types only)
#include <winternl.h>   // PEB / TEB as far as the SDK documents them (types only, for the static_asserts below)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

// ---- layout checks against the Windows SDK 10.0.26100.0 headers the compiler sees (um\winnt.h, um\winternl.h)
static_assert( offsetof( NT_TIB, ExceptionList ) == emu::win::layout::teb_exception_list && offsetof( NT_TIB, StackBase ) == emu::win::layout::teb_stack_base &&
				   offsetof( NT_TIB, StackLimit ) == emu::win::layout::teb_stack_limit && offsetof( NT_TIB, Self ) == emu::win::layout::teb_self,
			   "NT_TIB (winnt.h) = the TEB head" );
static_assert( offsetof( TEB, ProcessEnvironmentBlock ) == emu::win::layout::teb_peb && offsetof( TEB, TlsSlots ) == emu::win::layout::teb_tls_slots,
			   "winternl.h TEB" );
static_assert( offsetof( PEB, BeingDebugged ) == emu::win::layout::peb_being_debugged && offsetof( PEB, Ldr ) == emu::win::layout::peb_ldr &&
				   offsetof( PEB, ProcessParameters ) == emu::win::layout::peb_process_parameters,
			   "winternl.h PEB" );
static_assert( IMAGE_FILE_MACHINE_AMD64 == 0x8664 && PROCESSOR_ARCHITECTURE_AMD64 == 9 && VER_PLATFORM_WIN32_NT == 2, "winnt.h constants" );

namespace emu::win
{
	namespace
	{
		std::string hex( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}

		bool align_up( uint64_t v, uint64_t a, uint64_t& out )
		{
			if ( v > UINT64_MAX - ( a - 1 ) ) return false;
			out = ( v + a - 1 ) & ~( a - 1 );
			return true;
		}

		// a segment descriptor (253668-093 Vol3A 3.4.5): limit 15:0, base 23:0, access byte (P, DPL, S, type), flags nibble
		// (G, D/B, L, AVL) + limit 19:16, base 31:24
		uint64_t descriptor( uint32_t base, uint32_t limit, uint8_t access, uint8_t flags_high_nibble )
		{
			return uint64_t( limit & 0xFFFF ) | ( uint64_t( base & 0xFFFFFF ) << 16 ) | ( uint64_t( access ) << 40 ) | ( uint64_t( ( limit >> 16 ) & 0xF ) << 48 ) |
				   ( uint64_t( flags_high_nibble & 0xF0 ) << 48 ) | ( uint64_t( base >> 24 ) << 56 );
		}

		constexpr uint32_t msr_efer = 0xC0000080, msr_star = 0xC0000081, msr_lstar = 0xC0000082, msr_fmask = 0xC0000084;
		// IA32_STAR: SYSCALL CS = 10H / SS = 18H (bits 47:32), SYSRET CS = 23H + 16 = 33H / SS = 23H + 8 = 2BH (bits 63:48)
		constexpr uint64_t star_value = ( uint64_t( layout::gdt_r3_cmcode | layout::rpl3 ) << 48 ) | ( uint64_t( layout::gdt_r0_code ) << 32 );
		// IA32_FMASK: TF, IF, DF, NT cleared at the kernel entry (chosen, as cases_sysmsr_win; R11 restores the user RFLAGS)
		constexpr uint64_t fmask_value = 0x4700;

		uint64_t read_reg( uc_engine* uc, int id )
		{
			uint64_t v = 0;
			uc_reg_read( uc, id, &v );
			return v;
		}
	}

	const char* name_of( stop_kind k )
	{
		switch ( k )
		{
			case stop_kind::terminated: return "terminated";
			case stop_kind::unknown_syscall: return "unknown system service";
			case stop_kind::unimplemented_syscall: return "unimplemented system service";
			case stop_kind::unimplemented_api: return "unimplemented API (--HLE)";
			case stop_kind::fault: return "fault";
			case stop_kind::count: return "instruction bound";
			case stop_kind::returned: return "returned from the entry point";
			case stop_kind::error: return "error";
			default: return "none";
		}
	}

	bool load_environment_facts( environment env, const std::string& capture_dir_explicit, const std::string& data_dir, environment_facts& f,
								 std::vector<std::string>& log, std::string& error )
	{
		f = environment_facts{};
		f.env = env;
		if ( !locate_capture( env, capture_dir_explicit, data_dir, f.capture_dir, error ) ) return false;
		capture_manifest m;
		if ( !load_manifest( f.capture_dir, env, m, error ) ) return false;
		const std::string ntdll_path = ( std::filesystem::path( f.capture_dir ) / "ntdll.dll" ).string();
		std::string bytes;
		if ( !read_input_file( ntdll_path, max_ntdll_bytes, bytes, error ) ) return false;
		if ( bytes.size() != m.ntdll_size )
		{
			error = ntdll_path + " is " + std::to_string( bytes.size() ) + " bytes, the capture manifest says " + std::to_string( m.ntdll_size ) + " - recapture";
			return false;
		}
		std::vector<uint8_t> nt( bytes.begin(), bytes.end() );
		if ( !parse_syscall_table( nt, f.syscalls, error ) )
		{
			error = ntdll_path + ": " + error;
			return false;
		}
		platform_facts p;
		if ( !load_platform_facts( env, data_dir, p, error ) ) return false;
		f.os_major = m.os_major;
		f.os_minor = m.os_minor;
		f.os_build = m.os_build;
		f.product_type = m.product_type;
		f.system_root = m.system_root;
		f.processors = p.processors;
		f.processor_architecture = p.processor_architecture;
		f.total_phys = p.total_phys;
		f.min_app_address = p.min_app_address;
		f.max_app_address = p.max_app_address;
		f.page_size = p.page_size;   // U2011
		f.allocation_granularity = p.allocation_granularity;
		f.active_processor_mask = p.active_processor_mask;
		f.platform_source = p.source;
		log.push_back( std::string( "environment: " ) + name_of( env ) + " (decision 3.0.8: its own DLL capture, local only, never committed)" );
		log.push_back( "  capture: " + f.capture_dir + " (manifest format 1, captured " + m.captured + "; ntdll.dll " + std::to_string( bytes.size() ) +
					   " bytes = the manifest size; the SHA-256 is checked by the harness)" );
		log.push_back( "  syscall table from the capture's ntdll stubs (4C 8B D1 B8 imm32): " + std::to_string( f.syscalls.stubs ) + " services of " +
					   std::to_string( f.syscalls.nt_exports ) + " Nt* exports (plan 6.4.b: never hardcoded)" );
		log.push_back( "  OS facts (manifest): version " + std::to_string( f.os_major ) + "." + std::to_string( f.os_minor ) + " build " + std::to_string( f.os_build ) +
					   ", product type " + std::to_string( f.product_type ) + ", system root " + f.system_root );
		log.push_back( "  platform facts (" + f.platform_source + "): " + std::to_string( f.processors ) + " processors, architecture " +
					   std::to_string( f.processor_architecture ) + ", " + std::to_string( f.total_phys ) + " bytes physical memory, user range [" +
					   hex( f.min_app_address ) + ", " + hex( f.max_app_address ) + "]" );
		log.push_back( "  memory facts (" + f.platform_source + "): page " + hex( f.page_size ) + ", allocation granularity " + hex( f.allocation_granularity ) +
					   ", active processor mask " + hex( f.active_processor_mask ) );
		return true;
	}

	// ------------------------------------------------------------------------------------------------ hooks
	struct process::hook_ctx
	{
		process* p = nullptr;
		run_result* r = nullptr;
		uint64_t last_insn = 0;
		bool stopped = false;
		// U2020: instrumentation (process_options::counters / trace_path)
		bool counting = false;
		bool cpuid_pending = false;
		std::pair<uint32_t, uint32_t> cpuid_key{};
		size_t tail_next = 0;
		std::unique_ptr<output_file> trace;   // the audited writer (no_native_check)
		std::unique_ptr<disassembler> dis;

		void stop( stop_kind k, const std::string& detail )
		{
			if ( stopped ) return;
			stopped = true;
			r->stop = k;
			r->detail = detail;
		}
	};

	namespace
	{
		// U2020: the CPUID answer = the registers before the next instruction (253667-093 Vol2A CPUID "Operation")
		void finish_cpuid( uc_engine* uc, process::hook_ctx& c )
		{
			if ( !c.cpuid_pending ) return;
			c.cpuid_pending = false;
			auto& p = c.r->probes.cpuid[ c.cpuid_key ];
			if ( p.answered ) return;
			p.answered = true;
			p.eax = uint32_t( read_reg( uc, UC_X86_REG_RAX ) );
			p.ebx = uint32_t( read_reg( uc, UC_X86_REG_RBX ) );
			p.ecx = uint32_t( read_reg( uc, UC_X86_REG_RCX ) );
			p.edx = uint32_t( read_reg( uc, UC_X86_REG_RDX ) );
		}

		// U2020: count CPUID / RDTSC / RDTSCP / RDPID from the instruction bytes (after legacy and REX prefixes), keep the tail,
		// write the trace line
		void instrument( uc_engine* uc, uint64_t address, uint32_t size, process::hook_ctx& c )
		{
			finish_cpuid( uc, c );
			uint8_t b[ 16 ] = {};
			const uint32_t n = size > 15 ? 15 : size;
			if ( !n || uc_mem_read( uc, address, b, n ) != UC_ERR_OK ) return;
			auto& pr = c.r->probes;
			if ( c.counting )
			{
				uint32_t i = 0;
				bool rep = false;
				for ( ; i < n; i++ )
				{
					const uint8_t x = b[ i ];
					if ( x == 0xF3 ) rep = true;
					if ( x == 0x66 || x == 0x67 || x == 0xF2 || x == 0xF3 || x == 0xF0 || x == 0x2E || x == 0x36 || x == 0x3E || x == 0x26 || x == 0x64 || x == 0x65 ) continue;
					break;
				}
				if ( i < n && ( b[ i ] & 0xF0 ) == 0x40 ) i++;   // REX
				if ( i + 1 < n && b[ i ] == 0x0F )
				{
					const uint8_t op = b[ i + 1 ];
					if ( op == 0xA2 )
					{
						pr.cpuid_total++;
						c.cpuid_key = { uint32_t( read_reg( uc, UC_X86_REG_RAX ) ), uint32_t( read_reg( uc, UC_X86_REG_RCX ) ) };
						auto& p = pr.cpuid[ c.cpuid_key ];
						if ( !p.count ) p.first_at = address;
						p.leaf = c.cpuid_key.first;
						p.subleaf = c.cpuid_key.second;
						p.count++;
						c.cpuid_pending = true;
					}
					else if ( op == 0x31 )
						pr.rdtsc++;
					else if ( op == 0x01 && i + 2 < n && b[ i + 2 ] == 0xF9 )
						pr.rdtscp++;
					else if ( op == 0xC7 && rep && i + 2 < n && ( b[ i + 2 ] & 0xF8 ) == 0xF8 )
						pr.rdpid++;   // F3 0F C7 /7, ModRM.mod = 11B
				}
				if ( c.p->tail_capacity() )
				{
					auto& t = pr.tail;
					trace_tail_entry e;
					e.index = c.r->insns;
					e.rip = address;
					e.size = n;
					std::memcpy( e.bytes, b, n );
					if ( t.size() < c.p->tail_capacity() ) t.push_back( e );
					else t[ c.tail_next ] = e;
					c.tail_next = ( c.tail_next + 1 ) % c.p->tail_capacity();
				}
			}
			if ( c.trace && !pr.trace_truncated )
			{
				std::string l = std::to_string( c.r->insns ) + " " + hex( address ) + " " + c.p->where( address ) + " ";
				for ( uint32_t k = 0; k < n; k++ )
				{
					char h[ 4 ];
					std::snprintf( h, sizeof( h ), "%02x", b[ k ] );
					l += h;
				}
				if ( c.dis && c.dis->ok() ) l += "  " + c.dis->decode( b, n, address ).text;
				l += "\n";
				if ( c.trace->write( l ) )
				{
					pr.trace_bytes += l.size();
					pr.trace_lines++;
				}
				else
					pr.trace_truncated = true;
			}
		}

		void on_code( uc_engine* uc, uint64_t address, uint32_t size, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			c.r->insns++;
			c.last_insn = address;
			if ( size == 2 )
			{
				uint8_t b[ 2 ] = {};
				if ( uc_mem_read( uc, address, b, 2 ) == UC_ERR_OK && b[ 0 ] == 0x0F && b[ 1 ] == 0x05 ) c.r->syscall_insns++;
			}
			if ( c.counting || c.trace ) instrument( uc, address, size, c );
		}

		// U2020: a guest read of PEB / TEB / KUSER_SHARED_DATA (and the other structures the environment builds), named per member
		void on_env_read( uc_engine*, uc_mem_type, uint64_t address, int size, int64_t, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			std::string region, field;
			if ( !c.p->name_field( address, region, field ) ) return;
			auto& pr = c.r->probes;
			pr.reads_total++;
			auto& rp = pr.reads[ region + "." + field ];
			if ( !rp.count )
			{
				rp.region = region;
				rp.field = field;
				rp.first_at = c.last_insn;
			}
			rp.count++;
			(void)size;
		}

		void on_intr( uc_engine* uc, uint32_t intno, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			uc_x86_exception x{};
			int64_t ec = -1;
			if ( uc_ctl_get_x86_exception( uc, &x ) == UC_ERR_OK && x.vector == int32_t( intno ) && x.has_error_code ) ec = x.error_code;
			c.r->fault_vector = int( intno );
			c.r->fault_error_code = ec;
			static const char* const names[ 20 ] = { "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM", "#DF", "", "#TS", "#NP", "#SS", "#GP", "#PF", "", "#MF", "#AC", "#MC", "#XM" };
			const std::string mnem = intno < 20 && names[ intno ][ 0 ] ? std::string( names[ intno ] ) + " " : std::string();
			c.stop( stop_kind::fault, mnem + "interrupt / exception vector " + std::to_string( intno ) + ( ec >= 0 ? " error code " + hex( uint64_t( ec ) ) : std::string() ) +
										  " at rip " + hex( read_reg( uc, UC_X86_REG_RIP ) ) +
										  ( intno == 0x2E ? " (INT 2EH system call: not a path of milestone M4; stopped, never forwarded)" : "" ) +
										  " - exception dispatch (KiUserExceptionDispatcher) is milestone M7" );
			uc_emu_stop( uc );
		}

		bool on_invalid( uc_engine* uc, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			c.r->fault_vector = 6;
			c.stop( stop_kind::fault, "#UD (invalid opcode) at rip " + hex( read_reg( uc, UC_X86_REG_RIP ) ) + " - exception dispatch is milestone M7" );
			return false;
		}

		bool on_bad_mem( uc_engine* uc, uc_mem_type type, uint64_t address, int, int64_t, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			const bool fetch = type == UC_MEM_FETCH_UNMAPPED || type == UC_MEM_FETCH_PROT;
			const bool write = type == UC_MEM_WRITE_UNMAPPED || type == UC_MEM_WRITE_PROT;
			c.r->fault_vector = 14;
			c.r->fault_address = address;
			c.stop( stop_kind::fault, std::string( "#PF " ) + ( fetch ? "execute" : write ? "write" : "read" ) + " access to " + hex( address ) +
										  ( type == UC_MEM_FETCH_PROT || type == UC_MEM_READ_PROT || type == UC_MEM_WRITE_PROT ? " (against the page's protection)" : " (not mapped)" ) +
										  " at rip " + hex( read_reg( uc, UC_X86_REG_RIP ) ) + " - exception dispatch is milestone M7" );
			return false;
		}

		// the emulated kernel: runs after the SDM SYSCALL transition (fork U901), in the kernel-entry state
		void on_syscall( uc_engine* uc, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			run_result& r = *c.r;
			r.syscalls_handled++;
			syscall_record s;
			s.index = r.syscalls_handled;
			s.at = c.last_insn;
			s.rax = read_reg( uc, UC_X86_REG_RAX );
			s.number = uint32_t( s.rax & 0xFFFF );
			const uint64_t rcx = read_reg( uc, UC_X86_REG_RCX ), r11 = read_reg( uc, UC_X86_REG_R11 ), rsp = read_reg( uc, UC_X86_REG_RSP );
			s.args = { read_reg( uc, UC_X86_REG_R10 ), read_reg( uc, UC_X86_REG_RDX ), read_reg( uc, UC_X86_REG_R8 ), read_reg( uc, UC_X86_REG_R9 ), 0, 0 };
			uint64_t stack[ 2 ] = {};
			std::string err;
			if ( rsp <= UINT64_MAX - 0x38 && c.p->eng().read( rsp + 0x28, stack, sizeof( stack ), err ) )
			{
				s.args[ 4 ] = stack[ 0 ];
				s.args[ 5 ] = stack[ 1 ];
				s.stack_args_read = true;
			}
			r.user_rip = s.at;
			r.user_rflags = r11;
			r.user_rsp = rsp;
			const auto& tab = c.p->syscalls();
			const auto it = s.number <= 0xFFF ? tab.by_number.find( s.number ) : tab.by_number.end();
			if ( s.at + 2 != rcx ) s.outcome = "(RCX " + hex( rcx ) + " is not the SYSCALL address + 2) ";
			if ( it == tab.by_number.end() )
			{
				s.outcome += "unknown: no stub of the environment's ntdll loads this number" + std::string( s.number > 0xFFF ? " (bits 15:12 set: not the NT table)" : "" );
				r.syscalls.push_back( s );
				c.stop( stop_kind::unknown_syscall, "system service EAX AND FFFFH = " + hex( s.number ) + " (RAX " + hex( s.rax ) + ") at rip " + hex( s.at ) +
														" is unknown in the environment's syscall table - stopped with a state dump, nothing forwarded to the host" );
				uc_emu_stop( uc );
				return;
			}
			s.name = it->second;
			// plan 3.M5 (U2012): the one kernel layer answers, terminates or stops loudly (emu::nt::kernel::dispatch)
			nt::call call;
			call.number = s.number;
			call.name = s.name;
			call.args = s.args;
			call.stack_args = s.stack_args_read;
			const nt::reply rep = c.p->kern().dispatch( call );
			s.writes = rep.writes;
			if ( rep.kind == nt::reply_kind::done )
			{
				const uint64_t st = rep.status;   // NTSTATUS in EAX, the upper half of RAX zero
				uc_reg_write( uc, UC_X86_REG_RAX, &st );
				char b[ 16 ];
				std::snprintf( b, sizeof( b ), "%08X", rep.status );
				s.answered = true;
				s.status = rep.status;
				s.outcome += std::string( "status " ) + b + "H: " + rep.text;
				r.syscalls.push_back( s );
				return;   // the SYSRETQ at IA32_LSTAR returns to the caller
			}
			if ( rep.kind == nt::reply_kind::terminated )
			{
				r.exit_status = rep.exit_status;
				s.outcome += rep.text;
				r.syscalls.push_back( s );
				c.stop( stop_kind::terminated, "NtTerminateProcess( NtCurrentProcess(), " + hex( r.exit_status ) + " ) at rip " + hex( s.at ) +
												   " - the emulated process ended; the host process is not affected" );
				uc_emu_stop( uc );
				return;
			}
			s.outcome += "not answered: " + rep.text;
			r.syscalls.push_back( s );
			c.stop( stop_kind::unimplemented_syscall, s.name + " (" + hex( s.number ) + ") at rip " + hex( s.at ) + ": " + rep.text +
														  " - stopped with a state dump, nothing forwarded" );
			uc_emu_stop( uc );
		}

		// plan 3.M5 (U2012): an --HLE API stub was reached (emu::hle)
		void on_hle( uc_engine* uc, uint64_t address, uint32_t, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			if ( c.stopped ) return;
			run_result& r = *c.r;
			if ( address >= hle::trampoline_va ) return;   // the TLS start trampoline is ordinary user code
			api_record a;
			a.index = r.api_calls.size() + 1;
			a.stub = address;
			const hle::result res = c.p->hle().call( address, uc, c.p->kern(), c.p->image_base(), a.api, a.args );
			a.outcome = res.text;
			if ( res.kind == hle::result_kind::returned )
			{
				a.rax = res.rax;
				uc_reg_write( uc, UC_X86_REG_RAX, &a.rax );
				r.api_calls.push_back( a );
				return;   // the stub's RET returns to the caller
			}
			r.api_calls.push_back( a );
			if ( res.kind == hle::result_kind::terminated )
			{
				r.exit_status = res.exit_status;
				c.stop( stop_kind::terminated, a.api + "( " + hex( res.exit_status ) + " ) - the emulated process ended through the kernel layer; the host process is not affected" );
			}
			else
				c.stop( stop_kind::unimplemented_api, a.api + " at " + hex( address ) + ": " + res.text + " - stopped with a state dump" );
			uc_emu_stop( uc );
		}
	}

	// ------------------------------------------------------------------------------------------------ process
	process::~process() = default;

	std::unique_ptr<process> process::create( const engine_config& ec, const environment_facts& facts, const process_options& po,
											  const std::vector<uint8_t>& pe_file, std::vector<std::string>& log, std::string& error )
	{
		std::unique_ptr<process> p( new process() );
		p->facts_ = facts;
		p->po_ = po;
		p->log_.push_back( std::string( "boundary: --" ) + name_of( po.boundary ) +
						   ( po.boundary == env_boundary::sys
								 ? " (D3, plan 6.1: the real system DLLs run in the engine, emulated at the system-call boundary; milestone M5 maps no "
								   "system DLL - an image with imports / TLS is refused (P35 open item); direct system calls go to the one kernel layer below)"
								 : " (D3, plan 6.1: API-level reimplementation of the system DLLs; milestone M5 binds the imports to the emu::hle stubs "
								   "(ExitProcess = the kernel layer's NtTerminateProcess); direct system calls go to the same kernel layer as --Sys)" ) );
		p->e_ = engine::open( ec, error );
		if ( !p->e_ )
		{
			log = p->log_;
			return nullptr;
		}
		for ( auto& l : p->e_->log() ) p->log_.push_back( "engine: " + l );
		const bool ok = p->build( facts, po, pe_file, error );
		log = p->log_;
		if ( !ok ) return nullptr;
		return p;
	}

	bool process::build( const environment_facts& f, const process_options& po, const std::vector<uint8_t>& file, std::string& error )
	{
		auto refuse = [ & ]( const std::string& why ) {
			error = why;
			log_.push_back( "REFUSED: " + why );
			return false;
		};
		if ( f.env == environment::none ) return refuse( "no Windows environment (a PE run needs --guest or --host, decision 3.0.8)" );
		if ( !pe::parse( file, img_, error ) ) return refuse( error );
		// plan 3.M5 (U2012): imports and TLS. --HLE: the redirect and the loader's TLS start below. --Sys: the process would start
		// in the environment's real ntdll!LdrInitializeThunk, whose initialisation of the loader, the API set map, the NLS tables
		// and kernelbase's CSR connection needs kernel-side state that no cited source documents - refused, not guessed.
		if ( img_.dirs[ IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT ].rva || img_.dirs[ IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT ].size )
			return refuse( po.image_name + " has delay imports - not modelled in milestone M5 (either mode)" );
		const bool has_imports = img_.dirs[ IMAGE_DIRECTORY_ENTRY_IMPORT ].rva || img_.dirs[ IMAGE_DIRECTORY_ENTRY_IMPORT ].size;
		const bool has_tls = img_.dirs[ IMAGE_DIRECTORY_ENTRY_TLS ].rva || img_.dirs[ IMAGE_DIRECTORY_ENTRY_TLS ].size;
		std::vector<pe::import_module> imports;
		if ( po.boundary == env_boundary::sys && ( has_imports || has_tls ) )
		{
			std::string what, why;
			if ( has_imports && pe::imports( file, img_, imports, why ) )
				for ( auto& m : imports ) what += ( what.empty() ? "" : ", " ) + m.dll;
			return refuse( "--Sys: " + po.image_name + ( has_imports ? " imports" + ( what.empty() ? std::string( " (a malformed import directory)" ) : " " + what ) : std::string() ) +
						   ( has_imports && has_tls ? " and" : "" ) + ( has_tls ? " has a TLS directory" : "" ) +
						   ": under --Sys the process starts in the environment's real ntdll!LdrInitializeThunk (plan 6.1), which needs the kernel's "
						   "process-creation state - PEB members beyond the documented layouts (the API set map, the NLS code-page tables), the "
						   "loader's initial thread context, and kernelbase's CSR / BaseSrv connection - for which the project has no cited source; not "
						   "modelled in milestone M5 (user decision pending, notes\\eread\\P35_phase3_m5.md). --HLE runs this image." );
		}
		if ( !pe::imports( file, img_, imports, error ) ) return refuse( error );
		if ( img_.file_characteristics & IMAGE_FILE_DLL ) return refuse( po.image_name + " is a DLL (IMAGE_FILE_DLL): the process image must be an executable" );
		if ( !img_.entry_rva ) return refuse( po.image_name + " has no entry point (AddressOfEntryPoint 0)" );

		// the image, as data
		const uint64_t base = po.image_base_given ? po.image_base : img_.image_base;
		uint64_t size = 0;
		align_up( img_.size_of_image, page_size, size );
		if ( base < f.min_app_address || base > f.max_app_address || size - 1 > f.max_app_address - base )
			return refuse( "image range [" + hex( base ) + ", +" + hex( size ) + ") outside the user address range [" + hex( f.min_app_address ) + ", " +
						   hex( f.max_app_address ) + "] of the platform profile" );
		if ( !pe::map( *e_, file, img_, base, po.image_name, map_, error ) )
		{
			for ( auto& l : map_.log ) log_.push_back( "pe: " + l );
			return refuse( error );
		}
		for ( auto& l : map_.log ) log_.push_back( "pe: " + l );

		// the stack: SizeOfStackReserve, fully committed (no guard page: milestone M7)
		uint64_t reserve = img_.stack_reserve;
		if ( !reserve ) return refuse( "SizeOfStackReserve 0" );
		if ( !align_up( reserve, page_size, reserve ) || reserve > guest_stack_max_reserve )
			return refuse( "SizeOfStackReserve " + hex( img_.stack_reserve ) + " above the M4 stack area " + hex( guest_stack_max_reserve ) );
		stack_reserve_ = reserve;
		stack_base_ = guest_stack_top;
		const uint64_t stack_low = guest_stack_top - reserve;
		std::string err;
		auto map = [ & ]( uint64_t va, uint64_t n, uint32_t prot, const std::string& name ) {
			if ( !e_->map( va, n, prot, name, err ) ) return refuse( err );
			return true;
		};
		if ( !map( stack_low, reserve, UC_PROT_READ | UC_PROT_WRITE, "stack (main thread, fully committed)" ) ||
			 !map( guest_peb_va, page_size, UC_PROT_READ | UC_PROT_WRITE, "PEB" ) || !map( guest_teb_va, guest_teb_size, UC_PROT_READ | UC_PROT_WRITE, "TEB (main thread)" ) ||
			 !map( guest_kuser_va, page_size, UC_PROT_READ, "KUSER_SHARED_DATA (synthesized, read-only)" ) ||
			 !map( guest_kernel_gdt_va, page_size, UC_PROT_READ, "emulator kernel: GDT" ) ||
			 !map( guest_kernel_entry_va, page_size, UC_PROT_READ | UC_PROT_EXEC, "emulator kernel: system-call entry (SYSRETQ) + start stub" ) )
			return false;

		auto w = [ & ]( uint64_t va, const void* data, uint64_t n, const std::string& label, const std::string& shown ) {
			if ( !e_->write( va, data, n, err ) ) return refuse( "write " + label + ": " + err );
			if ( !label.empty() ) env_lines_.push_back( label + " = " + shown );
			return true;
		};
		auto w64 = [ & ]( uint64_t va, uint64_t v, unsigned bytes, const std::string& label ) { return w( va, &v, bytes, label, hex( v ) ); };

		// GDT (ksamd64.inc KGDT64_*; descriptor format 253668-093 Vol3A 3.4.5, L flag p. 3-12)
		const uint64_t gdt[] = { 0, 0,
								 descriptor( 0, 0xFFFFF, 0x9B, 0xA0 ),    // 10H R0 code: P, DPL 0, S, type B; G, L (64-bit)
								 descriptor( 0, 0xFFFFF, 0x93, 0xC0 ),    // 18H R0 data: type 3; G, B
								 descriptor( 0, 0xFFFFF, 0xFB, 0xC0 ),    // 20H R3 compatibility code: DPL 3; G, D
								 descriptor( 0, 0xFFFFF, 0xF3, 0xC0 ),    // 28H R3 data
								 descriptor( 0, 0xFFFFF, 0xFB, 0xA0 ),    // 30H R3 64-bit code: G, L
								 0, 0, 0,                                 // 38H, 40H / 48H (system TSS: no LTR in M4)
								 descriptor( 0, 0xFFF, 0xF3, 0x40 ) };    // 50H R3 compatibility-mode TEB: base 0 (no 32-bit TEB), B
		static_assert( sizeof( gdt ) == layout::gdt_limit + 1, "GDT size = limit + 1" );
		if ( !w( guest_kernel_gdt_va, gdt, sizeof( gdt ), "", "" ) ) return false;
		log_.push_back( "GDT at " + hex( guest_kernel_gdt_va ) + " limit " + hex( layout::gdt_limit ) + ": R0 code 10H, R0 data 18H, R3 compat code 20H, R3 data 28H, "
						"R3 code 30H, R3 compat TEB 50H (ksamd64.inc KGDT64_*)" );

		// the kernel page: +0 = IA32_LSTAR target (SYSRETQ), +100H = the start stub (DS / ES / GS := 2BH, FS := 53H, SYSRETQ)
		const uint8_t sysretq[] = { 0x48, 0x0F, 0x07 };
		const uint8_t stub[] = { 0x66, 0xB8, 0x2B, 0x00,   // mov ax, 2BH
								 0x8E, 0xD8,               // mov ds, ax
								 0x8E, 0xC0,               // mov es, ax
								 0x8E, 0xE8,               // mov gs, ax   (the GS base is set to the TEB afterwards: Vol3A 3.4.4)
								 0x66, 0xB8, 0x53, 0x00,   // mov ax, 53H
								 0x8E, 0xE0 };             // mov fs, ax
		start_stub_ = guest_kernel_entry_va + 0x100;
		start_sysret_ = start_stub_ + sizeof( stub );
		if ( !w( guest_kernel_entry_va, sysretq, sizeof( sysretq ), "", "" ) || !w( start_stub_, stub, sizeof( stub ), "", "" ) ||
			 !w( start_sysret_, sysretq, sizeof( sysretq ), "", "" ) )
			return false;

		// KUSER_SHARED_DATA (WDK km\ntddk.h C_ASSERT offsets; values from the facts; everything else 0)
		const uint64_t k = guest_kuser_va;
		std::vector<uint16_t> root;
		for ( char ch : f.system_root.substr( 0, size_t( layout::kuser_system_root_chars - 1 ) ) ) root.push_back( uint16_t( uint8_t( ch ) < 0x80 ? ch : '?' ) );
		root.push_back( 0 );
		const uint64_t phys_pages = f.total_phys / page_size;
		if ( !w64( k + layout::kuser_image_number_low, IMAGE_FILE_MACHINE_AMD64, 2, "KUSER ImageNumberLow (2CH)" ) ||
			 !w64( k + layout::kuser_image_number_high, IMAGE_FILE_MACHINE_AMD64, 2, "KUSER ImageNumberHigh (2EH)" ) ||
			 !w( k + layout::kuser_system_root, root.data(), root.size() * 2, "KUSER NtSystemRoot (30H)", f.system_root ) ||
			 !w64( k + layout::kuser_build_number, f.os_build, 4, "KUSER NtBuildNumber (260H)" ) ||
			 !w64( k + layout::kuser_product_type, f.product_type, 4, "KUSER NtProductType (264H)" ) ||
			 !w64( k + layout::kuser_product_type_is_valid, 1, 1, "KUSER ProductTypeIsValid (268H)" ) ||
			 !w64( k + layout::kuser_native_processor_architecture, f.processor_architecture, 2, "KUSER NativeProcessorArchitecture (26AH)" ) ||
			 !w64( k + layout::kuser_major_version, f.os_major, 4, "KUSER NtMajorVersion (26CH)" ) ||
			 !w64( k + layout::kuser_minor_version, f.os_minor, 4, "KUSER NtMinorVersion (270H)" ) ||
			 !w64( k + layout::kuser_kd_debugger_enabled, 0, 1, "KUSER KdDebuggerEnabled (2D4H)" ) ||
			 !w64( k + layout::kuser_number_of_physical_pages, phys_pages, 4, "KUSER NumberOfPhysicalPages (2E8H)" ) ||
			 !w64( k + layout::kuser_safe_boot_mode, 0, 1, "KUSER SafeBootMode (2ECH)" ) ||
			 !w64( k + layout::kuser_system_call, 0, 4, "KUSER SystemCall (308H, 0 = the stubs use SYSCALL)" ) ||
			 !w64( k + layout::kuser_active_processor_count, f.processors, 4, "KUSER ActiveProcessorCount (3C0H)" ) ||
			 !w64( k + layout::kuser_active_group_count, 1, 1, "KUSER ActiveGroupCount (3C4H)" ) )
			return false;
		env_lines_.push_back( "KUSER TickCountMultiplier / InterruptTime / SystemTime / TimeZoneBias / TickCount = 0 (the virtual time base is milestone M6)" );
		env_lines_.push_back( "KUSER ProcessorFeatures[64] = 0 (from the CPUID profile with their first consumer; none in milestone M5)" );

		// PEB
		const uint64_t pb = guest_peb_va;
		if ( !w64( pb + layout::peb_being_debugged, 0, 1, "PEB BeingDebugged (2H)" ) || !w64( pb + layout::peb_image_base_address, base, 8, "PEB ImageBaseAddress (10H)" ) ||
			 !w64( pb + layout::peb_ldr, 0, 8, "PEB Ldr (18H, not modelled in M5: --HLE keeps no loader lists; the --Sys loader start is the P35 open item)" ) ||
			 !w64( pb + layout::peb_process_parameters, 0, 8, "PEB ProcessParameters (20H, not modelled in M5)" ) ||
			 !w64( pb + layout::peb_process_heap, 0, 8, "PEB ProcessHeap (30H, not modelled in M5)" ) ||
			 !w64( pb + layout::peb_os_major_version, f.os_major, 4, "PEB OSMajorVersion (118H)" ) ||
			 !w64( pb + layout::peb_os_minor_version, f.os_minor, 4, "PEB OSMinorVersion (11CH)" ) ||
			 !w64( pb + layout::peb_os_build_number, f.os_build, 2, "PEB OSBuildNumber (120H)" ) ||
			 !w64( pb + layout::peb_os_csd_version, 0, 2, "PEB OSCSDVersion (122H)" ) ||
			 !w64( pb + layout::peb_os_platform_id, VER_PLATFORM_WIN32_NT, 4, "PEB OSPlatformId (124H)" ) )
			return false;

		// TEB
		const uint64_t tb = guest_teb_va;
		if ( !w64( tb + layout::teb_exception_list, 0, 8, "TEB NtTib.ExceptionList (0H)" ) || !w64( tb + layout::teb_stack_base, stack_base_, 8, "TEB NtTib.StackBase (8H)" ) ||
			 !w64( tb + layout::teb_stack_limit, stack_low, 8, "TEB NtTib.StackLimit (10H, fully committed)" ) ||
			 !w64( tb + layout::teb_self, tb, 8, "TEB NtTib.Self (30H)" ) || !w64( tb + layout::teb_client_id, guest_process_id, 8, "TEB ClientId.UniqueProcess (40H)" ) ||
			 !w64( tb + layout::teb_client_id + 8, guest_thread_id, 8, "TEB ClientId.UniqueThread (48H)" ) ||
			 !w64( tb + layout::teb_tls_pointer, 0, 8, "TEB ThreadLocalStoragePointer (58H, 0 until a TLS directory is set up)" ) ||
			 !w64( tb + layout::teb_peb, pb, 8, "TEB ProcessEnvironmentBlock (60H)" ) || !w64( tb + layout::teb_last_error, 0, 4, "TEB LastErrorValue (68H)" ) ||
			 !w64( tb + layout::teb_deallocation_stack, stack_low, 8, "TEB DeallocationStack (1478H)" ) )
			return false;

		// the entry frame: RSP = 8 mod 16 as after a CALL, [RSP] = the return sentinel (never mapped)
		user_rsp_ = guest_stack_top - 0x48;
		if ( !w64( user_rsp_, guest_return_sentinel, 8, "stack [RSP] at the entry (return sentinel, never mapped)" ) ) return false;
		env_lines_.push_back( "RSP at the entry = " + hex( user_rsp_ ) + ", GS base = TEB " + hex( tb ) + ", FS base 0" );

		// CPU: GDTR, the SYSCALL MSRs (IA32_EFER.SCE is set by the engine's 64-bit reset state, fork U594)
		uc_engine* uc = e_->handle();
		uc_x86_mmr g{ 0, guest_kernel_gdt_va, uint32_t( layout::gdt_limit ), 0 };
		if ( uc_reg_write( uc, UC_X86_REG_GDTR, &g ) != UC_ERR_OK ) return refuse( "GDTR write failed" );
		for ( auto [ id, v ] : { std::pair<uint32_t, uint64_t>{ msr_star, star_value }, { msr_lstar, guest_kernel_entry_va }, { msr_fmask, fmask_value } } )
		{
			uc_x86_msr m{ id, v };
			if ( uc_reg_write( uc, UC_X86_REG_MSR, &m ) != UC_ERR_OK ) return refuse( "WRMSR " + hex( id ) + " failed" );
		}
		uc_x86_msr efer{ msr_efer, 0 };
		uc_reg_read( uc, UC_X86_REG_MSR, &efer );
		if ( !( efer.value & 1 ) ) return refuse( "IA32_EFER.SCE is 0 in the engine's reset state" );
		log_.push_back( "CPU: GDTR " + hex( guest_kernel_gdt_va ) + "/" + hex( layout::gdt_limit ) + ", IA32_STAR " + hex( star_value ) + ", IA32_LSTAR " +
						hex( guest_kernel_entry_va ) + " (SYSRETQ), IA32_FMASK " + hex( fmask_value ) + " (chosen), IA32_EFER " + hex( efer.value ) + " (SCE 1)" );
		// plan 3.M5 (U2007 / U2012): the one kernel layer of this process
		nt::kernel_facts kf;
		kf.min_app_address = f.min_app_address;
		kf.max_app_address = f.max_app_address;
		kf.page_size = f.page_size;
		kf.allocation_granularity = f.allocation_granularity;
		kf.active_processor_mask = f.active_processor_mask;
		kf.process_id = guest_process_id;
		kf.thread_id = guest_thread_id;
		kf.peb = pb;
		kf.teb = tb;
		kernel_ = std::make_unique<nt::kernel>( *e_, kf );
		hle_ = std::make_unique<hle::module>();
		start_va_ = map_.entry;
		if ( !imports.empty() )   // --HLE (U2009): the imports bound to the API stubs
		{
			std::vector<std::string> blog;
			if ( !hle_->bind( *e_, imports, base, blog, err ) ) return refuse( "binding the imports: " + err );
			for ( auto& l : blog ) log_.push_back( l );
			size_t impl = 0;
			for ( auto& b : hle_->bindings() ) impl += b.id != hle::api::unknown;
			env_lines_.push_back( "imports (--HLE): " + std::to_string( imports.size() ) + " module(s), " + std::to_string( hle_->bindings().size() ) + " entries bound to the "
								  "API stubs at " + hex( hle::module_va ) + ", " + std::to_string( impl ) + " of them implemented" );
			for ( auto& b : hle_->bindings() ) env_lines_.push_back( "import " + b.label() + " -> " + hex( b.stub ) + ( b.id == hle::api::unknown ? " (not implemented)" : "" ) );
		}
		if ( has_tls )   // --HLE (U2010): the loader's TLS start - index 0, the thread's block from the template, the callbacks
		{
			if ( !pe::tls( *e_, img_, map_, tls_, err ) ) return refuse( err );
			const uint64_t tmpl = tls_.end - tls_.start, block = tmpl + tls_.zero_fill;
			uint64_t bb = 0, bs = block ? block : 1, sb = 0, ss = 0x1000;
			const nt::reply ra = kernel_->allocate( bb, bs, nt::mem::commit | nt::mem::reserve, nt::page::readwrite );
			const nt::reply rs = kernel_->allocate( sb, ss, nt::mem::commit | nt::mem::reserve, nt::page::readwrite );
			if ( ra.kind != nt::reply_kind::done || ra.status || rs.kind != nt::reply_kind::done || rs.status )
				return refuse( "TLS memory: " + ra.text + " / " + rs.text );
			std::vector<uint8_t> t( size_t( tmpl ), 0 );
			if ( ( tmpl && ( !e_->read( tls_.start, t.data(), tmpl, err ) || !e_->write( bb, t.data(), tmpl, err ) ) ) ) return refuse( "TLS template copy: " + err );
			const uint32_t index = 0;   // the executable's TLS index: the first (and only) module with TLS
			if ( !e_->write( tls_.index_va, &index, 4, err ) || !e_->write( sb, &bb, 8, err ) ) return refuse( "TLS index / slot: " + err );
			if ( !w64( tb + layout::teb_tls_pointer, sb, 8, "TEB ThreadLocalStoragePointer (58H, the TLS slot array; slot 0 = the executable's block)" ) ) return false;
			env_lines_.push_back( "TLS (--HLE): template [" + hex( tls_.start ) + ", " + hex( tls_.end ) + ") + " + hex( tls_.zero_fill ) + " zero bytes -> block " + hex( bb ) +
								  ", index 0 written to " + hex( tls_.index_va ) + ", " + std::to_string( tls_.callbacks.size() ) + " callback(s) before the entry" );
			if ( !tls_.callbacks.empty() )
			{
				if ( !hle_->write_trampoline( *e_, base, tls_.callbacks, map_.entry, start_va_, err ) ) return refuse( "TLS start trampoline: " + err );
				for ( auto cb : tls_.callbacks ) env_lines_.push_back( "TLS callback " + hex( cb ) + " ( DllHandle " + hex( base ) + ", DLL_PROCESS_ATTACH, 0 ) from the start trampoline " +
																	   hex( start_va_ ) );
			}
		}
		log_.push_back( "environment built: image " + hex( base ) + " entry " + hex( map_.entry ) + ", PEB " + hex( pb ) + ", TEB " + hex( tb ) + ", KUSER " + hex( k ) +
						", stack [" + hex( stack_low ) + ", " + hex( stack_base_ ) + "), " + std::to_string( env_lines_.size() ) + " environment fields" +
						( start_va_ != map_.entry ? ", user start " + hex( start_va_ ) + " (TLS trampoline)" : "" ) );
		hc_ = std::make_unique<hook_ctx>();
		hc_->p = this;
		return true;
	}

	bool process::run( run_result& r, std::string& error )
	{
		r = run_result{};
		uc_engine* uc = e_->handle();
		hc_->r = &r;
		hc_->stopped = false;
		// 1. the start stub at CPL 0: DS / ES / GS / FS selectors, up to (not including) the SYSRETQ
		uc_err ue = e_->emulate( start_stub_, start_sysret_, 16 );
		if ( ue != UC_ERR_OK || read_reg( uc, UC_X86_REG_RIP ) != start_sysret_ )
		{
			error = std::string( "start stub failed: " ) + uc_strerror( ue ) + ", rip " + hex( read_reg( uc, UC_X86_REG_RIP ) );
			log_.push_back( "FAILED: " + error );
			return false;
		}
		// 2. the register state SYSRETQ hands to user mode (253667-093 Vol2B SYSRET p. 4-710: RIP := RCX, RFLAGS := (R11 & 3C7FD7H) | 2,
		//    CS := IA32_STAR[63:48] + 16 OR 3, SS := IA32_STAR[63:48] + 8 OR 3, CPL := 3); GS base = TEB after the GS load
		const std::pair<int, uint64_t> regs[] = { { UC_X86_REG_GS_BASE, guest_teb_va }, { UC_X86_REG_FS_BASE, 0 }, { UC_X86_REG_RCX, start_va_ },
												  { UC_X86_REG_R11, guest_initial_rflags }, { UC_X86_REG_RSP, user_rsp_ } };
		for ( auto& [ id, v ] : regs )
			if ( uc_reg_write( uc, id, &v ) != UC_ERR_OK )
			{
				error = "register write before the SYSRETQ failed";
				log_.push_back( "FAILED: " + error );
				return false;
			}
		// 3. hooks: the kernel (SYSCALL), faults, the instruction counter
		uc_hook h{};
		if ( uc_hook_add( uc, &h, UC_HOOK_INSN, (void*)on_syscall, hc_.get(), 1, 0, UC_X86_INS_SYSCALL ) != UC_ERR_OK ||
			 uc_hook_add( uc, &h, UC_HOOK_CODE, (void*)on_code, hc_.get(), 1, 0 ) != UC_ERR_OK ||
			 uc_hook_add( uc, &h, UC_HOOK_INTR, (void*)on_intr, hc_.get(), 1, 0 ) != UC_ERR_OK ||
			 uc_hook_add( uc, &h, UC_HOOK_INSN_INVALID, (void*)on_invalid, hc_.get(), 1, 0 ) != UC_ERR_OK ||
			 uc_hook_add( uc, &h, UC_HOOK_MEM_UNMAPPED | UC_HOOK_MEM_PROT, (void*)on_bad_mem, hc_.get(), 1, 0 ) != UC_ERR_OK ||
			 ( hle_->mapped() && uc_hook_add( uc, &h, UC_HOOK_CODE, (void*)on_hle, hc_.get(), hle::module_va, hle::module_va + hle::module_size - 1 ) != UC_ERR_OK ) )
		{
			error = "uc_hook_add failed";
			log_.push_back( "FAILED: " + error );
			return false;
		}
		// U2020: instrumentation - counters (CPUID / RDTSC / RDTSCP / RDPID, environment reads, the tail) and the trace file
		r.probes.enabled = po_.counters;
		hc_->counting = po_.counters;
		hc_->tail_next = 0;
		if ( po_.counters )
		{
			const std::pair<uint64_t, uint64_t> ranges[] = { { guest_peb_va, page_size }, { guest_teb_va, guest_teb_size }, { guest_kuser_va, page_size } };
			for ( auto [ lo, n ] : ranges )
				if ( uc_hook_add( uc, &h, UC_HOOK_MEM_READ, (void*)on_env_read, hc_.get(), lo, lo + n - 1 ) != UC_ERR_OK )
				{
					error = "uc_hook_add (environment reads) failed";
					log_.push_back( "FAILED: " + error );
					return false;
				}
			log_.push_back( "probe counters on: CPUID / RDTSC / RDTSCP / RDPID, reads of PEB / TEB / KUSER_SHARED_DATA per member (public-symbol field "
							"tables), system calls, API calls, exceptions; the last " + std::to_string( po_.trace_tail ) + " instructions" );
		}
		if ( !po_.trace_path.empty() )
		{
			hc_->trace = std::make_unique<output_file>();
			std::string terr;
			if ( !hc_->trace->create( po_.trace_path, po_.trace_max_bytes, terr ) )
			{
				hc_->trace.reset();
				error = "the trace file: " + terr;
				log_.push_back( "FAILED: " + error );
				return false;
			}
			hc_->dis = std::make_unique<disassembler>();
			log_.push_back( "trace: one line per instruction (index, RIP, region +offset, bytes, Capstone text) -> " + po_.trace_path + " (at most " +
							std::to_string( po_.trace_max_bytes ) + " bytes)" );
		}
		log_.push_back( "run: SYSRETQ to " + ( start_va_ == map_.entry ? "the entry " + hex( map_.entry ) : "the TLS start trampoline " + hex( start_va_ ) + " (then the entry " + hex( map_.entry ) + ")" ) +
						" at CPL 3, at most " + std::to_string( po_.max_insns ) + " instructions" );
		// 4. the run: until the return sentinel, a stop of the hooks, or the instruction bound (no host time)
		ue = e_->emulate( start_sysret_, guest_return_sentinel, po_.max_insns );
		std::string rerr;
		r.have_final = reg::read( uc, r.final, rerr );
		const uint64_t rip = read_reg( uc, UC_X86_REG_RIP );
		if ( !hc_->stopped )
		{
			if ( rip == guest_return_sentinel )
			{
				r.fault_address = rip;
				hc_->stop( stop_kind::returned, "the entry point returned (to the sentinel " + hex( rip ) +
													"): a thread exit (RtlExitUserThread) is not modelled in milestone M5" );
			}
			else if ( ue == UC_ERR_OK )   // no until, no hook stop: Unicorn's own instruction count ended the run
				hc_->stop( stop_kind::count, "the instruction bound " + std::to_string( po_.max_insns ) + " was reached at rip " + hex( rip ) + " (" +
												 std::to_string( r.insns ) + " instructions counted)" );
			else
				hc_->stop( stop_kind::error, std::string( "emulation ended without a stop reason: " ) + uc_strerror( ue ) + ", rip " + hex( rip ) );
		}
		log_.push_back( std::string( "stop: " ) + name_of( r.stop ) + " - " + r.detail );
		// U2020: the counters that come from the run's records; the tail ring oldest first
		if ( po_.counters )
		{
			finish_cpuid( uc, *hc_ );
			for ( auto& s : r.syscalls ) r.probes.syscalls[ s.name.empty() ? "(unknown " + hex( s.number ) + ")" : s.name ]++;
			for ( auto& a : r.api_calls ) r.probes.apis[ a.api ]++;
			if ( r.fault_vector >= 0 ) r.probes.exceptions[ r.fault_vector ]++;
			auto& t = r.probes.tail;
			if ( t.size() == tail_capacity() && hc_->tail_next ) std::rotate( t.begin(), t.begin() + std::ptrdiff_t( hc_->tail_next ), t.end() );
		}
		if ( hc_->trace )
		{
			hc_->trace->close();
			hc_->trace.reset();
			log_.push_back( "trace: " + std::to_string( r.probes.trace_lines ) + " lines, " + std::to_string( r.probes.trace_bytes ) + " bytes" +
							( r.probes.trace_truncated ? " (TRUNCATED at the size limit)" : "" ) + " -> " + po_.trace_path );
		}
		return true;
	}

	// ------------------------------------------------------------------------------------------------ U2020: instrumentation output
	std::string process::where( uint64_t address ) const
	{
		for ( const auto& rg : e_->regions() )
			if ( address >= rg.base && address - rg.base < rg.size ) return "[" + rg.name + "]+" + hex( address - rg.base );
		return "[unmapped]";
	}

	bool process::name_field( uint64_t address, std::string& region, std::string& field ) const
	{
		auto pick = [ & ]( const char* rname, uint64_t base, const auto& table, uint64_t size ) {
			if ( address < base || address - base >= size ) return false;
			region = rname;
			const uint64_t off = address - base;
			const fields::field* f = fields::find( table, off );
			field = f ? std::string( f->name ) + ( off != f->offset ? "+" + hex( off - f->offset ) : std::string() ) : "(+" + hex( off ) + ", no member)";
			return true;
		};
		return pick( "PEB", guest_peb_va, fields::peb, fields::peb_size ) || pick( "PEB", guest_peb_va, fields::peb, page_size ) ||
			   pick( "TEB", guest_teb_va, fields::teb, guest_teb_size ) || pick( "KUSER", guest_kuser_va, fields::kuser, page_size );
	}

	std::vector<std::string> process::probe_summary( const run_result& r ) const
	{
		std::vector<std::string> o;
		const auto& p = r.probes;
		if ( !p.enabled ) return o;
		auto h32 = [ & ]( uint32_t v ) {
			char b[ 16 ];
			std::snprintf( b, sizeof( b ), "%08X", v );
			return std::string( b );
		};
		o.push_back( "probe: instructions " + std::to_string( r.insns ) + "; CPUID " + std::to_string( p.cpuid_total ) + " (" + std::to_string( p.cpuid.size() ) +
					 " leaf / sub-leaf pairs), RDTSC " + std::to_string( p.rdtsc ) + ", RDTSCP " + std::to_string( p.rdtscp ) + ", RDPID " + std::to_string( p.rdpid ) );
		for ( const auto& [ k, c ] : p.cpuid )
			o.push_back( "probe: cpuid leaf " + h32( c.leaf ) + "H sub-leaf (ECX) " + h32( c.subleaf ) + "H x" + std::to_string( c.count ) + ", first at " + where( c.first_at ) +
						 ( c.answered ? ", answered EAX " + h32( c.eax ) + " EBX " + h32( c.ebx ) + " ECX " + h32( c.ecx ) + " EDX " + h32( c.edx )
									  : std::string( ", no answer recorded (the run stopped at the CPUID)" ) ) );
		o.push_back( "probe: environment reads " + std::to_string( p.reads_total ) + " (" + std::to_string( p.reads.size() ) + " members)" );
		for ( const auto& [ k, c ] : p.reads ) o.push_back( "probe: read " + k + " x" + std::to_string( c.count ) + ", first at " + where( c.first_at ) );
		uint64_t ns = 0, na = 0, ne = 0;
		for ( auto& [ k, v ] : p.syscalls ) ns += v;
		for ( auto& [ k, v ] : p.apis ) na += v;
		for ( auto& [ k, v ] : p.exceptions ) ne += v;
		o.push_back( "probe: system calls " + std::to_string( ns ) + " (" + std::to_string( p.syscalls.size() ) + " services)" );
		for ( auto& [ k, v ] : p.syscalls ) o.push_back( "probe: syscall " + k + " x" + std::to_string( v ) );
		o.push_back( "probe: API calls " + std::to_string( na ) + " (" + std::to_string( p.apis.size() ) + " APIs)" );
		for ( auto& [ k, v ] : p.apis ) o.push_back( "probe: api " + k + " x" + std::to_string( v ) );
		o.push_back( "probe: exceptions " + std::to_string( ne ) + " (each one stops the run until exception dispatch, milestone M7)" );
		for ( auto& [ k, v ] : p.exceptions ) o.push_back( "probe: exception vector " + std::to_string( k ) + " x" + std::to_string( v ) );
		o.push_back( "probe: the last " + std::to_string( p.tail.size() ) + " instructions (oldest first):" );
		disassembler d;
		for ( const auto& t : p.tail )
		{
			std::string bytes;
			for ( uint32_t k = 0; k < t.size; k++ )
			{
				char b[ 4 ];
				std::snprintf( b, sizeof( b ), "%02x", t.bytes[ k ] );
				bytes += b;
			}
			o.push_back( "probe: tail #" + std::to_string( t.index ) + " " + hex( t.rip ) + " " + where( t.rip ) + " " + bytes + "  " + ( d.ok() ? d.decode( t.bytes, t.size, t.rip ).text : "" ) );
		}
		if ( !po_.trace_path.empty() )
			o.push_back( "probe: trace file " + po_.trace_path + ": " + std::to_string( p.trace_lines ) + " lines" + ( p.trace_truncated ? " (truncated)" : "" ) );
		return o;
	}

	std::vector<std::string> process::observable( const run_result& r ) const
	{
		std::vector<std::string> o;
		o.push_back( std::string( "stop: " ) + name_of( r.stop ) );
		o.push_back( "stop detail: " + r.detail );
		if ( r.stop == stop_kind::terminated )
		{
			char b[ 64 ];
			std::snprintf( b, sizeof( b ), "exit status: 0x%08x (%u)", r.exit_status, r.exit_status );
			o.push_back( b );
		}
		o.push_back( "instructions executed: " + std::to_string( r.insns ) + " (user code + the environment's SYSRETQ)" );
		o.push_back( "syscall instructions executed by the guest: " + std::to_string( r.syscall_insns ) + "; handled by the emulated kernel layer: " +
					 std::to_string( r.syscalls_handled ) + "; forwarded to the host: 0 (no such path exists)" );
		for ( auto& s : r.syscalls )
		{
			std::string l = "syscall " + std::to_string( s.index ) + ": " + ( s.name.empty() ? std::string( "(unknown)" ) : s.name ) + " number " + hex( s.number ) +
							" (rax " + hex( s.rax ) + ") at " + hex( s.at ) + " args";
			for ( int i = 0; i < 6; i++ ) l += " " + ( i < 4 || s.stack_args_read ? hex( s.args[ size_t( i ) ] ) : std::string( "?" ) );
			o.push_back( l + " -> " + s.outcome );
		}
		for ( auto& s : r.syscalls )
			for ( auto& wr : s.writes ) o.push_back( "syscall " + std::to_string( s.index ) + " wrote " + wr );
		for ( auto& a : r.api_calls )
			o.push_back( "api call " + std::to_string( a.index ) + ": " + a.api + "( " + hex( a.args[ 0 ] ) + ", " + hex( a.args[ 1 ] ) + ", " + hex( a.args[ 2 ] ) + ", " + hex( a.args[ 3 ] ) +
						 " ) -> " + a.outcome );
		for ( auto& l : kernel_->describe() ) o.push_back( "kernel: " + l );
		if ( r.fault_vector >= 0 ) o.push_back( "fault vector " + std::to_string( r.fault_vector ) + ", error code " + std::to_string( r.fault_error_code ) + ", address " + hex( r.fault_address ) );
		o.push_back( "image: base " + hex( map_.base ) + ", entry " + hex( map_.entry ) + ( map_.relocated ? ", relocated (" + std::to_string( map_.relocations ) + " fixups)" : "" ) );
		for ( auto& l : env_lines_ ) o.push_back( "env: " + l );
		if ( r.have_final )
			for ( auto& l : reg::format( r.final ) ) o.push_back( "final: " + l );
		return o;
	}

	std::vector<std::string> process::state_dump( const run_result& r ) const
	{
		std::vector<std::string> d;
		d.push_back( std::string( "STATE DUMP (stop: " ) + name_of( r.stop ) + ")" );
		d.push_back( "  " + r.detail );
		if ( !r.syscalls.empty() )
		{
			const auto& s = r.syscalls.back();
			d.push_back( "  last system call: #" + std::to_string( s.index ) + " " + ( s.name.empty() ? std::string( "(unknown)" ) : s.name ) + " EAX AND FFFFH " +
						 hex( s.number ) + ", RAX " + hex( s.rax ) + ", at " + hex( s.at ) );
			d.push_back( "  arguments: R10 " + hex( s.args[ 0 ] ) + ", RDX " + hex( s.args[ 1 ] ) + ", R8 " + hex( s.args[ 2 ] ) + ", R9 " + hex( s.args[ 3 ] ) +
						 ( s.stack_args_read ? ", [RSP+28H] " + hex( s.args[ 4 ] ) + ", [RSP+30H] " + hex( s.args[ 5 ] ) : std::string( ", stack arguments not readable" ) ) );
			d.push_back( "  user mode at the SYSCALL: rip " + hex( r.user_rip ) + ", rflags (R11) " + hex( r.user_rflags ) + " (" + reg::decode_rflags( r.user_rflags ) +
						 "), rsp " + hex( r.user_rsp ) );
			d.push_back( "  the register dump below is the kernel-entry state after the SDM SYSCALL transition (CPL 0, RIP = IA32_LSTAR)" );
		}
		if ( !r.api_calls.empty() )
		{
			const auto& a = r.api_calls.back();
			d.push_back( "  last API call (--HLE): #" + std::to_string( a.index ) + " " + a.api + " at stub " + hex( a.stub ) + ", RCX " + hex( a.args[ 0 ] ) + ", RDX " +
						 hex( a.args[ 1 ] ) + ", R8 " + hex( a.args[ 2 ] ) + ", R9 " + hex( a.args[ 3 ] ) + " -> " + a.outcome );
		}
		if ( r.have_final )
			for ( auto& l : reg::format( r.final ) ) d.push_back( "  " + l );
		for ( auto& l : e_->describe_memory_map() ) d.push_back( "  " + l );
		if ( kernel_ )
			for ( auto& l : kernel_->describe() ) d.push_back( "  " + l );
		return d;
	}
}
