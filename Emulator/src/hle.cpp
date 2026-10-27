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
								   { "WriteConsoleW", api::write_console_w }, { "GetLastError", api::get_last_error } };
		// P44 (U2123): user32.dll APIs (test PE T6)
		const known user32[] = { { "CharUpperW", api::char_upper_w } };
		// P45 (U2131): ntdll.dll's file-system system-service exports (test PE T7) - each IS the kernel layer's service of that name (D3: one
		// implementation; the export's own code is a system-call stub: its arguments are the stub's - RCX, RDX, R8, R9, [RSP+28H] ...)
		const known ntdll[] = { { "NtOpenFile", api::nt_file_service }, { "NtCreateFile", api::nt_file_service }, { "NtQueryAttributesFile", api::nt_file_service },
								{ "NtQueryFullAttributesFile", api::nt_file_service }, { "NtQueryDirectoryFile", api::nt_file_service },
								{ "NtQueryDirectoryFileEx", api::nt_file_service }, { "NtClose", api::nt_file_service } };
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
			default:
				r.text = b.label() + " is not implemented in milestone M5 (the --HLE APIs of M5: kernel32.dll GetCurrentProcessId, GetCurrentThreadId, ExitProcess, "
									 "GetModuleHandleW / A( NULL ); P41: GetStdHandle, WriteFile, WriteConsoleA / W, GetLastError; P44: user32.dll CharUpperW; "
									 "P45: ntdll.dll NtOpenFile, NtCreateFile, NtQueryAttributesFile, NtQueryFullAttributesFile, NtQueryDirectoryFile(Ex), NtClose)";
				return r;
		}
	}
}
