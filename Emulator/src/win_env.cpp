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

	namespace
	{
		// U2031: the files the --Sys loader start needs in the capture (U2024)
		bool loader_files_present( const environment_facts& f )
		{
			if ( !f.files ) return false;
			for ( const char* n : { "ntdll.dll", "kernel32.dll", "kernelbase.dll", "apisetschema.dll" } )
				if ( !f.files->count( n ) ) return false;
			return !f.manifest.nls_acp_file.empty() && !f.manifest.nls_oemcp_file.empty() && !f.manifest.nls_case_file.empty();
		}

		// U2030: _CONTEXT member offsets (guest ntdll.pdb, emu/win_fields.hpp) and winnt.h CONTEXT_* flags (SDK um\winnt.h :4218-4226)
		constexpr uint32_t ctx_flags = fields::offset_of( fields::context, "ContextFlags" ), ctx_mxcsr = fields::offset_of( fields::context, "MxCsr" ),
						   ctx_segcs = fields::offset_of( fields::context, "SegCs" ), ctx_eflags = fields::offset_of( fields::context, "EFlags" ),
						   ctx_rax = fields::offset_of( fields::context, "Rax" ), ctx_rsp = fields::offset_of( fields::context, "Rsp" ),
						   ctx_rip = fields::offset_of( fields::context, "Rip" ), ctx_fltsave = fields::offset_of( fields::context, "FltSave" ),
						   ctx_xmm0 = fields::offset_of( fields::context, "Xmm0" );
		static_assert( ctx_flags == 0x30 && ctx_mxcsr == 0x34 && ctx_segcs == 0x38 && ctx_eflags == 0x44 && ctx_rax == 0x78 && ctx_rsp == 0x98 && ctx_rip == 0xF8 &&
						   ctx_fltsave == 0x100 && ctx_xmm0 == 0x1A0 && fields::context_size == 0x4D0,
					   "_CONTEXT (guest ntdll.pdb)" );
		constexpr uint32_t context_amd64 = 0x100000, context_control = context_amd64 | 1, context_integer = context_amd64 | 2, context_segments = context_amd64 | 4,
						   context_floating_point = context_amd64 | 8, context_full = context_control | context_integer | context_floating_point;
		// the user RFLAGS bits a CONTEXT may set (ReactOS 0.4.16 sdk/include/ndk/amd64/ketypes.h:207 EFLAGS_USER_SANITIZE 3F4DD7H, minus
		// VM (bit 17, meaningless in 64-bit mode)); IF and bit 1 always set
		constexpr uint64_t eflags_user_sanitize = 0x3F4DD7 & ~0x20000ull;
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
		f.manifest = m;
		// U2024: the further captured files (system DLL images, apisetschema, NLS tables), each size-checked against the manifest
		{
			auto files = std::make_shared<std::map<std::string, std::vector<uint8_t>>>();
			uint64_t total = 0;
			for ( const auto& [ name, sz ] : m.files )
			{
				std::vector<uint8_t> b;
				if ( !load_capture_file( f.capture_dir, m, name, b, error ) ) return false;
				total += b.size();
				if ( total > max_capture_total_bytes )
				{
					error = f.capture_dir + ": the further captured files exceed " + std::to_string( max_capture_total_bytes ) + " bytes";
					return false;
				}
				( *files )[ name ] = std::move( b );
			}
			( *files )[ "ntdll.dll" ] = nt;   // the system image ntdll.dll (the syscall table's source, size = the manifest)
			f.files = files;
			auto reg = std::make_shared<std::map<std::string, nt::registry_key>>();   // U2034
			if ( !load_registry( f.capture_dir, *reg, error ) ) return false;
			f.registry = reg;
		}
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
		std::string names;
		for ( const auto& [ n, b ] : *f.files ) names += ( names.empty() ? "" : ", " ) + n + " (" + std::to_string( b.size() ) + ")";
		log.push_back( "  further captured files (U2024, read as data; sizes = the manifest): " + ( names.empty() ? std::string( "none" ) : names ) +
					   ( m.nls_acp_file.empty() ? std::string() : "; NLS: ACP " + std::to_string( m.nls_acp ) + " " + m.nls_acp_file + ", OEMCP " + std::to_string( m.nls_oemcp ) + " " +
																		  m.nls_oemcp_file + ", case table " + m.nls_case_file ) );
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
		// U2040: register-level trace lines (process_options::trace_regs) and the ring of the last lines (trace_last)
		bool regs = false;
		uint64_t last = 0;
		std::vector<std::string> ring;
		size_t ring_next = 0;
		std::string pending;          // the current instruction's line: written when the next instruction starts / the run stops
		bool have_pending = false;
		bool pending_mem = false;
		// P41 (U2084): the console capture - the --Sys entry hooks (address -> export), the calls waiting for their return, the file
		std::map<uint64_t, std::string> con_hooks;
		struct con_wait
		{
			uint64_t ret = 0, rsp = 0, pwritten = 0;
			console_attempt a;
		};
		std::vector<con_wait> con_waits;
		std::unique_ptr<output_file> con;
		std::string con_dir;
		uint64_t con_max = 0;
		bool con_failed = false;

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

		// U2040: one trace line out - straight to the file, or (trace_last) into the ring of the last lines
		void trace_emit( process::hook_ctx& c, std::string&& l )
		{
			auto& pr = c.r->probes;
			if ( pr.trace_truncated ) return;
			if ( c.last )
			{
				if ( c.ring.size() < c.last ) c.ring.push_back( std::move( l ) );
				else c.ring[ c.ring_next ] = std::move( l );
				c.ring_next = size_t( ( c.ring_next + 1 ) % c.last );
				return;
			}
			if ( c.trace->write( l ) )
			{
				pr.trace_bytes += l.size();
				pr.trace_lines++;
			}
			else
				pr.trace_truncated = true;
		}

		void trace_flush_pending( process::hook_ctx& c )
		{
			if ( !c.have_pending ) return;
			c.have_pending = false;
			c.pending += "\n";
			trace_emit( c, std::move( c.pending ) );
			c.pending.clear();
		}

		// U2040: at the stop - the last pending line, then the ring oldest first through the audited writer
		void trace_finish( process::hook_ctx& c )
		{
			trace_flush_pending( c );
			if ( !c.last ) return;
			auto& pr = c.r->probes;
			const size_t n = c.ring.size(), first = n < c.last ? 0 : c.ring_next;
			for ( size_t k = 0; k < n && !pr.trace_truncated; k++ )
			{
				const std::string& l = c.ring[ ( first + k ) % n ];
				if ( c.trace->write( l ) )
				{
					pr.trace_bytes += l.size();
					pr.trace_lines++;
				}
				else
					pr.trace_truncated = true;
			}
			c.ring.clear();
		}

		// U2040: the 16 general-purpose registers and RFLAGS before the instruction (" | rax=... rfl=...")
		std::string trace_regs_text( uc_engine* uc )
		{
			static const struct { int id; const char* n; } R[] = {
				{ UC_X86_REG_RAX, "rax" }, { UC_X86_REG_RBX, "rbx" }, { UC_X86_REG_RCX, "rcx" }, { UC_X86_REG_RDX, "rdx" }, { UC_X86_REG_RSI, "rsi" },
				{ UC_X86_REG_RDI, "rdi" }, { UC_X86_REG_RBP, "rbp" }, { UC_X86_REG_RSP, "rsp" }, { UC_X86_REG_R8, "r8" }, { UC_X86_REG_R9, "r9" },
				{ UC_X86_REG_R10, "r10" }, { UC_X86_REG_R11, "r11" }, { UC_X86_REG_R12, "r12" }, { UC_X86_REG_R13, "r13" }, { UC_X86_REG_R14, "r14" },
				{ UC_X86_REG_R15, "r15" }, { UC_X86_REG_RFLAGS, "rfl" } };
			std::string s = " |";
			for ( auto& r : R )
			{
				char t[ 32 ];
				std::snprintf( t, sizeof( t ), " %s=%016llx", r.n, (unsigned long long)read_reg( uc, r.id ) );
				s += t;
			}
			return s;
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
				if ( c.regs )
				{
					// U2040: the previous instruction's line is complete (its memory accesses were appended by on_trace_mem)
					trace_flush_pending( c );
					l += trace_regs_text( uc );
					c.pending = std::move( l );
					c.have_pending = true;
					c.pending_mem = false;
				}
				else
					trace_emit( c, l + "\n" );
			}
		}

		// U2040: memory reads / writes of the current instruction, appended to its pending trace line (observation only: a read
		// is shown with the bytes in guest memory at the access, read through the engine with the size bounded to 64)
		void on_trace_mem( uc_engine* uc, uc_mem_type type, uint64_t address, int size, int64_t value, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			if ( !c.have_pending || c.r->probes.trace_truncated ) return;
			if ( c.pending.size() > 4096 ) return;   // a string instruction's accesses: the first ones suffice
			char t[ 96 ];
			const unsigned long long a = (unsigned long long)address;
			std::string s = c.pending_mem ? " " : " | mem ";
			c.pending_mem = true;
			if ( type == UC_MEM_WRITE )
			{
				if ( size > 0 && size <= 8 )
				{
					const uint64_t v = size == 8 ? uint64_t( value ) : uint64_t( value ) & ( ( 1ull << ( 8 * size ) ) - 1 );
					std::snprintf( t, sizeof( t ), "w:0x%llx:%d=0x%llx", a, size, (unsigned long long)v );
					s += t;
				}
				else
				{
					std::snprintf( t, sizeof( t ), "w:0x%llx:%d=bytes:", a, size );
					s += t;
				}
			}
			else
			{
				uint8_t v[ 64 ] = {};
				const size_t m = size > 0 && size <= 64 ? size_t( size ) : 0;
				const bool got = m && uc_mem_read( uc, address, v, m ) == UC_ERR_OK;
				std::snprintf( t, sizeof( t ), "r:0x%llx:%d=", a, size );
				s += t;
				if ( got && m <= 8 )
				{
					uint64_t x = 0;
					std::memcpy( &x, v, m );   // little-endian guest bytes as a number
					std::snprintf( t, sizeof( t ), "0x%llx", (unsigned long long)x );
					s += t;
				}
				else
				{
					s += "bytes:";
					for ( size_t k = 0; got && k < m; k++ )
					{
						char h[ 4 ];
						std::snprintf( h, sizeof( h ), "%02x", v[ k ] );
						s += h;
					}
				}
			}
			c.pending += s;
		}

		// P41 (U2084): the console capture - text of the guest (bounded: at most 512 units read; host safety: only mapped guest memory,
		// read through the engine), printable ASCII kept, everything else escaped
		std::string con_text( uc_engine* uc, uint64_t a, uint64_t n, bool wide, bool nul_terminated )
		{
			std::string s;
			if ( !a ) return s;
			const uint64_t cap = 512;
			for ( uint64_t i = 0; i < ( nul_terminated ? cap : std::min( n, cap ) ); i++ )
			{
				uint16_t v = 0;
				if ( uc_mem_read( uc, a + i * ( wide ? 2 : 1 ), &v, wide ? 2 : 1 ) != UC_ERR_OK )
				{
					s += "<unreadable>";
					break;
				}
				if ( nul_terminated && !v ) break;
				char e[ 16 ];
				if ( v == 13 ) s += "\\r";
				else if ( v == 10 ) s += "\\n";
				else if ( v == 9 ) s += "\\t";
				else if ( v == '\\' ) s += "\\\\";
				else if ( v == '"' ) s += "\\" "\"";
				else if ( v >= 0x20 && v < 0x7F ) s += char( v );
				else
				{
					std::snprintf( e, sizeof( e ), wide ? "\\u%04X" : "\\x%02X", unsigned( v ) );
					s += e;
				}
			}
			if ( !nul_terminated && n > cap ) s += "... (" + std::to_string( n ) + " units, the first 512 shown)";
			return s;
		}

		// record one attempt: in the run result and (from the first attempt on) in <console dir>\console_capture.txt
		void con_record( process::hook_ctx& c, console_attempt a )
		{
			a.index = c.r->console.size() + 1;
			const std::string line = "#" + std::to_string( a.index ) + " insn " + std::to_string( a.insn ) + " " + a.via + " handle " + hex( a.handle ) + " length " +
									 std::to_string( a.length ) + " text \"" + a.text + "\" -> " + a.result + "\n";
			c.r->console.push_back( std::move( a ) );
			if ( c.con_dir.empty() || c.con_failed ) return;
			if ( !c.con )
			{
				c.con = std::make_unique<output_file>();
				std::string err;
				const std::string path = c.con_dir + "\\console_capture.txt";
				if ( !c.con->create( path, c.con_max, err ) )
				{
					c.con.reset();
					c.con_failed = true;
					c.r->console_file = "(not written: " + err + ")";
					return;
				}
				c.r->console_file = path;
				c.con->write( "# console capture (P41, emu::win): every console / standard-handle output ATTEMPT of the emulated process, in order - observation "
							  "only; the process got the measured results of a process WITHOUT a console (tm_nt con.*)\n" );
			}
			if ( !c.con->write( line ) ) c.r->console_truncated = true;
		}

		// --Sys: the result of a waiting attempt when its call returns (RAX, the TEB's LastErrorValue, the written count)
		void con_return( uc_engine* uc, process::hook_ctx& c )
		{
			auto w = c.con_waits.back();
			c.con_waits.pop_back();
			uint64_t rax = read_reg( uc, UC_X86_REG_RAX ), gs = read_reg( uc, UC_X86_REG_GS_BASE );
			uint32_t le = 0, wr = 0;
			const bool lok = uc_mem_read( uc, gs + 0x68, &le, 4 ) == UC_ERR_OK;
			std::string res = "returned " + hex( uint32_t( rax ) ) + ( lok ? ", LastError " + std::to_string( le ) : std::string() );
			if ( w.pwritten ) res += uc_mem_read( uc, w.pwritten, &wr, 4 ) == UC_ERR_OK ? ", written " + std::to_string( wr ) : ", written <unreadable>";
			w.a.result = res;
			con_record( c, w.a );
		}

		void on_code( uc_engine* uc, uint64_t address, uint32_t size, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			c.r->insns++;
			c.last_insn = address;
			if ( !c.con_waits.empty() && address == c.con_waits.back().ret && read_reg( uc, UC_X86_REG_RSP ) == c.con_waits.back().rsp + 8 ) con_return( uc, c );   // P41 (U2084)
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
			s.args = { read_reg( uc, UC_X86_REG_R10 ), read_reg( uc, UC_X86_REG_RDX ), read_reg( uc, UC_X86_REG_R8 ), read_reg( uc, UC_X86_REG_R9 ) };
			uint64_t stack[ 8 ] = {};   // U2031: [RSP+28H] .. [RSP+60H] (arguments 5..12)
			std::string err;
			if ( rsp <= UINT64_MAX - 0x68 && c.p->eng().read( rsp + 0x28, stack, sizeof( stack ), err ) )
			{
				for ( int i = 0; i < 8; i++ ) s.args[ size_t( 4 + i ) ] = stack[ i ];
				s.stack_args_read = true;
			}
			else if ( rsp <= UINT64_MAX - 0x38 && c.p->eng().read( rsp + 0x28, stack, 16, err ) )
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
			call.insns = r.insns;   // U2032: the virtual clock
			const nt::reply rep = c.p->kern().dispatch( call );
			s.writes = rep.writes;
			if ( s.name == "NtWriteFile" && c.con_waits.empty() && s.stack_args_read && c.p->std_handle( uc, s.args[ 0 ] ) )
			{
				// P41 (U2084): NtWriteFile on a standard handle / handle 0 outside the hooked output exports (a program's own system call)
				console_attempt ca;
				ca.insn = r.insns;
				ca.via = "NtWriteFile";
				ca.handle = s.args[ 0 ];
				ca.length = uint32_t( s.args[ 6 ] );
				ca.text = con_text( uc, s.args[ 5 ], uint32_t( s.args[ 6 ] ), false, false );
				char st[ 16 ];
				std::snprintf( st, sizeof( st ), "%08X", rep.status );
				ca.result = rep.kind == nt::reply_kind::done ? std::string( "status " ) + st + "H" : std::string( "not answered (stop)" );
				con_record( c, ca );
			}
			if ( rep.kind == nt::reply_kind::resume )   // U2030: NtContinue - the full CPU state from the CONTEXT, back to user mode by IRETQ
			{
				std::string why;
				if ( !c.p->resume_context( uc, rep.context, why ) )
				{
					s.outcome += "not answered: " + why;
					r.syscalls.push_back( s );
					c.stop( stop_kind::unimplemented_syscall, s.name + " at rip " + hex( s.at ) + ": " + why + " - stopped with a state dump" );
					uc_emu_stop( uc );
					return;
				}
				s.answered = true;
				s.outcome += rep.text;
				r.syscalls.push_back( s );
				return;
			}
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

		// U2025 (plan 3.M5 part 2): under --HLE the system DLL images are DATA - an instruction fetched from one (a call through an
		// export address the program found itself, e.g. by walking the loader lists and an export directory) stops loudly, naming
		// the export (the --HLE APIs are reached through the import stubs; the DLL code runs only under --Sys)
		void on_sysimage_hle( uc_engine* uc, uint64_t address, uint32_t, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			if ( c.stopped ) return;
			for ( const auto& m : c.p->modules() )
			{
				if ( !m.is_dll || address < m.base || address - m.base >= m.size ) continue;
				const uint32_t rva = uint32_t( address - m.base );
				std::string at = m.name + "+" + hex( rva );
				auto it = m.exports_by_rva.upper_bound( rva );
				if ( it != m.exports_by_rva.begin() )
				{
					--it;
					at = m.name + "!" + it->second + ( rva != it->first ? "+" + hex( rva - it->first ) : std::string() );
				}
				api_record a;
				a.index = c.r->api_calls.size() + 1;
				a.stub = address;
				a.api = at;
				a.args = { read_reg( uc, UC_X86_REG_RCX ), read_reg( uc, UC_X86_REG_RDX ), read_reg( uc, UC_X86_REG_R8 ), read_reg( uc, UC_X86_REG_R9 ) };
				a.outcome = "not answered: execution entered the system DLL image (data under --HLE)";
				c.r->api_calls.push_back( a );
				c.stop( stop_kind::unimplemented_api, "execution reached " + at + " (" + hex( address ) + ") under --HLE: the system DLL images are mapped as data, their code runs "
																	   "only under --Sys; no --HLE API is reached this way - stopped with a state dump" );
				uc_emu_stop( uc );
				return;
			}
		}

		// P41 (U2084): --Sys - execution entered an output export of kernelbase (the hooks of process::run): the attempt waits for its return
		void on_con_api( uc_engine* uc, uint64_t address, uint32_t, void* user )
		{
			auto& c = *static_cast<process::hook_ctx*>( user );
			if ( c.stopped ) return;
			const auto it = c.con_hooks.find( address );
			if ( it == c.con_hooks.end() ) return;
			const std::string& n = it->second;
			const uint64_t rcx = read_reg( uc, UC_X86_REG_RCX ), rdx = read_reg( uc, UC_X86_REG_RDX ), r8 = read_reg( uc, UC_X86_REG_R8 ), r9 = read_reg( uc, UC_X86_REG_R9 );
			const uint64_t rsp = read_reg( uc, UC_X86_REG_RSP );
			uint64_t ret = 0;
			if ( uc_mem_read( uc, rsp, &ret, 8 ) != UC_ERR_OK ) return;
			process::hook_ctx::con_wait w;
			w.ret = ret;
			w.rsp = rsp;
			w.a.insn = c.r->insns;
			w.a.via = "kernelbase.dll!" + n;
			if ( n == "OutputDebugStringA" || n == "OutputDebugStringW" )
			{
				w.a.text = con_text( uc, rcx, 0, n.back() == 'W', true );
				w.a.length = w.a.text.size();
			}
			else
			{
				if ( n == "WriteFile" && !c.p->std_handle( uc, rcx ) ) return;   // a write to a file / pipe: not a console attempt
				w.a.handle = rcx;
				w.a.length = uint32_t( r8 );
				w.a.text = con_text( uc, rdx, uint32_t( r8 ), n == "WriteConsoleW", false );
				w.pwritten = r9;
			}
			w.a.result = "did not return before the stop";
			c.con_waits.push_back( w );
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
			if ( res.console )   // P41 (U2084): an output attempt through the API stubs
			{
				console_attempt ca;
				ca.insn = r.insns;
				ca.via = a.api;
				ca.handle = res.con_handle;
				ca.length = res.con_length;
				ca.text = con_text( uc, res.con_buffer, res.con_length, res.con_wide, false );
				ca.result = res.con_result;
				con_record( c, ca );
			}
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
		p->rng_seed_ = ec.rng_seed;   // P39 (U2052)
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
		// plan 3.M5 part 2 (U2031): --Sys --loader with the environment's loader files in the capture (ntdll, kernel32, kernelbase,
		// apisetschema, the NLS tables) starts the process in the real ntdll!LdrInitializeThunk - every image, as Windows does; the
		// loader then maps kernel32 / kernelbase, resolves the imports and runs the TLS callbacks itself. OPT-IN: the real loader
		// meets system services no allowed source describes (P36: NtQuerySecurityAttributesToken first) and stops there loudly; the
		// default --Sys start stays the direct one of milestone M5 until those services have a source.
		if ( po.loader && po.boundary != env_boundary::sys ) return refuse( "--loader is a --Sys option (the real ntdll loader); --HLE reimplements the loader's part" );
		if ( po.loader && !loader_files_present( f ) )
			return refuse( "--Sys --loader needs the loader files in the capture (kernel32.dll, kernelbase.dll, apisetschema.dll, the NLS tables: capture_sysdlls.py --dll / "
						   "--nls, U2024)" );
		loader_start_ = po.boundary == env_boundary::sys && po.loader;
		if ( po.boundary == env_boundary::sys && !loader_start_ && ( has_imports || has_tls ) )
		{
			std::string what, why;
			if ( has_imports && pe::imports( file, img_, imports, why ) )
				for ( auto& m : imports ) what += ( what.empty() ? "" : ", " ) + m.dll;
			return refuse( "--Sys: " + po.image_name + ( has_imports ? " imports" + ( what.empty() ? std::string( " (a malformed import directory)" ) : " " + what ) : std::string() ) +
						   ( has_imports && has_tls ? " and" : "" ) + ( has_tls ? " has a TLS directory" : "" ) +
						   ": under --Sys an image with imports / TLS starts in the environment's real ntdll!LdrInitializeThunk (plan 6.1) - the "
						   "opt-in --Sys --loader start (U2031), which stops at the first system service without a source (P36). --HLE runs this image." );
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
			 !map( guest_kernel_entry_va, page_size, UC_PROT_READ | UC_PROT_EXEC, "emulator kernel: system-call entry (SYSRETQ) + start stub" ) ||
			 !map( guest_kernel_stack_va, page_size, UC_PROT_READ | UC_PROT_WRITE, "emulator kernel: stack page (IRETQ frames, U2030)" ) )
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
		// U2030: +200H = IRETQ (48 CF; 253667-093 Vol2A IRET / IRETD / IRETQ Operation "IA-32e mode": RIP, CS, RFLAGS, RSP, SS popped) - the
		// return to user mode with a full register state (NtContinue, the --Sys loader start), where SYSRETQ would lose RCX / R11
		iretq_stub_ = guest_kernel_entry_va + 0x200;
		const uint8_t iretq[] = { 0x48, 0xCF };
		if ( !w( iretq_stub_, iretq, sizeof( iretq ), "", "" ) ) return false;

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
		kf.total_phys = f.total_phys;   // U2032
		kf.processors = f.processors;
		kf.rng_seed = rng_seed_;   // P39 (U2052): the seeded model's seed (decision D8) - the process cookie
		kf.registry = facts_.registry.get();   // U2034 (facts_ owns it for the process' lifetime)
		kf.files = facts_.files.get();         // P39 (U2057): KnownDlls sections map the captured images
		kf.image_dir = make_identity( f, po.image_file_name ).image_dir;   // P39 (U2058): the emulated file system's one directory
		kf.system_root = f.system_root;        // P39 (U2057): the KnownDllPath link target
		kernel_ = std::make_unique<nt::kernel>( *e_, kf );
		hle_ = std::make_unique<hle::module>();
		start_va_ = map_.entry;

		// plan 3.M5 part 2 (U2025-U2027): the process state a program finds at its entry - the system DLL images as data, the loader
		// data (PEB Ldr), the process parameters, the process heap - and the PEB members that point at them or describe the process
		// (offsets: the guest ntdll.pdb _PEB, emu/win_fields.hpp; values: ReactOS 0.4.16 ntoskrnl/mm/ARM3/procsup.c:584-619 MmCreatePeb
		// for which member the kernel fills from what, licensed OSS reference only)
		{
			std::vector<module_image> sys;
			std::vector<std::string> mlog;
			// --HLE (and --Sys without the loader files): the three system images as data; the --Sys loader start: the kernel maps
			// ntdll.dll only (ReactOS 0.4.16 ntoskrnl/ps/process.c PspCreateProcess -> PspMapSystemDll; the loader maps the rest)
			std::vector<std::string> names( std::begin( system_images_order ), std::end( system_images_order ) );
			if ( loader_start_ ) names = { "ntdll.dll" };
			if ( !map_system_images( *e_, f, names, sys, mlog, err ) ) return refuse( "system images: " + err );
			for ( auto& l : mlog ) log_.push_back( l );
			const process_identity id = make_identity( f, po.image_file_name );
			module_image self;
			self.name = po.image_file_name;
			self.path = id.image_path;
			self.base = map_.base;
			self.size = map_.size;
			self.entry = map_.entry;
			self.original_base = img_.image_base;
			self.img = img_;
			if ( file.size() >= uint64_t( img_.e_lfanew ) + 12 ) std::memcpy( &self.timestamp, file.data() + img_.e_lfanew + 8, 4 );   // IMAGE_FILE_HEADER TimeDateStamp
			modules_.clear();
			modules_.push_back( self );
			for ( auto& m : sys ) modules_.push_back( std::move( m ) );
			for ( const auto& m : modules_ )
				if ( m.is_dll ) env_lines_.push_back( "system image " + m.name + " at " + hex( m.base ) + ", SizeOfImage " + hex( m.size ) + ", entry " + hex( m.entry ) + " (as data)" );
			for ( const auto& m : modules_ )   // U2031: every image is a MEM_IMAGE section view of the process
				if ( !kernel_->add_image_view( m.base, m.size, m.name, err ) ) return refuse( "image view " + m.name + ": " + err );
			std::vector<std::string> plines;
			if ( !build_process_parameters( *e_, *kernel_, f, id, pstate_, plines, err ) ) return refuse( err );
			// the heap and the loader data: the environment's (--HLE, --Sys without the loader files) or the real loader's (the --Sys
			// loader start: ntdll's LdrpInitializeProcess creates the process heap and the loader lists itself)
			if ( !loader_start_ && ( !build_process_heap( *e_, *kernel_, pstate_, plines, err ) || !build_loader_data( *e_, *kernel_, modules_, pstate_, plines, err ) ) )
				return refuse( err );
			if ( loader_start_ && !build_kernel_views( f, plines, err ) ) return refuse( err );
			for ( auto& l : plines ) env_lines_.push_back( l );
			constexpr uint64_t peb_size = fields::peb_size;   // 7C8H: ProcessHeaps = PEB + sizeof(PEB) (MmCreatePeb, procsup.c:618-619)
			const uint64_t heaps = pb + peb_size, max_heaps = ( page_size - peb_size ) / 8;
			// CriticalSectionTimeout: the NT value 720 hours (ReactOS mminit.c:388 comment), negative = relative, 100 ns units
			const uint64_t cs_timeout = uint64_t( -int64_t( 720ull * 60 * 60 * 10000000ull ) );
			const uint32_t session = 1;   // the interactive session (session 0 hosts services); chosen
			if ( !w64( pb + 0x018, pstate_.ldr, 8, loader_start_ ? "PEB Ldr (18H) = 0: the real loader sets it" : "PEB Ldr (18H) = the loader data" ) ||
				 !w64( pb + 0x020, pstate_.parameters, 8, "PEB ProcessParameters (20H)" ) ||
				 !w64( pb + 0x030, pstate_.heap, 8, loader_start_ ? "PEB ProcessHeap (30H) = 0: the real loader creates it" : "PEB ProcessHeap (30H)" ) ||
				 !w64( pb + 0x0B8, f.processors, 4, "PEB NumberOfProcessors (B8H)" ) ||
				 !w64( pb + 0x0BC, 0, 4, "PEB NtGlobalFlag (BCH) = 0: no global flags, no debugger (MmCreatePeb copies the system's NtGlobalFlag)" ) ||
				 !w64( pb + 0x0C0, cs_timeout, 8, "PEB CriticalSectionTimeout (C0H)" ) || !w64( pb + 0x0C8, 0x100000, 8, "PEB HeapSegmentReserve (C8H, mminit.c:367)" ) ||
				 !w64( pb + 0x0D0, 0x2000, 8, "PEB HeapSegmentCommit (D0H, mminit.c:368)" ) ||
				 !w64( pb + 0x0D8, 0x10000, 8, "PEB HeapDeCommitTotalFreeThreshold (D8H, mminit.c:369)" ) ||
				 !w64( pb + 0x0E0, 0x1000, 8, "PEB HeapDeCommitFreeBlockThreshold (E0H, mminit.c:370)" ) ||
				 !w64( pb + 0x0E8, loader_start_ ? 0 : 1, 4, "PEB NumberOfHeaps (E8H)" ) || !w64( pb + 0x0EC, max_heaps, 4, "PEB MaximumNumberOfHeaps (ECH)" ) ||
				 !w64( pb + 0x0F0, heaps, 8, "PEB ProcessHeaps (F0H) = PEB + 7C8H" ) ||
				 ( !loader_start_ && !w64( heaps, pstate_.heap, 8, "PEB ProcessHeaps[0] = the process heap" ) ) ||
				 !w64( pb + 0x128, img_.subsystem, 4, "PEB ImageSubsystem (128H)" ) || !w64( pb + 0x12C, img_.subsystem_major, 4, "PEB ImageSubsystemMajorVersion (12CH)" ) ||
				 !w64( pb + 0x130, img_.subsystem_minor, 4, "PEB ImageSubsystemMinorVersion (130H)" ) ||
				 !w64( pb + 0x138, f.active_processor_mask, 8, "PEB ActiveProcessAffinityMask (138H)" ) || !w64( pb + 0x2C0, session, 4, "PEB SessionId (2C0H, chosen)" ) )
				return false;
			static_assert( fields::offset_of( fields::peb, "NumberOfProcessors" ) == 0xB8 && fields::offset_of( fields::peb, "NtGlobalFlag" ) == 0xBC &&
							   fields::offset_of( fields::peb, "CriticalSectionTimeout" ) == 0xC0 && fields::offset_of( fields::peb, "HeapSegmentReserve" ) == 0xC8 &&
							   fields::offset_of( fields::peb, "HeapSegmentCommit" ) == 0xD0 && fields::offset_of( fields::peb, "HeapDeCommitTotalFreeThreshold" ) == 0xD8 &&
							   fields::offset_of( fields::peb, "HeapDeCommitFreeBlockThreshold" ) == 0xE0 && fields::offset_of( fields::peb, "NumberOfHeaps" ) == 0xE8 &&
							   fields::offset_of( fields::peb, "MaximumNumberOfHeaps" ) == 0xEC && fields::offset_of( fields::peb, "ProcessHeaps" ) == 0xF0 &&
							   fields::offset_of( fields::peb, "ImageSubsystem" ) == 0x128 && fields::offset_of( fields::peb, "ImageSubsystemMajorVersion" ) == 0x12C &&
							   fields::offset_of( fields::peb, "ImageSubsystemMinorVersion" ) == 0x130 && fields::offset_of( fields::peb, "ActiveProcessAffinityMask" ) == 0x138 &&
							   fields::offset_of( fields::peb, "SessionId" ) == 0x2C0 && fields::offset_of( fields::peb, "Ldr" ) == 0x18 &&
							   fields::offset_of( fields::peb, "ProcessParameters" ) == 0x20 && fields::offset_of( fields::peb, "ProcessHeap" ) == 0x30,
						   "the PEB members written above (guest ntdll.pdb _PEB)" );
		}
		if ( loader_start_ )
		{
			// U2031: the first thread's start (Wine 11.0 dlls/ntdll/unix/signal_x86_64.c:2896-2973 init_syscall_frame, licensed OSS
			// reference; the main thread's entry = the image entry, its argument = the PEB: dlls/ntdll/unix/server.c:1768): a CONTEXT
			// below the stack top - Rcx = the entry, Rdx = the PEB, Rsp = StackBase - 28H, Rip = ntdll!RtlUserThreadStart, CS 33H,
			// DS / ES / GS / SS 2BH, FS 53H, EFlags 200H, the x87 control word 27FH, MXCSR 1F80H, ContextFlags CONTEXT_FULL - and the
			// thread enters ntdll!LdrInitializeThunk( CONTEXT ) with RSP = the CONTEXT - 8; LdrInitializeThunk continues at the CONTEXT
			// (NtContinue) when the process is initialized (dlls/ntdll/signal_x86_64.c:1039-1043)
			const module_image* nt = nullptr;
			for ( const auto& m : modules_ )
				if ( m.name == "ntdll.dll" ) nt = &m;
			if ( !nt ) return refuse( "--Sys loader start: ntdll.dll is not mapped" );
			auto export_va = [ & ]( const char* n ) -> uint64_t {
				for ( const auto& [ rva, name ] : nt->exports_by_rva )
					if ( name == n ) return nt->base + rva;
				return 0;
			};
			const uint64_t ldr_thunk = export_va( "LdrInitializeThunk" ), user_start = export_va( "RtlUserThreadStart" );
			if ( !ldr_thunk || !user_start ) return refuse( "--Sys loader start: ntdll.dll exports no LdrInitializeThunk / RtlUserThreadStart" );
			const uint64_t ctx_rsp_value = stack_base_ - 0x28;
			const uint64_t ctx = ( ctx_rsp_value & ~0xFull ) - fields::context_size;
			std::vector<uint8_t> c( size_t( fields::context_size ), 0 );
			auto put = [ & ]( uint32_t off, uint64_t v, unsigned n ) { std::memcpy( c.data() + off, &v, n ); };
			put( ctx_flags, context_full, 4 );
			put( ctx_mxcsr, 0x1F80, 4 );
			put( ctx_segcs, 0x33, 2 );
			put( ctx_segcs + 2, 0x2B, 2 );   // SegDs
			put( ctx_segcs + 4, 0x2B, 2 );   // SegEs
			put( ctx_segcs + 6, 0x53, 2 );   // SegFs
			put( ctx_segcs + 8, 0x2B, 2 );   // SegGs
			put( ctx_segcs + 10, 0x2B, 2 );  // SegSs
			put( ctx_eflags, 0x200, 4 );
			put( ctx_rax + 8, map_.entry, 8 );   // Rcx
			put( ctx_rax + 16, pb, 8 );          // Rdx
			put( ctx_rsp, ctx_rsp_value, 8 );
			put( ctx_rip, user_start, 8 );
			put( ctx_fltsave, 0x27F, 2 );        // FltSave.ControlWord (_XSAVE_FORMAT +0)
			put( ctx_fltsave + 0x18, 0x1F80, 4 );   // FltSave.MxCsr (_XSAVE_FORMAT +18H)
			if ( !w( ctx, c.data(), c.size(), "", "" ) ) return false;
			start_context_ = ctx;
			start_rsp_ = ctx - 8;
			ntdll_base_ = nt->base;
			start_va_ = ldr_thunk;
			env_lines_.push_back( "--Sys loader start: CONTEXT at " + hex( ctx ) + " (Rcx = the entry " + hex( map_.entry ) + ", Rdx = the PEB, Rsp " + hex( ctx_rsp_value ) +
								  ", Rip = ntdll!RtlUserThreadStart " + hex( user_start ) + "); the thread enters ntdll!LdrInitializeThunk " + hex( ldr_thunk ) + " with RCX = the CONTEXT, RDX = the ntdll base, RSP " +
								  hex( start_rsp_ ) );
		}
		if ( !imports.empty() && !loader_start_ )   // --HLE (U2009): the imports bound to the API stubs
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
		if ( has_tls && !loader_start_ )   // --HLE (U2010): the loader's TLS start - index 0, the thread's block from the template, the callbacks
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
		uint64_t begin = start_sysret_;
		if ( loader_start_ )
		{
			// U2031: the --Sys loader start enters user mode by IRETQ (RCX = the CONTEXT is kept): the frame RIP = LdrInitializeThunk,
			// CS 33H, RFLAGS 202H, RSP = the CONTEXT - 8, SS 2BH on the emulator kernel's stack page
			const uint64_t frame[ 5 ] = { start_va_, uint64_t( layout::gdt_r3_code | layout::rpl3 ), guest_initial_rflags, start_rsp_, uint64_t( layout::gdt_r3_data | layout::rpl3 ) };
			std::string werr;
			const uint64_t fva = guest_kernel_stack_va + page_size - sizeof( frame );
			// RDX = the system DLL's base: ReactOS 0.4.16 ntoskrnl/ps/thread.c:74-79 queues LdrInitializeThunk (PspSystemDllEntryPoint)
			// with SystemArgument1 = PspSystemDllBase - its second argument
			const uint64_t rcx = start_context_, rdx = ntdll_base_;
			if ( !e_->write( fva, frame, sizeof( frame ), werr ) || uc_reg_write( uc, UC_X86_REG_RSP, &fva ) != UC_ERR_OK || uc_reg_write( uc, UC_X86_REG_RCX, &rcx ) != UC_ERR_OK ||
				 uc_reg_write( uc, UC_X86_REG_RDX, &rdx ) != UC_ERR_OK )
			{
				error = "the IRETQ frame of the loader start: " + werr;
				log_.push_back( "FAILED: " + error );
				return false;
			}
			begin = iretq_stub_;
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
		if ( po_.boundary == env_boundary::hle )   // U2025: the system DLL images are data under --HLE
			for ( const auto& m : modules_ )
				if ( m.is_dll && uc_hook_add( uc, &h, UC_HOOK_CODE, (void*)on_sysimage_hle, hc_.get(), m.base, m.base + m.size - 1 ) != UC_ERR_OK )
				{
					error = "uc_hook_add (system image) failed";
					log_.push_back( "FAILED: " + error );
					return false;
				}
		// U2020: instrumentation - counters (CPUID / RDTSC / RDTSCP / RDPID, environment reads, the tail) and the trace file
		r.probes.enabled = po_.counters;
		hc_->counting = po_.counters;
		hc_->tail_next = 0;
		if ( po_.counters )
		{
			std::vector<std::pair<uint64_t, uint64_t>> ranges = { { guest_peb_va, page_size }, { guest_teb_va, guest_teb_size }, { guest_kuser_va, page_size } };
			if ( pstate_.ldr ) ranges.push_back( { pstate_.ldr, fields::peb_ldr_data_size } );   // U2025-U2027
			for ( auto en : pstate_.entries ) ranges.push_back( { en, fields::ldr_data_table_entry_size } );
			if ( pstate_.parameters ) ranges.push_back( { pstate_.parameters, fields::process_parameters_size } );
			if ( pstate_.environment ) ranges.push_back( { pstate_.environment, pstate_.environment_size } );
			if ( pstate_.heap ) ranges.push_back( { pstate_.heap, fields::heap_size } );
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
			// U2040: register-level lines (observation only: the memory hook reads, never writes guest state) / the ring of the last lines
			hc_->regs = po_.trace_regs;
			hc_->last = po_.trace_last;
			hc_->ring.clear();
			hc_->ring_next = 0;
			hc_->have_pending = false;
			if ( po_.trace_regs && uc_hook_add( uc, &h, UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, (void*)on_trace_mem, hc_.get(), 1, 0 ) != UC_ERR_OK )
			{
				error = "uc_hook_add (trace memory accesses) failed";
				log_.push_back( "FAILED: " + error );
				return false;
			}
			log_.push_back( "trace: one line per instruction (index, RIP, region +offset, bytes, Capstone text" +
							std::string( po_.trace_regs ? "; RAX..R15 / RFLAGS before the instruction; its memory reads / writes" : "" ) + ")" +
							( po_.trace_last ? " - only the last " + std::to_string( po_.trace_last ) + " lines, written at the stop" : std::string() ) + " -> " +
							po_.trace_path + " (at most " + std::to_string( po_.trace_max_bytes ) + " bytes)" );
		}
		log_.push_back( loader_start_ ? "run: IRETQ to ntdll!LdrInitializeThunk " + hex( start_va_ ) + " with RCX = the CONTEXT " + hex( start_context_ ) + " (the --Sys loader start), at most " + std::to_string( po_.max_insns ) + " instructions" : "run: SYSRETQ to " + ( start_va_ == map_.entry ? "the entry " + hex( map_.entry ) : "the TLS start trampoline " + hex( start_va_ ) + " (then the entry " + hex( map_.entry ) + ")" ) +
						" at CPL 3, at most " + std::to_string( po_.max_insns ) + " instructions" );
		// P41 (U2084): the console capture - under --Sys an entry hook on each output export of the capture's kernelbase (at its slot)
		hc_->con_hooks.clear();
		hc_->con_waits.clear();
		hc_->con.reset();
		hc_->con_dir = po_.console_dir;
		hc_->con_max = po_.console_max_bytes;
		hc_->con_failed = false;
		if ( loader_start_ && facts_.files && facts_.files->count( "kernelbase.dll" ) )
		{
			const auto& kb = facts_.files->at( "kernelbase.dll" );
			pe::image ki;
			std::vector<pe::export_entry> ex;
			std::string perr;
			if ( pe::parse( kb, ki, perr ) && pe::exports( kb, ki, ex, perr ) )
			{
				const uint64_t slot = system_image_slot( "kernelbase.dll" );
				for ( const auto& x : ex )
					if ( !x.forwarder && ( x.name == "WriteFile" || x.name == "WriteConsoleA" || x.name == "WriteConsoleW" || x.name == "OutputDebugStringA" || x.name == "OutputDebugStringW" ) )
						hc_->con_hooks[ slot + x.rva ] = x.name;
			}
			for ( const auto& [ va, n ] : hc_->con_hooks )
				if ( uc_hook_add( uc, &h, UC_HOOK_CODE, (void*)on_con_api, hc_.get(), va, va ) != UC_ERR_OK )
				{
					error = "uc_hook_add (console capture) failed";
					log_.push_back( "FAILED: " + error );
					return false;
				}
			log_.push_back( "console capture: " + std::to_string( hc_->con_hooks.size() ) + " output exports of kernelbase.dll hooked (WriteFile on a standard handle, WriteConsoleA / W, "
							"OutputDebugStringA / W) + NtWriteFile on a standard handle; observation only" );
		}
		// 4. the run: until the return sentinel, a stop of the hooks, or the instruction bound (no host time)
		ue = e_->emulate( begin, guest_return_sentinel, po_.max_insns );
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
		while ( !hc_->con_waits.empty() )   // P41 (U2084): attempts whose call had not returned when the run stopped
		{
			auto w = hc_->con_waits.back();
			hc_->con_waits.pop_back();
			con_record( *hc_, w.a );
		}
		if ( hc_->con ) hc_->con->close();
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
			trace_finish( *hc_ );   // U2040
			hc_->trace->close();
			hc_->trace.reset();
			log_.push_back( "trace: " + std::to_string( r.probes.trace_lines ) + " lines, " + std::to_string( r.probes.trace_bytes ) + " bytes" +
							( r.probes.trace_truncated ? " (TRUNCATED at the size limit)" : "" ) + " -> " + po_.trace_path );
		}
		return true;
	}

	// ------------------------------------------------------------------------------------------------ U2030: NtContinue
	// The CPU state of a CONTEXT (guest ntdll.pdb _CONTEXT; winnt.h CONTEXT_* flags): CONTEXT_CONTROL (Rip, Rsp, EFlags - sanitized:
	// only the user-modifiable flags, IF set) is required; CONTEXT_INTEGER loads the other 15 GPRs; CONTEXT_FLOATING_POINT loads MXCSR,
	// XMM0-15 and the x87 control word of FltSave. The segment selectors stay the user ones (CS 33H, SS / DS / ES / GS 2BH, FS 53H;
	// the bases are not changed: GS base = the TEB). Debug registers in the CONTEXT: a loud stop when any is non-zero (milestone M7).
	// The return to user mode is IRETQ from the emulator kernel's stack page.
	bool process::resume_context( uc_engine* uc, uint64_t ctx, std::string& why )
	{
		std::vector<uint8_t> c( size_t( fields::context_size ), 0 );
		if ( !e_->read( ctx, c.data(), c.size(), why ) ) return false;
		auto g = [ & ]( uint32_t off, unsigned n ) {
			uint64_t v = 0;
			std::memcpy( &v, c.data() + off, n );
			return v;
		};
		const uint32_t flags = uint32_t( g( ctx_flags, 4 ) );
		if ( ( flags & context_control ) != context_control )
		{
			why = "CONTEXT " + hex( ctx ) + " ContextFlags " + hex( flags ) + " without CONTEXT_CONTROL: not modelled";
			return false;
		}
		if ( ( flags & ( context_amd64 | 0x10 ) ) == ( context_amd64 | 0x10 ) )
			for ( uint32_t o = 0x48; o < 0x78; o += 8 )
				if ( g( o, 8 ) )
				{
					why = "CONTEXT " + hex( ctx ) + " sets debug registers (CONTEXT_DEBUG_REGISTERS): not modelled before milestone M7";
					return false;
				}
		if ( flags & ~uint32_t( context_full | context_segments | 0x10 | 0x40 ) )
		{
			why = "CONTEXT " + hex( ctx ) + " ContextFlags " + hex( flags ) + ": bits beyond CONTROL / INTEGER / SEGMENTS / FLOATING_POINT / DEBUG_REGISTERS / XSTATE not modelled";
			return false;
		}
		if ( ( flags & context_integer ) == context_integer )
		{
			static const int gpr[ 16 ] = { UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RBX, -1, UC_X86_REG_RBP, UC_X86_REG_RSI, UC_X86_REG_RDI,
										   UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_R10, UC_X86_REG_R11, UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15 };
			for ( int i = 0; i < 16; i++ )
			{
				if ( gpr[ i ] < 0 ) continue;   // RSP: CONTEXT_CONTROL
				const uint64_t v = g( ctx_rax + 8 * i, 8 );
				uc_reg_write( uc, gpr[ i ], &v );
			}
		}
		if ( ( flags & context_floating_point ) == context_floating_point )
		{
			const uint32_t mx = uint32_t( g( ctx_mxcsr, 4 ) );
			const uint16_t fcw = uint16_t( g( ctx_fltsave, 2 ) );
			uc_reg_write( uc, UC_X86_REG_MXCSR, &mx );
			uc_reg_write( uc, UC_X86_REG_FPCW, &fcw );
			for ( int i = 0; i < 16; i++ ) uc_reg_write( uc, UC_X86_REG_XMM0 + i, c.data() + ctx_xmm0 + 16 * i );
		}
		const uint64_t rflags = ( g( ctx_eflags, 4 ) & eflags_user_sanitize ) | 0x202;
		const uint64_t frame[ 5 ] = { g( ctx_rip, 8 ), uint64_t( layout::gdt_r3_code | layout::rpl3 ), rflags, g( ctx_rsp, 8 ), uint64_t( layout::gdt_r3_data | layout::rpl3 ) };
		const uint64_t fva = guest_kernel_stack_va + page_size - sizeof( frame );
		if ( !e_->write( fva, frame, sizeof( frame ), why ) ) return false;
		if ( uc_reg_write( uc, UC_X86_REG_RSP, &fva ) != UC_ERR_OK || uc_reg_write( uc, UC_X86_REG_RIP, &iretq_stub_ ) != UC_ERR_OK )
		{
			why = "register write failed";
			return false;
		}
		return true;
	}

	// ------------------------------------------------------------------------------------------------ U2031: the kernel's views
	// The NLS tables (one read-only view: the ANSI code-page file, the OEM one, the case table, each from a page boundary - ReactOS
	// 0.4.16 boot/freeldr/freeldr/ntldr/wlregistry.c:406-500 WinLdrLoadNLSData; mapped into every process and pointed at by the PEB,
	// ntoskrnl/mm/ARM3/procsup.c:588-594 MmCreatePeb) and the API set schema (the apisetschema.dll file mapped read-only, PEB ApiSetMap =
	// its ".apiset" section's raw data when the schema is version 6 and fits the section - Wine 11.0 dlls/ntdll/unix/loader.c:1780-1844)
	bool process::build_kernel_views( const environment_facts& f, std::vector<std::string>& lines, std::string& error )
	{
		const auto& m = f.manifest;
		std::vector<uint8_t> nls;
		uint64_t off[ 3 ] = {};
		int k = 0;
		for ( const std::string* n : { &m.nls_acp_file, &m.nls_oemcp_file, &m.nls_case_file } )
		{
			const auto it = f.files->find( *n );
			if ( it == f.files->end() )
			{
				error = "NLS file " + *n + " missing from the capture";
				return false;
			}
			off[ k++ ] = nls.size();
			nls.insert( nls.end(), it->second.begin(), it->second.end() );
			nls.resize( ( nls.size() + page_size - 1 ) & ~( page_size - 1 ), 0 );
		}
		uint64_t nls_base = 0;
		if ( !kernel_->map_data_view( nls, "NLS tables (" + m.nls_acp_file + ", " + m.nls_oemcp_file + ", " + m.nls_case_file + ")", nls_base, error ) ) return false;
		const auto& as = f.files->at( "apisetschema.dll" );
		pe::image ai;
		if ( !pe::parse( as, ai, error ) )
		{
			error = "apisetschema.dll: " + error;
			return false;
		}
		const pe::section* aps = nullptr;
		for ( const auto& s : ai.sections )
			if ( s.name == ".apiset" ) aps = &s;
		uint32_t version = 0, size = 0;
		if ( !aps || uint64_t( aps->raw_offset ) + 8 > as.size() )
		{
			error = "apisetschema.dll has no .apiset section";
			return false;
		}
		std::memcpy( &version, as.data() + aps->raw_offset, 4 );
		std::memcpy( &size, as.data() + aps->raw_offset + 4, 4 );
		if ( version != 6 || size > aps->virtual_size || uint64_t( aps->raw_offset ) + aps->virtual_size > as.size() )
		{
			error = "apisetschema.dll .apiset: version " + std::to_string( version ) + ", size " + hex( size ) + " - not a schema version 6 that fits its section";
			return false;
		}
		uint64_t as_base = 0;
		if ( !kernel_->map_data_view( as, "apisetschema.dll (data view)", as_base, error ) ) return false;
		const uint64_t pb = guest_peb_va;
		const uint64_t vals[] = { nls_base + off[ 0 ], nls_base + off[ 1 ], nls_base + off[ 2 ], as_base + aps->raw_offset };
		static_assert( fields::offset_of( fields::peb, "AnsiCodePageData" ) == 0xA0 && fields::offset_of( fields::peb, "OemCodePageData" ) == 0xA8 &&
						   fields::offset_of( fields::peb, "UnicodeCaseTableData" ) == 0xB0 && fields::offset_of( fields::peb, "ApiSetMap" ) == 0x68,
					   "_PEB NLS / ApiSetMap members" );
		const uint64_t offs[] = { 0xA0, 0xA8, 0xB0, 0x68 };
		for ( int i = 0; i < 4; i++ )
			if ( !e_->write( pb + offs[ i ], &vals[ i ], 8, error ) ) return false;
		lines.push_back( "NLS view at " + hex( nls_base ) + ": PEB AnsiCodePageData (A0H) " + hex( vals[ 0 ] ) + " (" + m.nls_acp_file + "), OemCodePageData (A8H) " + hex( vals[ 1 ] ) +
						 " (" + m.nls_oemcp_file + "), UnicodeCaseTableData (B0H) " + hex( vals[ 2 ] ) + " (" + m.nls_case_file + ")" );
		lines.push_back( "API set schema view at " + hex( as_base ) + ": PEB ApiSetMap (68H) = " + hex( vals[ 3 ] ) + " (.apiset raw data, schema version 6, size " + hex( size ) + ")" );
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
		if ( pick( "PEB", guest_peb_va, fields::peb, fields::peb_size ) || pick( "PEB", guest_peb_va, fields::peb, page_size ) ||
			 pick( "TEB", guest_teb_va, fields::teb, guest_teb_size ) || pick( "KUSER", guest_kuser_va, fields::kuser, page_size ) )
			return true;
		// U2025-U2027: the process state (loader data, entries, parameters, environment block, heap header)
		if ( pstate_.ldr && pick( "PEB_LDR_DATA", pstate_.ldr, fields::peb_ldr_data, fields::peb_ldr_data_size ) ) return true;
		for ( size_t i = 0; i < pstate_.entries.size() && i < modules_.size(); i++ )
			if ( pick( "LDR_ENTRY", pstate_.entries[ i ], fields::ldr_data_table_entry, fields::ldr_data_table_entry_size ) )
			{
				region = "LDR_ENTRY(" + modules_[ i ].name + ")";
				return true;
			}
		if ( pstate_.parameters && pick( "PARAMS", pstate_.parameters, fields::process_parameters, fields::process_parameters_size ) ) return true;
		if ( pstate_.environment && address >= pstate_.environment && address - pstate_.environment < pstate_.environment_size )
		{
			region = "ENVIRONMENT";
			field = "+" + hex( ( address - pstate_.environment ) & ~0x3Full );   // per 64-byte line of the block
			return true;
		}
		return pstate_.heap && pick( "HEAP", pstate_.heap, fields::heap, fields::heap_size );
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
		o.push_back( "probe: console output attempts " + std::to_string( r.console.size() ) + " (P41 console capture)" );
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

	// P41 (U2084): is HANDLE one of the process' standard handles (PEB->ProcessParameters StandardInput / Output / Error, read now) or 0
	bool process::std_handle( uc_engine* uc, uint64_t handle ) const
	{
		if ( handle == 0 ) return true;
		uint64_t params = 0, h[ 3 ] = {};
		if ( uc_mem_read( uc, guest_peb_va + 0x20, &params, 8 ) != UC_ERR_OK || !params || uc_mem_read( uc, params + 0x20, h, sizeof( h ) ) != UC_ERR_OK ) return false;
		return handle == h[ 0 ] || handle == h[ 1 ] || handle == h[ 2 ];
	}

	std::vector<std::string> process::console_summary( const run_result& r ) const
	{
		std::vector<std::string> o;
		o.push_back( "console capture: " + std::to_string( r.console.size() ) + " output attempt(s)" +
					 ( r.console_file.empty() ? std::string( r.console.empty() ? " (no file: nothing was written)" : " (no file: no capture folder)" ) : "; file " + r.console_file ) +
					 ( r.console_truncated ? " (truncated at the size bound)" : "" ) );
		for ( const auto& a : r.console )
			o.push_back( "console capture #" + std::to_string( a.index ) + ": insn " + std::to_string( a.insn ) + " " + a.via + " handle " + hex( a.handle ) + " length " + std::to_string( a.length ) +
						 " text \"" + a.text + "\" -> " + a.result );
		// the process parameters' console fields as the process left them (the model starts detached: ConsoleHandle -1, no standard
		// handles; a native detached process reads ConsoleHandle 0 after its kernelbase start - tm_nt con.pp)
		uc_engine* uc = e_->handle();
		uint64_t params = 0, f[ 5 ] = {};
		if ( uc_mem_read( uc, guest_peb_va + 0x20, &params, 8 ) == UC_ERR_OK && params && uc_mem_read( uc, params + 0x10, f, sizeof( f ) ) == UC_ERR_OK )
			o.push_back( "console fields at the stop: PEB->ProcessParameters " + hex( params ) + ( params == pstate_.parameters ? " (the environment's block)" : " (the process' copy)" ) +
						 ": ConsoleHandle " + hex( f[ 0 ] ) + ", ConsoleFlags " + hex( uint32_t( f[ 1 ] ) ) + ", StandardInput " + hex( f[ 2 ] ) + ", StandardOutput " + hex( f[ 3 ] ) +
						 ", StandardError " + hex( f[ 4 ] ) );
		return o;
	}
}
