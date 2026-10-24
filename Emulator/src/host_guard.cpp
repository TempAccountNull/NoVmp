// emu::host_guard (plan 3.M1, ledger U1941): see include/emu/host_guard.hpp. Reads only (VirtualQuery + plain loads of
// committed readable pages under SEH, so a page that changes under the scan cannot crash the check).
#include <emu/host_guard.hpp>

#include <Windows.h>   // WIN32_LEAN_AND_MEAN / NOMINMAX come from the project

#include <cstdio>
#include <cstring>

namespace emu::host_guard
{
	namespace
	{
		// plain C helper (no objects with destructors) so __try / __except is allowed here: the number of occurrences of
		// pattern in [p, p + n), the addresses (up to max_hits) written to hits; on a fault the scan of this range stops
		size_t scan_range( const uint8_t* p, size_t n, const uint8_t* pattern, size_t length, uint64_t* hits, size_t max_hits )
		{
			size_t count = 0;
			__try
			{
				for ( size_t i = 0; i + length <= n; i++ )
				{
					if ( p[ i ] != pattern[ 0 ] ) continue;
					if ( std::memcmp( p + i, pattern, length ) != 0 ) continue;
					if ( count < max_hits ) hits[ count ] = uint64_t( uintptr_t( p + i ) );
					count++;
				}
			}
			__except ( EXCEPTION_EXECUTE_HANDLER )
			{
			}
			return count;
		}
	}

	bool is_executable( uint32_t protect )
	{
		const uint32_t base = protect & 0xFF;
		return base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
	}

	bool is_readable( uint32_t protect )
	{
		if ( protect & ( PAGE_GUARD | PAGE_NOACCESS ) ) return false;
		const uint32_t base = protect & 0xFF;
		return base == PAGE_READONLY || base == PAGE_READWRITE || base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READ ||
			   base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
	}

	std::vector<region> committed_regions()
	{
		std::vector<region> out;
		SYSTEM_INFO si = {};
		GetSystemInfo( &si );
		uint64_t a = uint64_t( uintptr_t( si.lpMinimumApplicationAddress ) );
		const uint64_t end = uint64_t( uintptr_t( si.lpMaximumApplicationAddress ) );
		while ( a < end )
		{
			MEMORY_BASIC_INFORMATION mbi = {};
			if ( VirtualQuery( reinterpret_cast<const void*>( uintptr_t( a ) ), &mbi, sizeof( mbi ) ) != sizeof( mbi ) ) break;
			const uint64_t rb = uint64_t( uintptr_t( mbi.BaseAddress ) );
			const uint64_t rs = uint64_t( mbi.RegionSize );
			if ( rs == 0 || rb + rs <= a ) break;   // no progress: stop instead of looping
			if ( mbi.State == MEM_COMMIT )
			{
				region r;
				r.base = rb;
				r.size = rs;
				r.protect = mbi.Protect;
				r.type = mbi.Type;
				out.push_back( r );
			}
			a = rb + rs;
		}
		return out;
	}

	std::vector<region> foreign_executable()
	{
		std::vector<region> out;
		for ( auto& r : committed_regions() )
			if ( is_executable( r.protect ) && r.type != MEM_IMAGE ) out.push_back( r );
		return out;
	}

	std::vector<uint64_t> find_pattern( const uint8_t* pattern, size_t length, bool executable_only )
	{
		std::vector<uint64_t> found;
		if ( !pattern || length == 0 ) return found;
		uint64_t hits[ 64 ];
		for ( auto& r : committed_regions() )
		{
			if ( !is_readable( r.protect ) ) continue;
			if ( executable_only && !is_executable( r.protect ) ) continue;
			const size_t n = scan_range( reinterpret_cast<const uint8_t*>( uintptr_t( r.base ) ), size_t( r.size ), pattern, length, hits, 64 );
			for ( size_t k = 0; k < n && k < 64; k++ ) found.push_back( hits[ k ] );
		}
		return found;
	}

	std::string describe( const region& r )
	{
		const char* t = r.type == MEM_IMAGE ? "image" : r.type == MEM_MAPPED ? "mapped" : r.type == MEM_PRIVATE ? "private" : "?";
		char b[ 160 ];
		std::snprintf( b, sizeof( b ), "base 0x%016llx size 0x%llx protect 0x%x type %s", ( unsigned long long ) r.base,
					   ( unsigned long long ) r.size, r.protect, t );
		return b;
	}
}
