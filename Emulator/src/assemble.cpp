// emu::assemble / emu::disassembler (plan 3.M3, ledger U1974): see include/emu/assemble.hpp.
#include <emu/assemble.hpp>

#include <capstone/capstone.h>
#include <keystone/keystone.h>

#include <cstdio>
#include <cstring>

namespace emu
{
	namespace
	{
		ks_engine* ks_open_decimal()
		{
			static const unsigned char want[] = { 0xB8, 0x0A, 0x00, 0x00, 0x00 };
			for ( int tries = 0; tries < 64; ++tries )
			{
				ks_engine* ks = nullptr;
				if ( ks_open( KS_ARCH_X86, KS_MODE_64, &ks ) != KS_ERR_OK ) return nullptr;
				unsigned char* enc = nullptr;
				size_t size = 0, count = 0;
				const bool decimal = ks_asm( ks, "mov eax, 10", 0, &enc, &size, &count ) == KS_ERR_OK && size == sizeof( want ) &&
									 !std::memcmp( enc, want, sizeof( want ) );
				if ( enc ) ks_free( enc );
				if ( decimal ) return ks;
				ks_close( ks );
			}
			return nullptr;
		}

		// the raw-byte classification of SYSCALL / SYSENTER / INT n (legacy prefixes and one REX byte skipped)
		void classify_bytes( const uint8_t* b, size_t n, insn_info& r )
		{
			size_t k = 0;
			while ( k < n && k < 14 && ( b[ k ] == 0x66 || b[ k ] == 0x67 || b[ k ] == 0xF2 || b[ k ] == 0xF3 || b[ k ] == 0x2E || b[ k ] == 0x3E ||
										  b[ k ] == 0x26 || b[ k ] == 0x36 || b[ k ] == 0x64 || b[ k ] == 0x65 || b[ k ] == 0xF0 ) )
				k++;
			if ( k < n && ( b[ k ] & 0xF0 ) == 0x40 ) k++;
			if ( k + 1 < n && b[ k ] == 0x0F && b[ k + 1 ] == 0x05 ) { r.is_system = true; r.system_kind = "syscall"; }
			else if ( k + 1 < n && b[ k ] == 0x0F && b[ k + 1 ] == 0x34 ) { r.is_system = true; r.system_kind = "sysenter"; }
			else if ( k + 1 < n && b[ k ] == 0xCD )
			{
				char t[ 16 ];
				std::snprintf( t, sizeof( t ), "int 0x%02x", b[ k + 1 ] );
				r.is_system = true;
				r.system_kind = t;
			}
		}
	}

	bool assemble( const std::string& text, uint64_t address, std::vector<uint8_t>& out, std::string& error )
	{
		out.clear();
		ks_engine* ks = ks_open_decimal();
		if ( !ks )
		{
			error = "ks_open: no Keystone engine that reads decimal immediates";
			return false;
		}
		unsigned char* enc = nullptr;
		size_t size = 0, count = 0;
		bool ok = true;
		if ( ks_asm( ks, text.c_str(), address, &enc, &size, &count ) != KS_ERR_OK )
		{
			error = std::string( "Keystone: " ) + ks_strerror( ks_errno( ks ) );
			ok = false;
		}
		else if ( size == 0 )
		{
			error = "Keystone produced no bytes";
			ok = false;
		}
		else
			out.assign( enc, enc + size );
		if ( enc ) ks_free( enc );
		ks_close( ks );
		return ok;
	}

	disassembler::disassembler()
	{
		csh h = 0;
		if ( cs_open( CS_ARCH_X86, CS_MODE_64, &h ) == CS_ERR_OK ) handle_ = size_t( h );
	}

	disassembler::~disassembler()
	{
		if ( handle_ )
		{
			csh h = csh( handle_ );
			cs_close( &h );
		}
	}

	insn_info disassembler::decode( const uint8_t* bytes, size_t n, uint64_t address ) const
	{
		insn_info r;
		r.address = address;
		if ( n > 15 ) n = 15;
		cs_insn* insn = nullptr;
		const size_t got = handle_ ? cs_disasm( csh( handle_ ), bytes, n, address, 1, &insn ) : 0;
		if ( got == 1 )
		{
			r.decoded = true;
			r.size = insn[ 0 ].size;
			r.text = std::string( insn[ 0 ].mnemonic ) + ( insn[ 0 ].op_str[ 0 ] ? std::string( " " ) + insn[ 0 ].op_str : std::string() );
			switch ( insn[ 0 ].id )
			{
			case X86_INS_CALL:
			case X86_INS_LCALL: r.is_call = true; break;
			case X86_INS_RET: r.is_ret = true; break;
			default: break;
			}
		}
		else
			r.text = "(not decoded by Capstone)";
		if ( insn ) cs_free( insn, got );
		classify_bytes( bytes, r.decoded ? r.size : n, r );
		return r;
	}
}
