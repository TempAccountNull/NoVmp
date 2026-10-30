// emu::hle (plan 3.M5, ledger U2009 / U2010): see include/emu/hle.hpp. Guest memory only; no host API is called for the guest.
#include <emu/hle.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace emu::hle
{
	namespace
	{
		std::string hex( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}

		std::string lower( std::string s )
		{
			for ( auto& c : s ) c = char( std::tolower( uint8_t( c ) ) );
			return s;
		}

		uint64_t reg( uc_engine* uc, int id )
		{
			uint64_t v = 0;
			uc_reg_read( uc, id, &v );
			return v;
		}

		struct known
		{
			const char* name;
			api id;
		};
		// kernel32.dll APIs of milestone M5 (the names as exported; compared exactly, the DLL name without case)
		const known kernel32[] = { { "GetCurrentProcessId", api::get_current_process_id }, { "GetCurrentThreadId", api::get_current_thread_id },
								   { "ExitProcess", api::exit_process }, { "GetModuleHandleW", api::get_module_handle_w }, { "GetModuleHandleA", api::get_module_handle_a },
								   // P41 (U2083): a process without a console prints
								   { "GetStdHandle", api::get_std_handle }, { "WriteFile", api::write_file }, { "WriteConsoleA", api::write_console_a },
								   { "WriteConsoleW", api::write_console_w }, { "GetLastError", api::get_last_error },
								   // P49 (U2257): the exception / debug-surface APIs of TX2 - TX6
								   { "AddVectoredExceptionHandler", api::add_vectored_exception_handler }, { "RemoveVectoredExceptionHandler", api::remove_vectored_exception_handler },
								   { "AddVectoredContinueHandler", api::add_vectored_continue_handler }, { "RemoveVectoredContinueHandler", api::remove_vectored_continue_handler },
								   { "IsDebuggerPresent", api::is_debugger_present }, { "CheckRemoteDebuggerPresent", api::check_remote_debugger_present },
								   { "GetCurrentProcess", api::get_current_process }, { "GetCurrentThread", api::get_current_thread },
								   { "GetThreadContext", api::get_thread_context }, { "GetProcessHeap", api::get_process_heap }, { "TlsAlloc", api::tls_alloc },
								   { "TlsSetValue", api::tls_set_value }, { "TlsFree", api::tls_free }, { "SetLastError", api::set_last_error } };
		// P44 (U2123): user32.dll APIs (test PE T6)
		const known user32[] = { { "CharUpperW", api::char_upper_w } };
		// P45 (U2131): ntdll.dll's file-system system-service exports (test PE T7) - each IS the kernel layer's service of that name (D3: one
		// implementation; the export's own code is a system-call stub: its arguments are the stub's - RCX, RDX, R8, R9, [RSP+28H] ...)
		const known ntdll[] = { { "NtOpenFile", api::nt_file_service }, { "NtCreateFile", api::nt_file_service }, { "NtQueryAttributesFile", api::nt_file_service },
								{ "NtQueryFullAttributesFile", api::nt_file_service }, { "NtQueryDirectoryFile", api::nt_file_service },
								{ "NtQueryDirectoryFileEx", api::nt_file_service }, { "NtClose", api::nt_file_service },
								// P46 (U2146): NtQueryInformationProcess (test PE T8: ProcessImageInformation) - the kernel layer's service the same way
								{ "NtQueryInformationProcess", api::nt_file_service },
								// P49 (U2257): TX5's NtQuerySystemInformation (SystemKernelDebuggerInformation) - the kernel layer's service the same way
								{ "NtQuerySystemInformation", api::nt_file_service }, { "NtGetContextThread", api::nt_file_service },
								// P55 (U2363): NtQueryInformationThread (ThreadBasicInformation, measured: tm_nt nqit) - the kernel layer's service the same way
								{ "NtQueryInformationThread", api::nt_file_service },
								// P56 (U2367): NtSetInformationThread (class 4 ThreadAffinityMask measured: tm_nt nsit; the other classes as the kernel layer answers them)
								{ "NtSetInformationThread", api::nt_file_service } };
		// P47 (U2168): bcrypt.dll's BCryptGenRandom (test PE T9)
		const known bcrypt[] = { { "BCryptGenRandom", api::bcrypt_gen_random } };
		// p3cm (U2284): cfgmgr32.dll's CM_Locate_DevNodeW (test PE T10)
		const known cfgmgr32[] = { { "CM_Locate_DevNodeW", api::cm_locate_devnode_w } };
	}

	std::string binding::label() const
	{
		return dll + "!" + ( name.empty() ? "#" + std::to_string( ordinal ) : name );
	}

	bool module::map_page( engine& e, std::string& error )
	{
		if ( mapped_ ) return true;
		if ( !e.map( module_va, module_size, UC_PROT_READ | UC_PROT_EXEC, "emulator HLE module (API stubs, start trampoline)", error ) ) return false;
		std::vector<uint8_t> fill( size_t( module_size ), 0xCC );   // INT3 everywhere a stub / the trampoline is not
		if ( !e.write( module_va, fill.data(), fill.size(), error ) ) return false;
		mapped_ = true;
		return true;
	}

	bool module::bind( engine& e, const std::vector<pe::import_module>& mods, uint64_t image_base, std::vector<std::string>& log, std::string& error )
	{
		if ( mods.empty() ) return true;
		if ( !map_page( e, error ) ) return false;
		for ( const auto& m : mods )
		{
			const bool is_k32 = lower( m.dll ) == "kernel32.dll", is_u32 = lower( m.dll ) == "user32.dll", is_nt = lower( m.dll ) == "ntdll.dll";
			const bool is_bc = lower( m.dll ) == "bcrypt.dll";   // P47 (U2168)
			const bool is_cm = lower( m.dll ) == "cfgmgr32.dll";   // p3cm (U2284)
			for ( const auto& en : m.entries )
			{
				if ( b_.size() >= max_stubs )
				{
					error = "more than " + std::to_string( max_stubs ) + " imports: the HLE page has no room";
					return false;
				}
				binding b;
				b.dll = m.dll;
				b.name = en.name;
				b.ordinal = en.ordinal;
				if ( is_k32 && !en.name.empty() )
					for ( const auto& k : kernel32 )
						if ( en.name == k.name ) b.id = k.id;
				if ( is_u32 && !en.name.empty() )
					for ( const auto& k : user32 )
						if ( en.name == k.name ) b.id = k.id;
				if ( is_nt && !en.name.empty() )   // P45 (U2131)
					for ( const auto& k : ntdll )
						if ( en.name == k.name ) b.id = k.id;
				if ( is_bc && !en.name.empty() )   // P47 (U2168)
					for ( const auto& k : bcrypt )
						if ( en.name == k.name ) b.id = k.id;
				if ( is_cm && !en.name.empty() )   // p3cm (U2284)
					for ( const auto& k : cfgmgr32 )
						if ( en.name == k.name ) b.id = k.id;
				b.stub = module_va + stub_size * b_.size();
				if ( image_base > UINT64_MAX - en.iat_rva )
				{
					error = "IAT slot address overflow";
					return false;
				}
				b.iat = image_base + en.iat_rva;
				const uint8_t ret = 0xC3;
				if ( !e.write( b.stub, &ret, 1, error ) || !e.write( b.iat, &b.stub, 8, error ) ) return false;
				log.push_back( "hle: " + b.label() + " -> stub " + hex( b.stub ) + " (IAT " + hex( b.iat ) + ")" +
							   ( b.id == api::unknown ? " - not implemented in milestone M5: a call stops loudly" : "" ) );
				b_.push_back( b );
			}
		}
		return true;
	}

	bool module::write_trampoline( engine& e, uint64_t image_base, const std::vector<uint64_t>& callbacks, uint64_t entry, uint64_t& start, std::string& error )
	{
		if ( !map_page( e, error ) ) return false;
		std::vector<uint8_t> c = { 0x48, 0x83, 0xEC, 0x28 };   // sub rsp, 28h: shadow space, RSP 16-aligned at each CALL (entry RSP = 8 mod 16)
		auto imm64 = [ & ]( uint64_t v ) { for ( int k = 0; k < 8; k++ ) c.push_back( uint8_t( v >> ( 8 * k ) ) ); };
		for ( uint64_t cb : callbacks )
		{
			c.insert( c.end(), { 0x48, 0xB9 } );   // mov rcx, DllHandle = the image base
			imm64( image_base );
			c.insert( c.end(), { 0xBA, 0x01, 0x00, 0x00, 0x00 } );   // mov edx, 1 = DLL_PROCESS_ATTACH (winnt.h)
			c.insert( c.end(), { 0x45, 0x33, 0xC0 } );              // xor r8d, r8d (Reserved)
			c.insert( c.end(), { 0x48, 0xB8 } );                    // mov rax, callback
			imm64( cb );
			c.insert( c.end(), { 0xFF, 0xD0 } );                    // call rax
		}
		c.insert( c.end(), { 0x48, 0x83, 0xC4, 0x28 } );   // add rsp, 28h: the entry's stack ([RSP] = the return sentinel)
		c.insert( c.end(), { 0x48, 0xB8 } );               // mov rax, entry
		imm64( entry );
		c.insert( c.end(), { 0xFF, 0xE0 } );               // jmp rax
		if ( c.size() > module_size - ( trampoline_va - module_va ) )
		{
			error = "start trampoline too large";
			return false;
		}
		if ( !e.write( trampoline_va, c.data(), c.size(), error ) ) return false;
		start = trampoline_va;
		return true;
	}

	result module::call( uint64_t address, uc_engine* uc, nt::kernel& k, uint64_t image_base, std::string& api_label, std::array<uint64_t, 4>& args, uint64_t insns )
	{
		result r;
		args = { reg( uc, UC_X86_REG_RCX ), reg( uc, UC_X86_REG_RDX ), reg( uc, UC_X86_REG_R8 ), reg( uc, UC_X86_REG_R9 ) };
		if ( address < module_va || ( address - module_va ) % stub_size || ( address - module_va ) / stub_size >= b_.size() )
		{
			api_label = "(no stub)";
			r.text = "execution reached " + hex( address ) + " in the HLE page outside a stub";
			return r;
		}
		const binding& b = b_[ size_t( ( address - module_va ) / stub_size ) ];
		api_label = b.label();
		const auto& f = k.facts();
		switch ( b.id )
		{
			case api::get_current_process_id:
				r.kind = result_kind::returned;
				r.rax = uint32_t( f.process_id );
				r.text = "returns the process identifier " + hex( r.rax );
				return r;
			case api::get_current_thread_id:
				r.kind = result_kind::returned;
				r.rax = uint32_t( f.thread_id );
				r.text = "returns the thread identifier " + hex( r.rax );
				return r;
			case api::exit_process:
			{
				// the one kernel layer: ExitProcess = NtTerminateProcess( NtCurrentProcess(), uExitCode )
				nt::call c;
				c.name = "NtTerminateProcess";
				c.args = { nt::current_process, uint32_t( args[ 0 ] ), 0, 0, 0, 0 };
				const nt::reply rep = k.dispatch( c );
				if ( rep.kind != nt::reply_kind::terminated )
				{
					r.text = "ExitProcess: the kernel layer did not terminate: " + rep.text;
					return r;
				}
				r.kind = result_kind::terminated;
				r.exit_status = rep.exit_status;
				r.text = "ExitProcess( " + hex( r.exit_status ) + " ) -> the kernel layer: " + rep.text;
				return r;
			}
			case api::get_module_handle_w:
			case api::get_module_handle_a:
				if ( args[ 0 ] != 0 )
				{
					r.text = b.label() + "( " + hex( args[ 0 ] ) + " ): a module name - no module list in milestone M5 (only NULL, the executable, is modelled)";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = image_base;
				r.text = "NULL -> the executable's base address " + hex( image_base );
				return r;
			case api::get_std_handle:
			{
				// the process parameters' StandardInput / Output / Error (RTL_USER_PROCESS_PARAMETERS +20H / +28H / +30H; the PEB's
				// ProcessParameters at +20H); LastError unchanged (measured: tm_nt con.std.*)
				const uint32_t which = uint32_t( args[ 0 ] );
				const int slot = which == uint32_t( -10 ) ? 0 : which == uint32_t( -11 ) ? 1 : which == uint32_t( -12 ) ? 2 : -1;
				if ( slot < 0 )
				{
					r.text = "GetStdHandle( " + hex( which ) + " ): not STD_INPUT / OUTPUT / ERROR_HANDLE - not modelled";
					return r;
				}
				uint64_t params = 0, h = 0;
				if ( uc_mem_read( uc, f.peb + 0x20, &params, 8 ) != UC_ERR_OK || !params || uc_mem_read( uc, params + 0x20 + 8ull * slot, &h, 8 ) != UC_ERR_OK )
				{
					r.text = "GetStdHandle: the process parameters are not readable";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = h;
				r.text = "GetStdHandle( " + std::string( slot == 0 ? "STD_INPUT_HANDLE" : slot == 1 ? "STD_OUTPUT_HANDLE" : "STD_ERROR_HANDLE" ) + " ) = " + hex( h ) +
						 ( h ? std::string() : " (no standard handles: a process started without a console; LastError unchanged - measured: tm_nt con.std)" );
				return r;
			}
			case api::write_file:
			case api::write_console_a:
			case api::write_console_w:
			{
				const bool file = b.id == api::write_file;
				r.console = true;
				r.con_handle = args[ 0 ];
				r.con_buffer = args[ 1 ];
				r.con_length = uint32_t( args[ 2 ] );
				r.con_wide = b.id == api::write_console_w;
				if ( args[ 0 ] != 0 )
				{
					r.text = b.label() + "( " + hex( args[ 0 ] ) + ", ... ): a handle other than the no-console standard handle 0 is not modelled";
					r.con_result = "not answered (stop)";
					return r;
				}
				uint32_t lasterr = 6;   // ERROR_INVALID_HANDLE (shared\winerror.h)
				std::string how;
				if ( file )
				{
					// *lpNumberOfBytesWritten = 0 first (measured: written 0), then the ONE kernel layer: NtWriteFile( 0, ... )
					if ( args[ 3 ] )
					{
						const uint32_t zero = 0;
						if ( !k.user_buffer( args[ 3 ], 4, UC_PROT_WRITE ) || uc_mem_write( uc, args[ 3 ], &zero, 4 ) != UC_ERR_OK )
						{
							r.text = "WriteFile: lpNumberOfBytesWritten is not writable - not modelled";
							r.con_result = "not answered (stop)";
							return r;
						}
					}
					nt::call c;
					c.name = "NtWriteFile";
					c.args = { 0, 0, 0, 0, 0, args[ 1 ], uint32_t( args[ 2 ] ), 0, 0, 0, 0, 0 };
					c.stack_args = true;
					const nt::reply rep = k.dispatch( c );
					if ( rep.kind != nt::reply_kind::done || rep.status != 0xC0000008 )
					{
						r.text = "WriteFile: the kernel layer's NtWriteFile did not answer STATUS_INVALID_HANDLE: " + rep.text;
						r.con_result = "not answered (stop)";
						return r;
					}
					how = "NtWriteFile( 0 ) = STATUS_INVALID_HANDLE -> ";
				}
				if ( uc_mem_write( uc, f.teb + 0x68, &lasterr, 4 ) != UC_ERR_OK )   // TEB LastErrorValue (68H)
				{
					r.text = b.label() + ": the TEB is not writable";
					r.con_result = "not answered (stop)";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = 0;
				r.con_result = "FALSE, LastError 6 (ERROR_INVALID_HANDLE)" + std::string( file ? ", *lpNumberOfBytesWritten 0" : ", the written count not written" );
				r.text = b.label() + "( 0, " + hex( args[ 1 ] ) + ", " + std::to_string( uint32_t( args[ 2 ] ) ) + ", " + hex( args[ 3 ] ) + " ): " + how + r.con_result +
						 " (a process without a console; measured: tm_nt " + ( file ? "con.api.wfile" : "con.api.wcon" ) + ")";
				return r;
			}
			case api::get_last_error:
			{
				uint32_t v = 0;
				if ( uc_mem_read( uc, f.teb + 0x68, &v, 4 ) != UC_ERR_OK )
				{
					r.text = "GetLastError: the TEB is not readable";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = v;
				r.text = "the TEB's LastErrorValue " + hex( v );
				return r;
			}
			case api::char_upper_w:
			{
				// P44 (U2123): LPWSTR CharUpperW( LPWSTR lpsz ) - Windows SDK um\winuser.h; Learn "CharUpperW function": a value whose high-order
				// word is zero is ONE character (returned converted in the low word), else a NUL-terminated string converted in place and the
				// pointer returned. The conversion here: the ASCII letters a..z (the characters of T6's string; their upper case is the same in
				// every locale) - another non-ASCII character stops (not modelled: the locale's case table); at most 1000H characters.
				const uint64_t p = args[ 0 ];
				if ( !( p >> 16 ) )
				{
					const uint16_t ch = uint16_t( p );
					if ( ch >= 0x80 )
					{
						r.text = "CharUpperW( the character " + hex( ch ) + " ): not ASCII - the locale's case table is not modelled under --HLE";
						return r;
					}
					r.kind = result_kind::returned;
					r.rax = ( ch >= 'a' && ch <= 'z' ) ? uint64_t( ch - 0x20 ) : uint64_t( ch );
					r.text = "CharUpperW( the character " + hex( ch ) + " ) = " + hex( r.rax );
					return r;
				}
				std::string conv;
				for ( uint32_t i = 0; i < 0x1000; i++ )
				{
					uint16_t ch = 0;
					if ( uc_mem_read( uc, p + 2ull * i, &ch, 2 ) != UC_ERR_OK )
					{
						r.text = "CharUpperW( " + hex( p ) + " ): character " + std::to_string( i ) + " is not readable";
						return r;
					}
					if ( !ch )
					{
						r.kind = result_kind::returned;
						r.rax = p;
						r.text = "CharUpperW( " + hex( p ) + " ): " + std::to_string( i ) + " characters converted in place (\"" + conv + "\"), the pointer returned";
						return r;
					}
					if ( ch >= 0x80 )
					{
						r.text = "CharUpperW( " + hex( p ) + " ): character " + std::to_string( i ) + " (" + hex( ch ) + ") is not ASCII - the locale's case table is not modelled under --HLE";
						return r;
					}
					if ( ch >= 'a' && ch <= 'z' )
					{
						ch = uint16_t( ch - 0x20 );
						if ( uc_mem_write( uc, p + 2ull * i, &ch, 2 ) != UC_ERR_OK )
						{
							r.text = "CharUpperW( " + hex( p ) + " ): character " + std::to_string( i ) + " is not writable";
							return r;
						}
					}
					conv += char( ch );
				}
				r.text = "CharUpperW( " + hex( p ) + " ): no NUL within 1000H characters - not modelled";
				return r;
			}
			case api::nt_file_service:
			{
				// P45 (U2131): the kernel layer's service of the export's name with the stub's arguments (RCX, RDX, R8, R9, then [RSP+28H] ..
				// [RSP+60H]: at the stub RSP holds the return address, as at the SYSCALL of the real stub) - the same answer as under --Sys
				nt::call c;
				c.name = b.name;
				c.args[ 0 ] = args[ 0 ];
				c.args[ 1 ] = args[ 1 ];
				c.args[ 2 ] = args[ 2 ];
				c.args[ 3 ] = args[ 3 ];
				c.insns = insns;
				const uint64_t rsp = reg( uc, UC_X86_REG_RSP );
				uint64_t st[ 8 ] = {};
				if ( rsp <= UINT64_MAX - 0x68 && uc_mem_read( uc, rsp + 0x28, st, sizeof( st ) ) == UC_ERR_OK )
				{
					for ( int i = 0; i < 8; i++ ) c.args[ size_t( 4 + i ) ] = st[ i ];
					c.stack_args = true;
				}
				const nt::reply rep = k.dispatch( c );
				if ( rep.kind != nt::reply_kind::done )
				{
					r.text = b.label() + ": the kernel layer did not answer: " + rep.text;
					return r;
				}
				char s[ 16 ];
				std::snprintf( s, sizeof( s ), "%08X", rep.status );
				r.kind = result_kind::returned;
				r.rax = rep.status;
				r.text = b.label() + " -> the kernel layer: status " + s + "H: " + rep.text;
				return r;
			}
			case api::bcrypt_gen_random:
			{
				// P47 (U2168): NTSTATUS BCryptGenRandom( BCRYPT_ALG_HANDLE hAlgorithm, PUCHAR pbBuffer, ULONG cbBuffer, ULONG dwFlags ) - Windows SDK
				// shared\bcrypt.h; Learn "BCryptGenRandom function": with BCRYPT_USE_SYSTEM_PREFERRED_RNG (2) hAlgorithm must be NULL, the buffer
				// is filled with random bytes, STATUS_SUCCESS. Under --HLE bcrypt's code does not run: the API's documented result - cbBuffer
				// bytes of the environment's seeded generator (the kernel layer's environment_random, the stream of the RNG device under --Sys;
				// never host entropy), STATUS_SUCCESS (test PE T9: native 3900007FH). Another algorithm handle / flag, a length of 0 or above
				// 10000H, an unwritable buffer: not modelled - stop.
				const uint32_t cb = uint32_t( args[ 2 ] ), flags = uint32_t( args[ 3 ] );
				if ( args[ 0 ] != 0 || flags != 2 || cb == 0 || cb > 0x10000 || !k.user_buffer( args[ 1 ], cb, UC_PROT_WRITE ) )
				{
					r.text = "BCryptGenRandom( " + hex( args[ 0 ] ) + ", " + hex( args[ 1 ] ) + ", " + hex( cb ) + ", " + hex( flags ) + " ): only NULL with "
							 "BCRYPT_USE_SYSTEM_PREFERRED_RNG and a writable buffer of 1 .. 10000H bytes is modelled";
					return r;
				}
				std::vector<uint8_t> buf( cb );
				k.environment_random( buf.data(), buf.size() );
				if ( uc_mem_write( uc, args[ 1 ], buf.data(), buf.size() ) != UC_ERR_OK )
				{
					r.text = "BCryptGenRandom: writing the buffer failed after the probe";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = 0;
				r.text = "BCryptGenRandom( NULL, " + hex( args[ 1 ] ) + ", " + hex( cb ) + ", BCRYPT_USE_SYSTEM_PREFERRED_RNG ): " + hex( cb ) +
						 " bytes of the environment's seeded generator (CTR_DRBG), STATUS_SUCCESS";
				return r;
			}
			case api::add_vectored_exception_handler:
			case api::add_vectored_continue_handler:
			{
				// P49 (U2257): PVOID AddVectoredExceptionHandler / AddVectoredContinueHandler( ULONG First, PVECTORED_EXCEPTION_HANDLER Handler ):
				// the handle = the address of a 10H-byte record {Handler, First} in a guest page this layer allocates (the kernel layer's
				// NtAllocateVirtualMemory core); First != 0 = the head of the list
				const bool cont = b.id == api::add_vectored_continue_handler;
				if ( !args[ 1 ] )
				{
					r.text = b.label() + "( " + hex( args[ 0 ] ) + ", NULL ): a NULL handler is not modelled";
					return r;
				}
				if ( !handle_page_ || handle_used_ + 0x10 > 0x1000 )
				{
					uint64_t base = 0, size = 0x1000;
					const nt::reply ra = k.allocate( base, size, nt::mem::commit | nt::mem::reserve, nt::page::readwrite );
					if ( ra.kind != nt::reply_kind::done || ra.status )
					{
						r.text = b.label() + ": the handler records' page: " + ra.text;
						return r;
					}
					handle_page_ = base;
					handle_used_ = 0;
				}
				const uint64_t h = handle_page_ + handle_used_;
				const uint64_t recd[ 2 ] = { args[ 1 ], uint32_t( args[ 0 ] ) };
				if ( uc_mem_write( uc, h, recd, sizeof( recd ) ) != UC_ERR_OK )
				{
					r.text = b.label() + ": the handler record is not writable";
					return r;
				}
				handle_used_ += 0x10;
				auto& list = cont ? vch_ : veh_;
				const vectored v{ h, args[ 1 ] };
				if ( uint32_t( args[ 0 ] ) ) list.insert( list.begin(), v );
				else list.push_back( v );
				r.kind = result_kind::returned;
				r.rax = h;
				r.text = b.label() + "( " + hex( uint32_t( args[ 0 ] ) ) + ", " + hex( args[ 1 ] ) + " ) = the handle " + hex( h ) +
						 ( uint32_t( args[ 0 ] ) ? " (the head of the list)" : " (the tail)" ) + ", " + std::to_string( list.size() ) + " handler(s)";
				return r;
			}
			case api::remove_vectored_exception_handler:
			case api::remove_vectored_continue_handler:
			{
				auto& list = b.id == api::remove_vectored_continue_handler ? vch_ : veh_;
				const auto it = std::find_if( list.begin(), list.end(), [ & ]( const vectored& v ) { return v.handle == args[ 0 ]; } );
				r.kind = result_kind::returned;
				r.rax = it != list.end() ? 1 : 0;
				if ( it != list.end() ) list.erase( it );
				r.text = b.label() + "( " + hex( args[ 0 ] ) + " ) = " + std::to_string( r.rax ) + ( r.rax ? " (removed)" : " (not a handle of the list)" );
				return r;
			}
			case api::is_debugger_present:
			{
				uint8_t bd = 0;
				if ( uc_mem_read( uc, f.peb + 2, &bd, 1 ) != UC_ERR_OK )
				{
					r.text = "IsDebuggerPresent: the PEB is not readable";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = bd;
				r.text = "the PEB's BeingDebugged " + hex( bd );
				return r;
			}
			case api::check_remote_debugger_present:
			{
				if ( args[ 0 ] != nt::current_process || !args[ 1 ] || !k.user_buffer( args[ 1 ], 4, UC_PROT_WRITE ) )
				{
					r.text = "CheckRemoteDebuggerPresent( " + hex( args[ 0 ] ) + ", " + hex( args[ 1 ] ) + " ): only NtCurrentProcess() with a writable BOOL is modelled";
					return r;
				}
				// the kernel layer's ProcessDebugPort into the handler-record page's scratch slot (no guest-visible allocation)
				if ( !handle_page_ || handle_used_ + 0x10 > 0x1000 )
				{
					uint64_t base = 0, size = 0x1000;
					const nt::reply ra = k.allocate( base, size, nt::mem::commit | nt::mem::reserve, nt::page::readwrite );
					if ( ra.kind != nt::reply_kind::done || ra.status )
					{
						r.text = "CheckRemoteDebuggerPresent: scratch memory: " + ra.text;
						return r;
					}
					handle_page_ = base;
					handle_used_ = 0;
				}
				const uint64_t scratch = handle_page_ + 0xFF0;
				nt::call c;
				c.name = "NtQueryInformationProcess";
				c.args = { nt::current_process, 7, scratch, 8, 0, 0, 0, 0, 0, 0, 0, 0 };
				c.stack_args = true;
				const nt::reply rep = k.dispatch( c );
				uint64_t port = 0;
				if ( rep.kind != nt::reply_kind::done || rep.status || uc_mem_read( uc, scratch, &port, 8 ) != UC_ERR_OK )
				{
					r.text = "CheckRemoteDebuggerPresent: the kernel layer's ProcessDebugPort: " + rep.text;
					return r;
				}
				const uint32_t present = port != 0;
				if ( uc_mem_write( uc, args[ 1 ], &present, 4 ) != UC_ERR_OK )
				{
					r.text = "CheckRemoteDebuggerPresent: *pbDebuggerPresent not writable after the probe";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = 1;
				r.text = "CheckRemoteDebuggerPresent( NtCurrentProcess(), " + hex( args[ 1 ] ) + " ): ProcessDebugPort " + hex( port ) + " -> *pbDebuggerPresent = " +
						 std::to_string( present ) + ", TRUE";
				return r;
			}
			case api::get_current_process:
			case api::get_current_thread:
				r.kind = result_kind::returned;
				r.rax = b.id == api::get_current_process ? ~0ull : ~1ull;
				r.text = std::string( b.id == api::get_current_process ? "the pseudo handle (HANDLE)-1" : "the pseudo handle (HANDLE)-2" );
				return r;
			case api::get_thread_context:
			{
				nt::call c;
				c.name = "NtGetContextThread";
				c.args = { args[ 0 ], args[ 1 ], 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
				c.insns = insns;
				const nt::reply rep = k.dispatch( c );
				if ( rep.kind != nt::reply_kind::done || rep.status )
				{
					r.text = "GetThreadContext: the kernel layer's NtGetContextThread did not succeed (" + rep.text + ") - the LastError mapping is not modelled";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = 1;
				r.text = "GetThreadContext -> the kernel layer's NtGetContextThread: " + rep.text;
				return r;
			}
			case api::get_process_heap:
			{
				uint64_t heap = 0;
				if ( uc_mem_read( uc, f.peb + 0x30, &heap, 8 ) != UC_ERR_OK )
				{
					r.text = "GetProcessHeap: the PEB is not readable";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = heap;
				r.text = "the PEB's ProcessHeap " + hex( heap );
				return r;
			}
			case api::tls_alloc:
			{
				unsigned i = 0;
				while ( i < 64 && ( tls_used_ >> i & 1 ) ) i++;
				if ( i == 64 )
				{
					r.text = "TlsAlloc: the 64 TEB slots are in use - the expansion slots are not modelled";
					return r;
				}
				const uint64_t zero = 0;
				if ( uc_mem_write( uc, f.teb + 0x1480 + 8ull * i, &zero, 8 ) != UC_ERR_OK )
				{
					r.text = "TlsAlloc: the TEB is not writable";
					return r;
				}
				tls_used_ |= 1ull << i;
				r.kind = result_kind::returned;
				r.rax = i;
				r.text = "TLS index " + std::to_string( i ) + " (its TEB slot zeroed)";
				return r;
			}
			case api::tls_set_value:
			case api::tls_free:
			{
				const uint32_t i = uint32_t( args[ 0 ] );
				if ( i >= 64 || !( tls_used_ >> i & 1 ) )
				{
					r.text = b.label() + "( " + std::to_string( i ) + " ): not an index TlsAlloc returned - not modelled";
					return r;
				}
				if ( b.id == api::tls_free ) tls_used_ &= ~( 1ull << i );
				else if ( uc_mem_write( uc, f.teb + 0x1480 + 8ull * i, &args[ 1 ], 8 ) != UC_ERR_OK )
				{
					r.text = "TlsSetValue: the TEB is not writable";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = 1;
				r.text = b.id == api::tls_free ? "TLS index " + std::to_string( i ) + " freed" : "TEB TlsSlots[" + std::to_string( i ) + "] = " + hex( args[ 1 ] );
				return r;
			}
			case api::set_last_error:
			{
				const uint32_t v = uint32_t( args[ 0 ] );
				if ( uc_mem_write( uc, f.teb + 0x68, &v, 4 ) != UC_ERR_OK )
				{
					r.text = "SetLastError: the TEB is not writable";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = 0;
				r.text = "the TEB's LastErrorValue := " + hex( v );
				return r;
			}
			case api::cm_locate_devnode_w:
			{
				// p3cm (U2284): CONFIGRET CM_Locate_DevNodeW( PDEVINST pdnDevInst, DEVINSTID_W pDeviceID, ULONG ulFlags ) - Windows SDK
				// um\cfgmgr32.h:3204-3208 (CR_SUCCESS 0, CM_LOCATE_DEVNODE_NORMAL 0); Learn "CM_Locate_DevNodeW function": pDeviceID NULL = the
				// root of the device tree. Under --HLE cfgmgr32's code does not run: the result of its own code under --Sys (test PE T10
				// exploration e5, the emulated trace): CR_SUCCESS and DEVINST 1 - cfgmgr32 resolves the root locally ("HTREE\ROOT\0", the first
				// entry of its own string table; cfgmgr32.dll+C26E writes it) without any request to \Device\DeviceApi\CMApi - no device-tree
				// content and no identifier. Any other device ID / flag, an unwritable pointer: not modelled - stop (a device-tree query needs the
				// user's decision).
				const uint32_t flags = uint32_t( args[ 2 ] );
				if ( args[ 1 ] != 0 || flags != 0 || !k.user_buffer( args[ 0 ], 4, UC_PROT_WRITE ) )
				{
					r.text = "CM_Locate_DevNodeW( " + hex( args[ 0 ] ) + ", " + hex( args[ 1 ] ) + ", " + hex( flags ) + " ): only the root (pDeviceID NULL, "
							 "CM_LOCATE_DEVNODE_NORMAL, a writable DEVINST) is modelled - another device ID is a device-tree query (the user's decision)";
					return r;
				}
				const uint32_t dn = 1;
				if ( uc_mem_write( uc, args[ 0 ], &dn, 4 ) != UC_ERR_OK )
				{
					r.text = "CM_Locate_DevNodeW: writing the DEVINST failed after the probe";
					return r;
				}
				r.kind = result_kind::returned;
				r.rax = 0;
				r.text = "CM_Locate_DevNodeW( " + hex( args[ 0 ] ) + ", NULL, CM_LOCATE_DEVNODE_NORMAL ): the root - DEVINST 1, CR_SUCCESS (cfgmgr32's own result under --Sys)";
				return r;
			}
			default:
				r.text = b.label() + " is not implemented in milestone M5 (the --HLE APIs of M5: kernel32.dll GetCurrentProcessId, GetCurrentThreadId, ExitProcess, "
									 "GetModuleHandleW / A( NULL ); P41: GetStdHandle, WriteFile, WriteConsoleA / W, GetLastError; P44: user32.dll CharUpperW; "
									 "P45: ntdll.dll NtOpenFile, NtCreateFile, NtQueryAttributesFile, NtQueryFullAttributesFile, NtQueryDirectoryFile(Ex), NtClose; "
									 "P46: ntdll.dll NtQueryInformationProcess; P47: bcrypt.dll BCryptGenRandom; p3cm: cfgmgr32.dll CM_Locate_DevNodeW( root ))";
				return r;
		}
	}
}
