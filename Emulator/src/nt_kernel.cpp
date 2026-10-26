// emu::nt (plan 3.M5, ledger U2007): see include/emu/nt_kernel.hpp. Every guest value is checked with 64-bit arithmetic; a
// combination the documentation does not answer stops loudly; nothing reaches the host kernel.
#include <emu/nt_kernel.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace emu::nt
{
	namespace
	{
		std::string hex( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}

		bool round_up( uint64_t v, uint64_t a, uint64_t& out )
		{
			if ( v > UINT64_MAX - ( a - 1 ) ) return false;
			out = ( v + a - 1 ) & ~( a - 1 );
			return true;
		}

		reply stopped( const std::string& why )
		{
			reply r;
			r.kind = reply_kind::stop;
			r.text = why;
			return r;
		}

		reply answered( uint32_t st, const std::string& text )
		{
			reply r;
			r.kind = reply_kind::done;
			r.status = st;
			r.text = text;
			return r;
		}
	}

	bool uc_prot_of_page( uint32_t protect, uint32_t& p )
	{
		switch ( protect )
		{
			case page::noaccess: p = UC_PROT_NONE; return true;
			case page::readonly: p = UC_PROT_READ; return true;
			case page::readwrite: p = UC_PROT_READ | UC_PROT_WRITE; return true;
			// no execute-only pages on x86-64: "Data may be read from any user-mode address with a protection key for which read
			// access is permitted." (253668-093 Vol3A 5.6.1 p. 5-35, user-mode accesses) - an executable user page is readable
			case page::execute: p = UC_PROT_READ | UC_PROT_EXEC; return true;
			case page::execute_read: p = UC_PROT_READ | UC_PROT_EXEC; return true;
			case page::execute_readwrite: p = UC_PROT_ALL; return true;
			default: return false;
		}
	}

	std::string page_text( uint32_t protect )
	{
		switch ( protect )
		{
			case 0: return "0";
			case page::noaccess: return "PAGE_NOACCESS";
			case page::readonly: return "PAGE_READONLY";
			case page::readwrite: return "PAGE_READWRITE";
			case page::writecopy: return "PAGE_WRITECOPY";
			case page::execute: return "PAGE_EXECUTE";
			case page::execute_read: return "PAGE_EXECUTE_READ";
			case page::execute_readwrite: return "PAGE_EXECUTE_READWRITE";
			case page::execute_writecopy: return "PAGE_EXECUTE_WRITECOPY";
			default: return hex( protect );
		}
	}

	// ------------------------------------------------------------------------------------------------ helpers
	bool kernel::user_buffer( uint64_t address, uint64_t length, uint32_t need ) const
	{
		if ( length == 0 ) return true;
		const uint64_t last = address + ( length - 1 );
		if ( last < address || address < f_.min_app_address || last > f_.max_app_address ) return false;
		return e_.accessible( address, length, need );
	}

	bool kernel::get64( uint64_t address, uint64_t& v ) const
	{
		std::string err;
		v = 0;
		return e_.read( address, &v, 8, err );
	}

	bool kernel::put( uint64_t address, const void* data, uint64_t length, reply& r )
	{
		std::string err;
		if ( !e_.write( address, data, length, err ) ) return false;
		uint64_t v = 0;
		std::memcpy( &v, data, size_t( std::min<uint64_t>( length, 8 ) ) );
		r.writes.push_back( "[" + hex( address ) + "] " + std::to_string( length ) + ( length <= 8 ? " = " + hex( v ) : std::string( " bytes" ) ) );
		return true;
	}

	allocation* kernel::owner( uint64_t address, uint64_t length )
	{
		for ( auto& [ b, a ] : allocs_ )
			if ( address >= a.base && length <= a.size && address - a.base <= a.size - length ) return &a;
		return nullptr;
	}

	bool kernel::find_free( uint64_t span, bool top_down, uint64_t& base, uint64_t limit_low, uint64_t limit_high, uint64_t align ) const
	{
		// U2035: the limits / alignment of NtAllocateVirtualMemoryEx's MEM_ADDRESS_REQUIREMENTS (0 = the user range / the granularity)
		const uint64_t g = align ? align : f_.allocation_granularity;
		const uint64_t user_max = limit_high ? std::min( limit_high, f_.max_app_address ) : f_.max_app_address;
		uint64_t lo = 0;
		if ( !round_up( std::max( f_.min_app_address, limit_low ), g, lo ) || span == 0 || user_max < lo || span - 1 > user_max - lo ) return false;
		auto regions = e_.regions();   // everything mapped (environment, views, committed private pages) ...
		for ( const auto& [ ab, a ] : allocs_ ) regions.push_back( { a.base, a.size, 0, "private reservation" } );   // ... + the reservations (U2035)
		for ( const auto& [ vb, v ] : views_ ) regions.push_back( { v.base, v.size, 0, "section view" } );   // ... + whole views, reserved parts too (P40, U2065)
		auto overlaps = [ & ]( uint64_t b ) {
			const uint64_t l = b + ( span - 1 );
			for ( const auto& r : regions )
				if ( b <= r.base + ( r.size - 1 ) && r.base <= l ) return &r;
			return static_cast<const mapped_region*>( nullptr );
		};
		if ( !top_down )
		{
			uint64_t c = lo;
			for ( int guard = 0; guard < 100000; guard++ )
			{
				if ( c > user_max || span - 1 > user_max - c ) return false;
				const mapped_region* hit = overlaps( c );
				if ( !hit )
				{
					base = c;
					return true;
				}
				uint64_t next = 0;
				if ( !round_up( hit->base + hit->size, g, next ) || next <= c ) return false;
				c = next;
			}
			return false;
		}
		uint64_t c = ( user_max + 1 - span ) & ~( g - 1 );
		for ( int guard = 0; guard < 100000; guard++ )
		{
			if ( c < lo ) return false;
			const mapped_region* hit = overlaps( c );
			if ( !hit )
			{
				base = c;
				return true;
			}
			if ( hit->base < lo + span ) return false;
			c = ( hit->base - span ) & ~( g - 1 );
		}
		return false;
	}

	bool kernel::commit_pages( allocation& a, uint64_t first, uint64_t count, uint32_t protect, std::string& why )
	{
		uint32_t ucp = 0;
		uc_prot_of_page( protect, ucp );
		for ( uint64_t i = first; i < first + count; i++ )
			if ( a.pages[ size_t( i ) ] && a.pages[ size_t( i ) ] != protect )
			{
				why = "re-commit of the committed page " + hex( a.base + i * f_.page_size ) + " (" + page_text( a.pages[ size_t( i ) ] ) + ") with " + page_text( protect ) +
					  ": Windows' result is not documented (VirtualAlloc: committing a committed page does not fail) - not modelled";
				return false;
			}
		// U2035: a reservation is address space only (not mapped in the engine: an access faults, as in Windows); committing maps each
		// run of new pages with the protection and zero contents
		const std::vector<uint8_t> zero( size_t( f_.page_size ), 0 );
		for ( uint64_t i = first; i < first + count; )
		{
			if ( a.pages[ size_t( i ) ] )
			{
				i++;
				continue;
			}
			uint64_t j = i;
			while ( j < first + count && !a.pages[ size_t( j ) ] ) j++;
			const uint64_t va = a.base + i * f_.page_size, n = ( j - i ) * f_.page_size;
			if ( !e_.map( va, n, UC_PROT_READ | UC_PROT_WRITE, "private allocation (committed)", why ) ) return false;
			for ( uint64_t k = 0; k < j - i; k++ )
				if ( !e_.write( va + k * f_.page_size, zero.data(), f_.page_size, why ) ) return false;   // a new page reads zero
			if ( !e_.protect( va, n, ucp, why ) ) return false;
			for ( uint64_t k = i; k < j; k++ ) a.pages[ size_t( k ) ] = protect;
			i = j;
		}
		return true;
	}

	bool kernel::decommit_pages( allocation& a, uint64_t first, uint64_t count, std::string& why )
	{
		// U2035: a decommitted page is unmapped again (reserved address space only)
		for ( uint64_t i = first; i < first + count; )
		{
			if ( !a.pages[ size_t( i ) ] )
			{
				i++;
				continue;
			}
			uint64_t j = i;
			while ( j < first + count && a.pages[ size_t( j ) ] ) j++;
			if ( !e_.unmap_range( a.base + i * f_.page_size, ( j - i ) * f_.page_size, why ) ) return false;
			for ( uint64_t k = i; k < j; k++ ) a.pages[ size_t( k ) ] = 0;
			i = j;
		}
		return true;
	}

	// ------------------------------------------------------------------------------------------------ U2031: section views
	view* kernel::view_of( uint64_t a )
	{
		for ( auto& [ b, v ] : views_ )
			if ( a >= v.base && a - v.base < v.size ) return &v;
		return nullptr;
	}

	bool kernel::add_image_view( uint64_t base, uint64_t size, const std::string& name, std::string& why )
	{
		const uint64_t ps = f_.page_size;
		if ( !size || ( base & ( ps - 1 ) ) || ( size & ( ps - 1 ) ) || view_of( base ) )
		{
			why = "image view [" + hex( base ) + ", +" + hex( size ) + ") not page-aligned or overlapping";
			return false;
		}
		view v;
		v.base = base;
		v.size = size;
		v.type = mem2::image;
		v.allocation_protect = page::execute_writecopy;
		v.name = name;
		const auto regions = e_.regions();
		for ( uint64_t a = base; a < base + size; a += ps )
		{
			uint32_t p = 0;   // a hole between sections: reserved (no access)
			for ( const auto& r : regions )
				if ( a >= r.base && a - r.base < r.size )
				{
					const bool rd = r.prot & UC_PROT_READ, wr = r.prot & UC_PROT_WRITE, ex = r.prot & UC_PROT_EXEC;
					p = ex ? ( wr ? page::execute_writecopy : page::execute_read ) : wr ? page::writecopy : rd ? page::readonly : page::noaccess;
				}
			v.pages.push_back( p );
		}
		views_[ base ] = v;
		return true;
	}

	bool kernel::map_data_view( const std::vector<uint8_t>& bytes, const std::string& name, uint64_t& base, std::string& why )
	{
		uint64_t size = 0, span = 0;
		if ( bytes.empty() || !round_up( bytes.size(), f_.page_size, size ) || !round_up( size, f_.allocation_granularity, span ) || !find_free( span, true, base ) )
		{
			why = "no free range for the view " + name;
			return false;
		}
		if ( !e_.map( base, size, UC_PROT_READ | UC_PROT_WRITE, name, why ) ) return false;
		std::vector<uint8_t> padded( bytes );
		padded.resize( size_t( size ), 0 );
		if ( !e_.write( base, padded.data(), size, why ) || !e_.protect( base, size, UC_PROT_READ, why ) ) return false;
		view v;
		v.base = base;
		v.size = size;
		v.type = mem2::mapped;
		v.allocation_protect = page::readonly;
		v.pages.assign( size_t( size / f_.page_size ), page::readonly );
		v.name = name;
		views_[ base ] = v;
		return true;
	}

	std::vector<std::string> kernel::describe() const
	{
		std::vector<std::string> out;
		for ( const auto& [ b, v ] : views_ )
			out.push_back( std::string( "view " ) + ( v.type == mem2::image ? "MEM_IMAGE" : "MEM_MAPPED" ) + " [" + hex( v.base ) + ", +" + hex( v.size ) + ") " + v.name );
		out.push_back( "private allocations: " + std::to_string( allocs_.size() ) );
		for ( const auto& [ b, a ] : allocs_ )
		{
			uint64_t committed = 0;
			for ( auto p : a.pages ) committed += p != 0;
			out.push_back( "  [" + hex( a.base ) + ", +" + hex( a.size ) + ") reserved " + page_text( a.allocation_protect ) + ", " + std::to_string( committed ) + " of " +
						   std::to_string( a.pages.size() ) + " page(s) committed" );
		}
		return out;
	}

	// ------------------------------------------------------------------------------------------------ the VM core
	reply kernel::allocate( uint64_t& base, uint64_t& size, uint32_t type, uint32_t protect, uint64_t limit_low, uint64_t limit_high, uint64_t align )
	{
		const uint32_t known = mem::commit | mem::reserve | mem::top_down;
		if ( type & ~known )
			return stopped( "AllocationType " + hex( type ) + ": bits other than MEM_COMMIT / MEM_RESERVE / MEM_TOP_DOWN are not modelled in milestone M5" );
		if ( !( type & ( mem::commit | mem::reserve ) ) )
			return stopped( "AllocationType " + hex( type ) + " has neither MEM_COMMIT nor MEM_RESERVE: Windows' status is not documented - not modelled" );
		if ( protect & ( page::guard | page::nocache | page::writecombine ) )
			return stopped( "Protect " + hex( protect ) + ": the modifiers PAGE_GUARD / PAGE_NOCACHE / PAGE_WRITECOMBINE are not modelled (guard pages: milestone M7)" );
		uint32_t ucp = 0;
		if ( !uc_prot_of_page( protect, ucp ) )
		{
			if ( protect == page::writecopy || protect == page::execute_writecopy )
				return stopped( "Protect " + page_text( protect ) + " for private memory: VirtualAlloc documents it as not supported, the status is not documented - not modelled" );
			return answered( status::invalid_page_protection, "Protect " + hex( protect ) + " is no page-protection value (ZwAllocateVirtualMemory: STATUS_INVALID_PAGE_PROTECTION)" );
		}
		if ( size == 0 ) return stopped( "*RegionSize 0: Windows' status is not documented - not modelled" );
		const uint64_t ps = f_.page_size, g = f_.allocation_granularity;
		if ( base == 0 || ( type & mem::reserve ) )
		{
			uint64_t start = 0, end = 0, pages_size = 0;
			if ( base == 0 )
			{
				// "If the initial value of this parameter is NULL, the operating system will determine where to allocate the region"
				// (ZwAllocateVirtualMemory); a NULL base with MEM_COMMIT alone reserves and commits (VirtualAlloc)
				if ( !round_up( size, ps, pages_size ) ) return stopped( "*RegionSize " + hex( size ) + " overflows when rounded to pages" );
				uint64_t span = 0;
				if ( !round_up( pages_size, g, span ) || !find_free( span, ( type & mem::top_down ) != 0, start, limit_low, limit_high, align ) )
					return stopped( "no free range of " + hex( pages_size ) + " bytes in the user range [" + hex( f_.min_app_address ) + ", " + hex( f_.max_app_address ) +
									"] (Windows' status for an exhausted address space is not modelled)" );
				end = start + pages_size;
			}
			else
			{
				// "If the memory is being reserved, the specified address is rounded down to the nearest multiple of the allocation
				// granularity" (VirtualAlloc); the end rounded up to a page
				start = base & ~( g - 1 );
				if ( base > UINT64_MAX - size || !round_up( base + size, ps, end ) ) return stopped( "*BaseAddress + *RegionSize above 2^64" );
				if ( start < f_.min_app_address || end - 1 > f_.max_app_address )
					return stopped( "reservation [" + hex( start ) + ", " + hex( end ) + ") outside the user range: Windows' status is not documented - not modelled" );
				for ( const auto& r : e_.regions() )
					if ( start <= r.base + ( r.size - 1 ) && r.base <= end - 1 )
						return answered( status::conflicting_addresses, "reservation [" + hex( start ) + ", " + hex( end ) + ") overlaps " + r.name +
																			" (ZwAllocateVirtualMemory: STATUS_CONFLICTING_ADDRESSES)" );
				for ( const auto& [ ab, a ] : allocs_ )   // U2035: reservations are not mapped - check them too
					if ( start <= a.base + ( a.size - 1 ) && a.base <= end - 1 )
						return answered( status::conflicting_addresses, "reservation [" + hex( start ) + ", " + hex( end ) + ") overlaps the reservation at " + hex( a.base ) +
																			" (ZwAllocateVirtualMemory: STATUS_CONFLICTING_ADDRESSES)" );
				pages_size = end - start;
			}
			std::string why;
			if ( pages_size > ( 64ull << 30 ) ) return stopped( "reserve of " + hex( pages_size ) + " bytes: above the 64 GiB this layer models" );
			allocation a;
			a.base = start;
			a.size = pages_size;
			a.allocation_protect = protect;
			a.pages.assign( size_t( pages_size / ps ), 0 );
			auto& ins = allocs_[ start ] = a;
			if ( ( type & mem::commit ) && !commit_pages( ins, 0, ins.pages.size(), protect, why ) ) return stopped( "commit: " + why );
			base = start;
			size = pages_size;
			return answered( status::success, std::string( ( type & mem::commit ) ? "reserved and committed" : "reserved" ) + " [" + hex( start ) + ", +" + hex( pages_size ) +
												  ") " + page_text( protect ) );
		}
		// MEM_COMMIT at a given address: "fails unless the entire range has already been reserved" (VirtualAlloc)
		const uint64_t first = base & ~( ps - 1 );
		uint64_t end = 0;
		if ( base > UINT64_MAX - size || !round_up( base + size, ps, end ) ) return stopped( "*BaseAddress + *RegionSize above 2^64" );
		allocation* a = owner( first, end - first );
		if ( !a )
			return stopped( "MEM_COMMIT of [" + hex( first ) + ", " + hex( end ) + ") which is not inside one reservation of this layer: Windows' status is not "
							"documented (VirtualAlloc: it fails) - not modelled" );
		std::string why;
		if ( !commit_pages( *a, ( first - a->base ) / ps, ( end - first ) / ps, protect, why ) ) return stopped( "commit: " + why );
		base = first;
		size = end - first;
		return answered( status::success, "committed [" + hex( first ) + ", +" + hex( end - first ) + ") " + page_text( protect ) );
	}

	// ------------------------------------------------------------------------------------------------ the services
	reply kernel::dispatch( const call& c )
	{
		if ( c.name.empty() ) return stopped( "unknown system service " + hex( c.number ) );
		if ( c.name == "NtTerminateProcess" ) return svc_terminate_process( c );
		if ( c.name == "NtClose" ) return svc_close( c );
		if ( c.name == "NtAllocateVirtualMemory" ) return svc_allocate_virtual_memory( c );
		if ( c.name == "NtFreeVirtualMemory" ) return svc_free_virtual_memory( c );
		if ( c.name == "NtQueryVirtualMemory" ) return svc_query_virtual_memory( c );
		if ( c.name == "NtQueryInformationProcess" ) return svc_query_information_process( c );
		if ( c.name == "NtContinue" )
		{
			// U2030: NtContinue( ContextRecord, TestAlert ) - continue at the CONTEXT (Wine 11.0 dlls/ntdll/signal_x86_64.c:1039-1043:
			// LdrInitializeThunk ends by continuing at its CONTEXT; the caller restores the state); no APC is ever queued here, so
			// TestAlert delivers nothing
			if ( !user_buffer( c.args[ 0 ], context_size, UC_PROT_READ ) )
				return answered( status::access_violation, "ContextRecord " + hex( c.args[ 0 ] ) + ": not readable user memory (probe)" );
			reply r;
			r.kind = reply_kind::resume;
			r.context = c.args[ 0 ];
			r.text = "continue at the CONTEXT " + hex( c.args[ 0 ] ) + ( c.args[ 1 ] & 0xFF ? " (TestAlert: no APC queued)" : "" );
			return r;
		}
		reply more;
		if ( dispatch_more( c, more ) ) return more;   // U2032: the services of the --Sys process start (nt_kernel_sys.cpp)
		return stopped( c.name + " (" + hex( c.number ) + ") is known but not implemented in milestone M5 (the services of M5: NtTerminateProcess, NtClose, "
										   "NtAllocateVirtualMemory, NtFreeVirtualMemory, NtQueryVirtualMemory, NtQueryInformationProcess, NtContinue + those of "
										   "nt_kernel_sys.cpp)" );
	}

	reply kernel::svc_terminate_process( const call& c )
	{
		if ( c.args[ 0 ] != current_process )
			return stopped( "NtTerminateProcess with ProcessHandle " + hex( c.args[ 0 ] ) + ": only NtCurrentProcess() is modelled (no handle table; a NULL handle's "
							"semantics are not documented)" );
		reply r;
		r.kind = reply_kind::terminated;
		r.exit_status = uint32_t( c.args[ 1 ] );
		r.text = "process terminated, ExitStatus " + hex( r.exit_status ) + " (ProcessHandle = NtCurrentProcess())";
		return r;
	}

	reply kernel::svc_close( const call& c )
	{
		const uint64_t h = c.args[ 0 ];
		if ( h >= ~0ull - 15 )
			return stopped( "NtClose of the pseudo handle " + hex( h ) + ": Windows' answer is not documented for ZwClose - not modelled" );
		// U2032: a handle of this process' table closes (STATUS_SUCCESS)
		if ( const auto it = handles_.find( h ); it != handles_.end() )
		{
			const std::string what = it->second.kind + " \"" + it->second.name + "\"";
			handles_.erase( it );
			return answered( status::success, "handle " + hex( h ) + " (" + what + ") closed" );
		}
		// km\wdm.h ZwClose: "STATUS_INVALID_HANDLE if Handle is not a valid handle"
		return answered( status::invalid_handle, "handle " + hex( h ) + " is not a handle of this process" );
	}

	reply kernel::svc_allocate_virtual_memory( const call& c )
	{
		if ( c.args[ 0 ] != current_process ) return stopped( "NtAllocateVirtualMemory for ProcessHandle " + hex( c.args[ 0 ] ) + ": only NtCurrentProcess() is modelled" );
		if ( !c.stack_args ) return stopped( "NtAllocateVirtualMemory: the stack arguments (AllocationType, Protect) are not readable" );
		const uint64_t pbase = c.args[ 1 ], psize = c.args[ 3 ];
		if ( !user_buffer( pbase, 8, UC_PROT_READ | UC_PROT_WRITE ) || !user_buffer( psize, 8, UC_PROT_READ | UC_PROT_WRITE ) )
			return answered( status::access_violation, "BaseAddress " + hex( pbase ) + " / RegionSize " + hex( psize ) + ": not writable user memory (probe)" );
		if ( c.args[ 2 ] != 0 ) return stopped( "ZeroBits " + hex( c.args[ 2 ] ) + ": not modelled in milestone M5" );
		uint64_t base = 0, size = 0;
		if ( !get64( pbase, base ) || !get64( psize, size ) ) return answered( status::access_violation, "BaseAddress / RegionSize not readable" );
		reply r = allocate( base, size, uint32_t( c.args[ 4 ] ), uint32_t( c.args[ 5 ] ) );
		if ( r.kind == reply_kind::done && r.status == status::success )
			if ( !put( pbase, &base, 8, r ) || !put( psize, &size, 8, r ) ) return stopped( "writing *BaseAddress / *RegionSize failed after the probe" );
		return r;
	}

	reply kernel::svc_free_virtual_memory( const call& c )
	{
		if ( c.args[ 0 ] != current_process ) return stopped( "NtFreeVirtualMemory for ProcessHandle " + hex( c.args[ 0 ] ) + ": only NtCurrentProcess() is modelled" );
		const uint64_t pbase = c.args[ 1 ], psize = c.args[ 2 ];
		const uint32_t type = uint32_t( c.args[ 3 ] );
		if ( !user_buffer( pbase, 8, UC_PROT_READ | UC_PROT_WRITE ) || !user_buffer( psize, 8, UC_PROT_READ | UC_PROT_WRITE ) )
			return answered( status::access_violation, "BaseAddress " + hex( pbase ) + " / RegionSize " + hex( psize ) + ": not writable user memory (probe)" );
		uint64_t base = 0, size = 0;
		if ( !get64( pbase, base ) || !get64( psize, size ) ) return answered( status::access_violation, "BaseAddress / RegionSize not readable" );
		const uint64_t ps = f_.page_size;
		std::string why;
		if ( type == mem::release )
		{
			// ZwFreeVirtualMemory: MEM_RELEASE needs *RegionSize 0 and *BaseAddress = the base returned when the region was reserved
			if ( size != 0 )
			{
				// U2035: MEM_RELEASE of a part (Wine 11.0 dlls/ntdll/unix/virtual.c NtFreeVirtualMemory: the page range inside ONE
				// reservation - beyond its end STATUS_UNABLE_TO_FREE_VM, in none STATUS_MEMORY_NOT_ALLOCATED; free_pages releases the
				// range and the rest of the reservation stays as separate reservation(s) with their own base)
				const uint64_t first = base & ~( ps - 1 );
				uint64_t end = 0;
				if ( base > UINT64_MAX - size || !round_up( base + size, ps, end ) ) return stopped( "*BaseAddress + *RegionSize above 2^64" );
				allocation* a = owner( first, ps );
				if ( !a ) return answered( 0xC00000A0, "MEM_RELEASE of " + hex( base ) + ": in no reservation (STATUS_MEMORY_NOT_ALLOCATED)" );
				if ( end - a->base > a->size ) return answered( 0xC000001A, "MEM_RELEASE of [" + hex( first ) + ", " + hex( end ) + ") beyond the reservation's end (STATUS_UNABLE_TO_FREE_VM)" );
				allocation whole = *a;
				const size_t i0 = size_t( ( first - whole.base ) / ps ), i1 = size_t( ( end - whole.base ) / ps );
				if ( !decommit_pages( *a, i0, i1 - i0, why ) ) return stopped( "release: " + why );
				whole = *a;
				allocs_.erase( whole.base );
				if ( i0 > 0 )
				{
					allocation lo = whole;
					lo.size = i0 * ps;
					lo.pages.assign( whole.pages.begin(), whole.pages.begin() + std::ptrdiff_t( i0 ) );
					allocs_[ lo.base ] = lo;
				}
				if ( i1 < whole.pages.size() )
				{
					allocation hi = whole;
					hi.base = whole.base + i1 * ps;
					hi.size = whole.size - i1 * ps;
					hi.pages.assign( whole.pages.begin() + std::ptrdiff_t( i1 ), whole.pages.end() );
					allocs_[ hi.base ] = hi;
				}
				const uint64_t rsize = end - first;
				reply r = answered( status::success, "released the part [" + hex( first ) + ", +" + hex( rsize ) + ") of the reservation at " + hex( whole.base ) );
				if ( !put( pbase, &first, 8, r ) || !put( psize, &rsize, 8, r ) ) return stopped( "writing *BaseAddress / *RegionSize failed after the probe" );
				return r;
			}
			const auto it = allocs_.find( base );
			if ( it == allocs_.end() )
				return stopped( "MEM_RELEASE of " + hex( base ) + " size 0: not the base of a reservation - Windows' status is not documented - not modelled" );
			const uint64_t rsize = it->second.size;
			if ( !decommit_pages( it->second, 0, it->second.pages.size(), why ) ) return stopped( "release: " + why );
			allocs_.erase( it );
			reply r = answered( status::success, "released [" + hex( base ) + ", +" + hex( rsize ) + ")" );
			if ( !put( pbase, &base, 8, r ) || !put( psize, &rsize, 8, r ) ) return stopped( "writing *BaseAddress / *RegionSize failed after the probe" );
			return r;
		}
		if ( type == mem::decommit )
		{
			uint64_t first = 0, end = 0;
			allocation* a = nullptr;
			if ( size == 0 )
			{
				// VirtualFree: "If dwSize is 0 and lpAddress is the base address returned by VirtualAlloc, the function decommits the entire region"
				const auto it = allocs_.find( base );
				if ( it == allocs_.end() ) return stopped( "MEM_DECOMMIT with size 0 of " + hex( base ) + ", not the base of a reservation: not modelled" );
				a = &it->second;
				first = a->base;
				end = a->base + a->size;
			}
			else
			{
				first = base & ~( ps - 1 );
				if ( base > UINT64_MAX - size || !round_up( base + size, ps, end ) ) return stopped( "*BaseAddress + *RegionSize above 2^64" );
				a = owner( first, end - first );
				if ( !a ) return stopped( "MEM_DECOMMIT of [" + hex( first ) + ", " + hex( end ) + ") which is not inside one reservation: not modelled" );
			}
			if ( !decommit_pages( *a, ( first - a->base ) / ps, ( end - first ) / ps, why ) ) return stopped( "decommit: " + why );
			const uint64_t rsize = end - first;
			reply r = answered( status::success, "decommitted [" + hex( first ) + ", +" + hex( rsize ) + ")" );
			if ( !put( pbase, &first, 8, r ) || !put( psize, &rsize, 8, r ) ) return stopped( "writing *BaseAddress / *RegionSize failed after the probe" );
			return r;
		}
		return stopped( "FreeType " + hex( type ) + ": only MEM_DECOMMIT / MEM_RELEASE are modelled" );
	}

	reply kernel::svc_query_virtual_memory( const call& c )
	{
		if ( c.args[ 0 ] != current_process ) return stopped( "NtQueryVirtualMemory for ProcessHandle " + hex( c.args[ 0 ] ) + ": only NtCurrentProcess() is modelled" );
		if ( !c.stack_args ) return stopped( "NtQueryVirtualMemory: the stack arguments (Length, ReturnLength) are not readable" );
		if ( uint32_t( c.args[ 2 ] ) == 6 ) return query_memory_image( c );   // U2033: MemoryImageInformation (nt_kernel_sys.cpp)
		if ( uint32_t( c.args[ 2 ] ) == 4 ) return query_working_set_ex( c );   // U2033: MemoryWorkingSetExInformation (nt_kernel_sys.cpp)
		if ( uint32_t( c.args[ 2 ] ) != 0 )
			return stopped( "MemoryInformationClass " + hex( uint32_t( c.args[ 2 ] ) ) + ": only MemoryBasicInformation (0) is modelled in milestone M5" );
		const uint64_t addr = c.args[ 1 ], out = c.args[ 3 ], len = c.args[ 4 ], pret = c.args[ 5 ];
		if ( len < mbi_size ) return stopped( "MemoryInformationLength " + hex( len ) + " below sizeof MEMORY_BASIC_INFORMATION (30H): Windows' status is not documented - not modelled" );
		if ( !user_buffer( out, mbi_size, UC_PROT_WRITE ) || ( pret && !user_buffer( pret, 8, UC_PROT_WRITE ) ) )
			return answered( status::access_violation, "MemoryInformation " + hex( out ) + " / ReturnLength " + hex( pret ) + ": not writable user memory (probe)" );
		if ( addr > f_.max_app_address )
			return stopped( "BaseAddress " + hex( addr ) + " above lpMaximumApplicationAddress: Windows' status is not documented for ZwQueryVirtualMemory - not modelled" );
		const uint64_t ps = f_.page_size, pg = addr & ~( ps - 1 );
		struct mbi_t { uint64_t base, alloc_base; uint32_t alloc_protect; uint16_t partition, pad; uint64_t region; uint32_t state, protect, type, pad2; } m{};
		static_assert( sizeof( mbi_t ) == mbi_size, "MEMORY_BASIC_INFORMATION (x64, um\\winnt.h)" );
		std::string what;
		if ( const allocation* a = owner( pg, ps ) )
		{
			const size_t i0 = size_t( ( pg - a->base ) / ps );
			size_t i = i0;
			while ( i < a->pages.size() && a->pages[ i ] == a->pages[ i0 ] ) i++;
			m.base = pg;
			m.alloc_base = a->base;
			m.alloc_protect = a->allocation_protect;
			m.region = uint64_t( i - i0 ) * ps;
			m.state = a->pages[ i0 ] ? mem::commit : mem::reserve;
			m.protect = a->pages[ i0 ];   // reserved pages: 0 (the documentation leaves Protect undefined for MEM_RESERVE)
			m.type = mem::private_;
			what = std::string( m.state == mem::commit ? "MEM_COMMIT " : "MEM_RESERVE " ) + page_text( m.protect ) + " MEM_PRIVATE";
		}
		else if ( const view* v = view_of( pg ) )   // U2031: a section view (image / mapped file)
		{
			const size_t i0 = size_t( ( pg - v->base ) / ps );
			size_t i = i0;
			while ( i < v->pages.size() && v->pages[ i ] == v->pages[ i0 ] ) i++;
			m.base = pg;
			m.alloc_base = v->base;
			m.alloc_protect = v->allocation_protect;
			m.region = uint64_t( i - i0 ) * ps;
			m.state = v->pages[ i0 ] ? mem::commit : mem::reserve;
			m.protect = v->pages[ i0 ];
			m.type = v->type;
			what = std::string( m.state == mem::commit ? "MEM_COMMIT " : "MEM_RESERVE " ) + page_text( m.protect ) + ( v->type == mem2::image ? " MEM_IMAGE " : " MEM_MAPPED " ) + v->name;
		}
		else
		{
			uint64_t next = f_.max_app_address + 1;
			for ( const auto& r : e_.regions() )
			{
				if ( pg >= r.base && pg - r.base < r.size )
					return stopped( "NtQueryVirtualMemory of " + hex( addr ) + " in the environment region \"" + r.name + "\": Windows' MEMORY_BASIC_INFORMATION for it (image / "
									"stack / PEB / TEB / shared pages) has no cited source yet - not modelled in milestone M5" );
				if ( r.base > pg ) next = std::min( next, r.base );
			}
			for ( const auto& [ ab, a ] : allocs_ )   // U2035: the (unmapped) reservations end a free run too
				if ( a.base > pg ) next = std::min( next, a.base );
			for ( const auto& [ vb, v ] : views_ )    // P40 (U2065): so do views with reserved (unmapped) pages
				if ( v.base > pg ) next = std::min( next, v.base );
			m.base = pg;
			m.region = next - pg;
			m.state = mem::free_;
			m.protect = page::noaccess;   // MEM_FREE: the other members are undefined in the documentation; PAGE_NOACCESS / 0 chosen
			what = "MEM_FREE";
		}
		reply r = answered( status::success, "MemoryBasicInformation of " + hex( addr ) + ": " + what + ", RegionSize " + hex( m.region ) );
		const uint64_t rl = mbi_size;
		if ( !put( out, &m, mbi_size, r ) || ( pret && !put( pret, &rl, 8, r ) ) ) return stopped( "writing the result failed after the probe" );
		return r;
	}

	reply kernel::svc_query_information_process( const call& c )
	{
		if ( c.args[ 0 ] != current_process )
			return stopped( "NtQueryInformationProcess for ProcessHandle " + hex( c.args[ 0 ] ) + ": only NtCurrentProcess() is modelled" );
		const uint32_t cls = uint32_t( c.args[ 1 ] ), len = uint32_t( c.args[ 3 ] );
		const uint64_t out = c.args[ 2 ];
		const uint64_t pret = c.stack_args ? c.args[ 4 ] : 0;
		if ( !c.stack_args ) return stopped( "NtQueryInformationProcess: the stack argument ReturnLength is not readable" );
		std::vector<uint8_t> data;
		std::string what;
		switch ( cls )
		{
			case 0:   // ProcessBasicInformation: winternl.h PROCESS_BASIC_INFORMATION {Reserved1, PebBaseAddress, Reserved2[2], UniqueProcessId, Reserved3}
			{
				data.assign( size_t( pbi_size ), 0 );
				std::memcpy( data.data() + 8, &f_.peb, 8 );
				std::memcpy( data.data() + 32, &f_.process_id, 8 );
				what = "ProcessBasicInformation: PebBaseAddress " + hex( f_.peb ) + ", UniqueProcessId " + hex( f_.process_id ) +
					   " (the Reserved members 0: winternl.h documents them as reserved - not modelled)";
				break;
			}
			case 7:   // ProcessDebugPort: "A nonzero value indicates that the process is being run under the control of a ring 3 debugger"
				data.assign( 8, 0 );
				what = "ProcessDebugPort 0 (no debugger: the emulated process has none)";
				break;
			case 26:   // ProcessWow64Information: nonzero = a WOW64 process; this one is a native 64-bit process
				data.assign( 8, 0 );
				what = "ProcessWow64Information 0 (native 64-bit process)";
				break;
			case 29:   // ProcessBreakOnTermination: ULONG, nonzero = critical process
				data.assign( 4, 0 );
				what = "ProcessBreakOnTermination 0";
				break;
			default:
				if ( reply more; query_process_more( c, more ) ) return more;   // U2033: the classes of the --Sys process start (nt_kernel_sys.cpp)
				return stopped( "ProcessInformationClass " + hex( cls ) + ": not one of the classes winternl.h documents (0, 7, 1AH, 1DH); 1BH ProcessImageFileName "
								"needs an image path identity - not modelled in milestone M5" );
		}
		if ( len != data.size() )
			return stopped( "ProcessInformationLength " + hex( len ) + " for class " + hex( cls ) + " (needs " + hex( data.size() ) +
							"): Windows' status for a wrong length is not documented in winternl.h - not modelled" );
		if ( !user_buffer( out, data.size(), UC_PROT_WRITE ) || ( pret && !user_buffer( pret, 4, UC_PROT_WRITE ) ) )
			return answered( status::access_violation, "ProcessInformation " + hex( out ) + " / ReturnLength " + hex( pret ) + ": not writable user memory (probe)" );
		reply r = answered( status::success, what );
		const uint32_t rl = uint32_t( data.size() );
		if ( !put( out, data.data(), data.size(), r ) || ( pret && !put( pret, &rl, 4, r ) ) ) return stopped( "writing the result failed after the probe" );
		return r;
	}
}
