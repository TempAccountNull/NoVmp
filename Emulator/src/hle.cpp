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
								   { "ExitProcess", api::exit_process }, { "GetModuleHandleW", api::get_module_handle_w }, { "GetModuleHandleA", api::get_module_handle_a } };
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
			const bool is_k32 = lower( m.dll ) == "kernel32.dll";
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

	result module::call( uint64_t address, uc_engine* uc, nt::kernel& k, uint64_t image_base, std::string& api_label, std::array<uint64_t, 4>& args )
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
			default:
				r.text = b.label() + " is not implemented in milestone M5 (the --HLE APIs of M5: kernel32.dll GetCurrentProcessId, GetCurrentThreadId, ExitProcess, "
									 "GetModuleHandleW / A( NULL ))";
				return r;
		}
	}
}
