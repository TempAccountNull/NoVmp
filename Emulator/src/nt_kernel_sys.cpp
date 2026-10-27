// emu::nt (plan 3.M5 part 2, ledgers U2032-U2039): the system services the environment's real ntdll / kernelbase / kernel32 use while
// a process starts under --Sys (LdrInitializeThunk -> the loader -> kernel32 / kernelbase initialisation -> the image's entry), each
// answered with the semantics of its source - the Microsoft documentation / Windows SDK / WDK prototype where one exists, else the
// licensed OSS reference (Wine 11.0, ReactOS 0.4.16; reference only, our own implementation, file:line cited per service) - or
// STOPPED loudly when no source states what Windows answers. Same rules as nt_kernel.cpp: guest pointers probed, OUT parameters
// written only on success, nothing forwarded to the host.
#include <emu/nt_kernel.hpp>
#include <emu/ctr_drbg.hpp>   // U2093
#include <emu/vclock.hpp>     // U2094

#include <algorithm>
#include <cstdint>
#include <cctype>
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

		reply stop_( const std::string& why )
		{
			reply r;
			r.kind = reply_kind::stop;
			r.text = why;
			return r;
		}

		reply done_( uint32_t st, const std::string& text )
		{
			reply r;
			r.kind = reply_kind::done;
			r.status = st;
			r.text = text;
			return r;
		}

		// U2094 (plan 3.M6): the counter comes from the one virtual time base (emu/vclock.hpp); M5 part 2's stand-in (one 100-ns tick
		// per instruction) is gone. QPC frequency 10 MHz: MEASURED (tm_nt time.qpf / time.nqpc: QueryPerformanceFrequency = KUSER
		// QpcFrequency = the frequency NtQueryPerformanceCounter writes = 989680H)
		constexpr uint64_t qpc_frequency = vclock::qpc_frequency;
	}

	// ------------------------------------------------------------------------------------------------ time (U2094, plan 3.M6)
	bool kernel::time_now( uint64_t& qpc, uint64_t& system_time, std::string& why )
	{
		vclock::reading rd;
		if ( !vclock::read( e_.handle(), f_.tsc_hz, rd, why ) ) return false;
		qpc = rd.qpc;
		system_time = rd.system_time;
		return true;
	}

	bool kernel::time_advance( uint64_t hns, std::string& why ) { return !hns || vclock::advance_hns( e_.handle(), f_.tsc_hz, hns, why ); }

	// a LARGE_INTEGER time-out / interval (phnt ntexapi.h:26-47 NtDelayExecution: "A negative value specifies an interval relative to
	// the current time. A positive value specifies an absolute time ... since January 1, 1601 (UTC). A value of zero causes the thread
	// to relinquish the remainder of its time slice"; the same encoding for NtWaitForSingleObject's Timeout, km\wdm.h KeWaitForSingleObject)
	bool kernel::wait_interval( uint64_t p, const std::string& what, uint64_t& hns, reply& r )
	{
		uint64_t t = 0;
		if ( !user_buffer( p, 8, UC_PROT_READ ) || !get64( p, t ) )
		{
			r = done_( status::access_violation, what + " " + hex( p ) + " not readable (probe)" );
			return false;
		}
		const int64_t v = int64_t( t );
		std::string why;
		uint64_t qpc = 0, now = 0;
		if ( !time_now( qpc, now, why ) )
		{
			r = stop_( what + ": " + why );
			return false;
		}
		if ( v == INT64_MIN )
		{
			// 8000000000000000H = the INFINITE interval kernelbase passes for Sleep( INFINITE ): one thread - nothing ends the wait
			r = stop_( what + " " + hex( t ) + " (an infinite wait): the process has one thread - nothing can end it" );
			return false;
		}
		hns = v < 0 ? uint64_t( -v ) : ( v > 0 && uint64_t( v ) > now ? uint64_t( v ) - now : 0 );
		return true;
	}

	reply kernel::svc_time( const call& c )
	{
		const std::string& n = c.name;
		std::string why;
		uint64_t qpc = 0, st = 0;
		if ( n == "NtQueryPerformanceCounter" )
		{
			// phnt ntexapi.h:2267-2284 (PerformanceCounter required, PerformanceFrequency optional); MEASURED tm_nt time.nqpc*: (&c, &f) and
			// (&c, NULL) = STATUS_SUCCESS, (NULL, &f) = STATUS_ACCESS_VIOLATION; the counter = the virtual time base's QPC
			const uint64_t pc = c.args[ 0 ], pf = c.args[ 1 ];
			if ( !user_buffer( pc, 8, UC_PROT_WRITE ) || ( pf && !user_buffer( pf, 8, UC_PROT_WRITE ) ) )
				return done_( status::access_violation, "PerformanceCounter " + hex( pc ) + " / PerformanceFrequency " + hex( pf ) + ": not writable user memory (probe)" );
			if ( !time_now( qpc, st, why ) ) return stop_( "NtQueryPerformanceCounter: " + why );
			reply r = done_( status::success, "the virtual time base: counter " + hex( qpc ) + ", frequency 10 MHz (emu/vclock.hpp)" );
			if ( !put( pc, &qpc, 8, r ) || ( pf && !put( pf, &qpc_frequency, 8, r ) ) ) return stop_( "writing the result failed after the probe" );
			return r;
		}
		if ( n == "NtQuerySystemTime" )
		{
			// um\winternl.h NtQuerySystemTime / phnt ntexapi.h:2194-2206; MEASURED tm_nt time.st*: STATUS_SUCCESS with the KUSER SystemTime
			// of that moment; NULL = STATUS_ACCESS_VIOLATION
			if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, "SystemTime " + hex( c.args[ 0 ] ) + " not writable (probe)" );
			if ( !time_now( qpc, st, why ) ) return stop_( "NtQuerySystemTime: " + why );
			reply r = done_( status::success, "the virtual time base: SystemTime " + hex( st ) + " (FILETIME, UTC)" );
			if ( !put( c.args[ 0 ], &st, 8, r ) ) return stop_( "writing the result failed after the probe" );
			return r;
		}
		// NtDelayExecution( Alertable, DelayInterval ): phnt ntexapi.h:26-47; MEASURED tm_nt time.delay*: STATUS_SUCCESS for a relative,
		// absolute (future or past) and zero interval and for Alertable TRUE with no APC queued (none can be: one thread, no APC
		// service); NULL = STATUS_ACCESS_VIOLATION; the time passes - the virtual time base advances by the interval
		uint64_t hns = 0, raw = 1;
		reply r;
		if ( !wait_interval( c.args[ 1 ], "DelayInterval", hns, r ) ) return r;
		if ( get64( c.args[ 1 ], raw ) && raw == 0 )
			// a zero interval is a yield (phnt: "relinquish the remainder of its time slice"); MEASURED tm_nt time.delay.zero: SUCCESS or
			// 40000024H STATUS_NO_YIELD_PERFORMED (shared\ntstatus.h) by whether another thread was ready - one thread here: none was
			return done_( 0x40000024, "NtDelayExecution( 0 ): a yield with no other thread ready - STATUS_NO_YIELD_PERFORMED, no time passes" );
		if ( !time_advance( hns, why ) ) return stop_( "NtDelayExecution: " + why );
		return done_( status::success, "NtDelayExecution( Alertable " + std::to_string( c.args[ 0 ] & 0xFF ) + " ): " + std::to_string( hns ) +
										   " x 100 ns passed on the virtual time base" );
	}

	uint64_t kernel::open_handle( const object& o )
	{
		const uint64_t h = next_handle_;
		next_handle_ += 4;
		handles_[ h ] = o;
		return h;
	}

	bool kernel::dispatch_more( const call& c, reply& r )
	{
		const std::string& n = c.name;
		if ( n == "NtQueryPerformanceCounter" || n == "NtQuerySystemTime" || n == "NtDelayExecution" )
		{
			r = svc_time( c );   // U2094 (plan 3.M6)
			return true;
		}
		if ( n == "NtProtectVirtualMemory" )
		{
			r = svc_protect_virtual_memory( c );
			return true;
		}
		if ( n == "NtCreateEvent" || n == "NtSetEvent" || n == "NtResetEvent" || n == "NtClearEvent" || n == "NtWaitForSingleObject" )
		{
			r = svc_event( c );
			return true;
		}
		if ( n == "NtQuerySystemInformation" )
		{
			r = svc_query_system_information( c );
			return true;
		}
		if ( n == "NtOpenKey" || n == "NtOpenKeyEx" )
		{
			r = svc_open_key( c );
			return true;
		}
		if ( n == "NtQueryValueKey" )
		{
			r = svc_query_value_key( c );
			return true;
		}
		if ( n == "NtAllocateVirtualMemoryEx" )
		{
			r = svc_allocate_virtual_memory_ex( c );
			return true;
		}
		return dispatch_measured( c, r );   // P39: the undocumented services (phnt prototypes, measured behaviour; nt_kernel_measured.cpp)
	}

	// ZwAllocateVirtualMemoryEx (WDK km\ntifs.h:31416: ProcessHandle, BaseAddress, RegionSize, AllocationType, PageProtection,
	// ExtendedParameters, ExtendedParameterCount; SDK um\winnt.h:15032-15067 MEM_EXTENDED_PARAMETER {Type : 8 bits, value} 10H each,
	// MEM_ADDRESS_REQUIREMENTS {LowestStartingAddress, HighestEndingAddress, Alignment}); the parameter rules per Wine 11.0
	// dlls/ntdll/unix/virtual.c:5232-5313 get_extended_params (count without a pointer, a type >= 32 or twice, an alignment that is
	// not a power of two of at least the granularity, a lowest address not granularity-aligned / beyond the user range, a highest
	// address not ending a page / not above the lowest = STATUS_INVALID_PARAMETER; NUMA node / partition / user physical handle:
	// ignored, as Wine ignores them) and :5315-5355 (a base address with limits = STATUS_INVALID_PARAMETER; size 0 =
	// STATUS_INVALID_PARAMETER); then the NtAllocateVirtualMemory core with the limits. Loud stops: attribute flags / image machine
	// other than 0, another process.
	reply kernel::svc_allocate_virtual_memory_ex( const call& c )
	{
		if ( c.args[ 0 ] != current_process ) return stop_( "NtAllocateVirtualMemoryEx for ProcessHandle " + hex( c.args[ 0 ] ) + ": only NtCurrentProcess() is modelled" );
		if ( !c.stack_args ) return stop_( "NtAllocateVirtualMemoryEx: the stack arguments are not readable" );
		const uint64_t pbase = c.args[ 1 ], psize = c.args[ 2 ], params = c.args[ 5 ];
		const uint32_t type = uint32_t( c.args[ 3 ] ), prot = uint32_t( c.args[ 4 ] ), count = uint32_t( c.args[ 6 ] );
		if ( count && !params ) return done_( status::invalid_parameter, "ExtendedParameterCount " + std::to_string( count ) + " without ExtendedParameters" );
		if ( count > 16 ) return stop_( "ExtendedParameterCount " + std::to_string( count ) + ": more than 16 not modelled" );
		if ( count && !user_buffer( params, uint64_t( count ) * 16, UC_PROT_READ ) ) return done_( status::access_violation, "ExtendedParameters " + hex( params ) + " not readable" );
		uint64_t lo = 0, hi = 0, align = 0;
		uint32_t present = 0;
		std::string what;
		for ( uint32_t i = 0; i < count; i++ )
		{
			uint64_t t = 0, v = 0;
			get64( params + 16 * i, t );
			get64( params + 16 * i + 8, v );
			const uint32_t ty = uint32_t( t & 0xFF );
			if ( ty >= 32 || ( present & ( 1u << ty ) ) ) return done_( status::invalid_parameter, "extended parameter " + std::to_string( i ) + ": type " + std::to_string( ty ) + " invalid / repeated" );
			present |= 1u << ty;
			if ( ty == 1 )   // MemExtendedParameterAddressRequirements
			{
				if ( !user_buffer( v, 0x18, UC_PROT_READ ) ) return done_( status::access_violation, "MEM_ADDRESS_REQUIREMENTS " + hex( v ) + " not readable" );
				uint64_t rl = 0, rh = 0, ra = 0;
				get64( v, rl );
				get64( v + 8, rh );
				get64( v + 16, ra );
				const uint64_t gm = f_.allocation_granularity - 1, limit = f_.max_app_address + 1;
				if ( ra && ( ( ra & ( ra - 1 ) ) || ra - 1 < gm ) ) return done_( status::invalid_parameter, "Alignment " + hex( ra ) + " invalid" );
				if ( rl && ( rl >= limit || ( rl & gm ) ) ) return done_( status::invalid_parameter, "LowestStartingAddress " + hex( rl ) + " invalid" );
				if ( rh && ( rh > limit || rh <= rl || ( ( rh + 1 ) & ( f_.page_size - 1 ) ) ) ) return done_( status::invalid_parameter, "HighestEndingAddress " + hex( rh ) + " invalid" );
				lo = rl;
				hi = rh;
				align = ra;
				what += " address requirements [" + hex( rl ) + ", " + hex( rh ) + "] alignment " + hex( ra ) + ";";
			}
			else if ( ty == 2 || ty == 3 || ty == 4 )
				what += " parameter type " + std::to_string( ty ) + " ignored (Wine);";
			else if ( ( ty == 5 || ty == 6 ) && ( v == 0 || ( ty == 6 && uint16_t( v ) == 0x8664 ) ) )
				what += " parameter type " + std::to_string( ty ) + " value " + hex( v ) + ";";
			else if ( ty == 5 || ty == 6 )
				return stop_( "extended parameter type " + std::to_string( ty ) + " (" + ( ty == 5 ? "attribute flags" : "image machine" ) + ") value " + hex( v ) + ": not modelled" );
			else
				return done_( status::invalid_parameter, "extended parameter type " + std::to_string( ty ) + " invalid (Wine get_extended_params)" );
		}
		if ( !user_buffer( pbase, 8, UC_PROT_READ | UC_PROT_WRITE ) || !user_buffer( psize, 8, UC_PROT_READ | UC_PROT_WRITE ) )
			return done_( status::access_violation, "BaseAddress " + hex( pbase ) + " / RegionSize " + hex( psize ) + ": not writable user memory (probe)" );
		uint64_t base = 0, size = 0;
		get64( pbase, base );
		get64( psize, size );
		if ( base && ( align || lo || hi ) ) return done_( status::invalid_parameter, "a BaseAddress with address requirements (Wine: STATUS_INVALID_PARAMETER)" );
		if ( !size ) return done_( status::invalid_parameter, "*RegionSize 0 (Wine: STATUS_INVALID_PARAMETER)" );
		reply r = allocate( base, size, type, prot, lo, hi, align );
		if ( r.kind == reply_kind::done && r.status == status::success )
		{
			r.text += what.empty() ? std::string() : " -" + what;
			if ( !put( pbase, &base, 8, r ) || !put( psize, &size, 8, r ) ) return stop_( "writing *BaseAddress / *RegionSize failed after the probe" );
		}
		return r;
	}

	// ZwQueryValueKey (WDK km\wdm.h:45753: KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
	// the layouts KEY_VALUE_BASIC_INFORMATION {TitleIndex, Type, NameLength, Name[]} (0), KEY_VALUE_FULL_INFORMATION {TitleIndex,
	// Type, DataOffset, DataLength, NameLength, Name[], data} (1), KEY_VALUE_PARTIAL_INFORMATION {TitleIndex, Type, DataLength, Data[]}
	// (2) (km\wdm.h); the status rules per Wine 11.0 dlls/ntdll/unix/registry.c:490-557 / copy_key_value_info :388-432 (ResultLength =
	// the fixed part + the name (0, 1) + the data (1, 2); Length below the fixed part = STATUS_BUFFER_TOO_SMALL - here only ResultLength
	// is written; below ResultLength = STATUS_BUFFER_OVERFLOW with the fixed part written). The value comes from the captured
	// registry subset: captured absent = STATUS_OBJECT_NAME_NOT_FOUND; not captured = a loud stop naming it.
	reply kernel::svc_query_value_key( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtQueryValueKey: the stack arguments are not readable" );
		const auto it = handles_.find( c.args[ 0 ] );
		if ( it == handles_.end() || it->second.kind != "key" ) return done_( status::invalid_handle, "KeyHandle " + hex( c.args[ 0 ] ) + " is not a key of this process" );
		std::string name, why;
		if ( !guest_ustring( c.args[ 1 ], name, why ) ) return done_( status::access_violation, why );
		const uint32_t cls = uint32_t( c.args[ 2 ] ), len = uint32_t( c.args[ 4 ] );
		const uint64_t out = c.args[ 3 ], pres = c.args[ 5 ];
		std::string key, lname;
		for ( char ch : it->second.name ) key += char( std::tolower( uint8_t( ch ) ) );
		while ( !key.empty() && key.back() == '\\' ) key.pop_back();
		for ( char ch : name ) lname += char( std::tolower( uint8_t( ch ) ) );
		const auto k = f_.registry ? f_.registry->find( key ) : std::map<std::string, registry_key>::const_iterator();
		if ( !f_.registry || k == f_.registry->end() || !k->second.values.count( lname ) )
			return stop_( "NtQueryValueKey \"" + it->second.name + "\" value \"" + name + "\" (class " + std::to_string( cls ) + "): the value is not in the captured registry subset - "
						  "capture it before this run can continue" );
		const registry_value& v = k->second.values.at( lname );
		if ( v.type == ~0u ) return done_( status::object_name_not_found, "value \"" + name + "\" of \"" + it->second.name + "\" is absent in the captured environment (STATUS_OBJECT_NAME_NOT_FOUND)" );
		if ( cls > 2 ) return stop_( "NtQueryValueKey class " + std::to_string( cls ) + ": not modelled (0 / 1 / 2 are)" );
		std::vector<uint8_t> nm;   // the name as UTF-16 (ASCII here)
		for ( char ch : name ) { nm.push_back( uint8_t( ch ) ); nm.push_back( 0 ); }
		const uint32_t nlen = uint32_t( nm.size() ), dlen = uint32_t( v.data.size() );
		std::vector<uint8_t> b;
		auto p32 = [ & ]( uint32_t x ) { for ( int i = 0; i < 4; i++ ) b.push_back( uint8_t( x >> ( 8 * i ) ) ); };
		uint32_t min_size = 0;
		if ( cls == 0 ) { p32( 0 ); p32( v.type ); p32( nlen ); min_size = 12; b.insert( b.end(), nm.begin(), nm.end() ); }
		else if ( cls == 1 ) { p32( 0 ); p32( v.type ); p32( 20 + nlen ); p32( dlen ); p32( nlen ); min_size = 20; b.insert( b.end(), nm.begin(), nm.end() ); b.insert( b.end(), v.data.begin(), v.data.end() ); }
		else { p32( 0 ); p32( v.type ); p32( dlen ); min_size = 12; b.insert( b.end(), v.data.begin(), v.data.end() ); }
		const uint32_t need = uint32_t( b.size() );
		if ( !user_buffer( pres, 4, UC_PROT_WRITE ) ) return done_( status::access_violation, "ResultLength " + hex( pres ) + " not writable" );
		if ( len < min_size )
		{
			reply rr = done_( status::buffer_too_small, "value \"" + name + "\": Length " + hex( len ) + " below the fixed part " + hex( min_size ) + " (STATUS_BUFFER_TOO_SMALL), ResultLength " + hex( need ) );
			if ( !put( pres, &need, 4, rr ) ) return stop_( "writing ResultLength failed after the probe" );
			return rr;
		}
		if ( !user_buffer( out, std::min( len, need ), UC_PROT_WRITE ) ) return done_( status::access_violation, "KeyValueInformation " + hex( out ) + " not writable" );
		const bool overflow = len < need;
		reply rr = done_( overflow ? status::buffer_overflow : status::success, "value \"" + name + "\" of \"" + it->second.name + "\": type " + std::to_string( v.type ) + ", " +
																					 std::to_string( dlen ) + " data bytes (captured)" + ( overflow ? ", buffer too short: STATUS_BUFFER_OVERFLOW" : "" ) );
		if ( !put( out, b.data(), overflow ? min_size : need, rr ) || !put( pres, &need, 4, rr ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// ------------------------------------------------------------------------------------------------ the registry
	// ZwOpenKey (WDK km\wdm.h:45604: KeyHandle, DesiredAccess, ObjectAttributes), ZwOpenKeyEx (:45616: + OpenOptions). The emulated
	// registry is the environment's CAPTURED subset (kernel_facts::registry, from the capture's registry facts): a key in it opens;
	// a key the capture recorded as ABSENT = STATUS_OBJECT_NAME_NOT_FOUND ("ZwOpenKey ... STATUS_OBJECT_NAME_NOT_FOUND" for a
	// missing key); any other key = a loud stop naming it (the capture list grows on demand, never a guess)
	reply kernel::svc_open_key( const call& c )
	{
		object_attributes oa;
		std::string why;
		if ( !guest_object_attributes( c.args[ 2 ], oa, why ) ) return done_( status::access_violation, why );
		std::string full = oa.name;
		if ( oa.root )
		{
			const auto it = handles_.find( oa.root );
			if ( it == handles_.end() || it->second.kind != "key" ) return done_( status::invalid_handle, "RootDirectory " + hex( oa.root ) + " is not a key handle of this process" );
			full = it->second.name + "\\" + oa.name;
		}
		std::string key;
		for ( char ch : full ) key += char( std::tolower( uint8_t( ch ) ) );
		while ( !key.empty() && key.back() == '\\' ) key.pop_back();
		const auto none = [ & ]() {
			return stop_( std::string( c.name ) + " \"" + full + "\" (access " + hex( c.args[ 1 ] ) + "): the key is not in the environment's captured registry subset - capture it "
						  "(present / absent and its values) before this run can continue" );
		};
		// P41 (U2073): an absent key - STATUS_OBJECT_NAME_NOT_FOUND with the handle written NULL (MEASURED: tm_nt hku.safer; before P41 the
		// handle was left unwritten)
		const auto absent = [ & ]( const std::string& why ) {
			reply ra = done_( status::object_name_not_found, "key \"" + full + "\" is absent " + why + " (STATUS_OBJECT_NAME_NOT_FOUND, handle NULL)" );
			const uint64_t zero = 0;
			if ( user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) && !put( c.args[ 0 ], &zero, 8, ra ) ) return stop_( "writing the handle failed after the probe" );
			return ra;
		};
		// P41 (U2073): the USER's keys \REGISTRY\USER\<the user SID>\... - the environment's user SID is the PLACEHOLDER of TokenUser
		// (S-1-5-21-1000001-1000002-1000003-1001, nt_kernel_measured.cpp); the user hive is never captured (real identifiers): each
		// key is MEASURED from inside our own test PE with its own SID (tm_nt hku.*) and listed here. Other user keys: stop.
		{
			const std::string up = "\\registry\\user\\s-1-5-21-1000001-1000002-1000003-1001";
			static const char* const measured_absent[] = { "\\software\\policies\\microsoft\\windows\\safer\\codeidentifiers" };   // tm_nt hku.safer
			if ( key.rfind( up, 0 ) == 0 )
			{
				const std::string rest = key.substr( up.size() );
				for ( auto m : measured_absent )
					if ( rest == m ) return absent( "for the user (measured from inside: tm_nt hku.*)" );
				return stop_( std::string( c.name ) + " \"" + full + "\": a key of the user's hive that was not measured (tm_nt hku.*: measured from inside with the process' own SID)" );
			}
		}
		// P44 (U2121): the hive root \REGISTRY\MACHINE (the predefined HKEY_LOCAL_MACHINE - WDK "Registry Key Object Routines": user-mode
		// HKEY_LOCAL_MACHINE = \Registry\Machine) exists on every system - kernelbase opens it (NtOpenKeyEx, MAXIMUM_ALLOWED) before a
		// relative open of HKLM\... (user32's LoadAppInitDlls, P44 x12); the capture tool records keys BELOW it only
		if ( key != "\\registry\\machine" )
		{
			if ( !f_.registry ) return none();
			const auto reg = f_.registry->find( key );
			if ( reg == f_.registry->end() ) return none();
			if ( !reg->second.present ) return absent( "in the captured environment" );
		}
		if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, "KeyHandle " + hex( c.args[ 0 ] ) + " not writable" );
		object o;
		o.kind = "key";
		o.name = full;
		const uint64_t h = open_handle( o );
		reply rr = done_( status::success, "key handle " + hex( h ) + ": \"" + full + "\" (captured)" );
		if ( !put( c.args[ 0 ], &h, 8, rr ) ) return stop_( "writing the handle failed after the probe" );
		return rr;
	}

	// ------------------------------------------------------------------------------------------------ NtQueryInformationProcess, more classes
	// The class numbers of Wine 11.0 include/winternl.h:1895 PROCESSINFOCLASS (winternl.h names a few); semantics per Wine
	// dlls/ntdll/unix/process.c NtQueryInformationProcess (current process only; a length other than the class size =
	// STATUS_INFO_LENGTH_MISMATCH with ReturnLength written):
	//   22H ProcessExecuteFlags (ULONG) :1471-1480 - a native 64-bit process: MEM_EXECUTE_OPTION_DISABLE | DISABLE_THUNK_EMULATION |
	//       PERMANENT (1 | 4 | 8, winternl.h:2021-2024): DEP always on
	//   24H ProcessCookie (ULONG) :1508-1516 - Wine answers 0 (a stub); Windows' value is a per-process secret, no source for it
	bool kernel::query_process_more( const call& c, reply& r )
	{
		const uint32_t cls = uint32_t( c.args[ 1 ] ), len = uint32_t( c.args[ 3 ] );
		const uint64_t out = c.args[ 2 ], pret = c.stack_args ? c.args[ 4 ] : 0;
		std::vector<uint8_t> data;
		std::string what;
		switch ( cls )
		{
			case 0x34:
			{
				// P43 (U2109): ProcessMitigationPolicy (phnt ntpsapi.h PROCESSINFOCLASS 52; PROCESS_MITIGATION_POLICY_INFORMATION {Policy,
				// flags} - um\winnt.h PROCESS_MITIGATION_POLICY, GetProcessMitigationPolicy). user32's start asks policy 0BH
				// ProcessSystemCallFilterPolicy (sample run 8, exploration x6 system call 487). MEASURED (tm_nt mit.*, an ordinary process): Length
				// 8 = STATUS_SUCCESS, the flags (bytes 4..7) 0, the policy field unchanged, ReturnLength NOT written (also when given); Length 4 =
				// STATUS_INFO_LENGTH_MISMATCH, nothing written. Other policies / lengths: stop.
				uint32_t pol = 0xFFFFFFFF;
				std::string err;
				if ( len >= 4 && user_buffer( out, 4, UC_PROT_READ ) ) e_.read( out, &pol, 4, err );
				const std::string w = "ProcessMitigationPolicy policy " + hex( pol ) + ", Length " + hex( len );
				// P44 (U2116): policy 6 ProcessExtensionPointDisablePolicy (kernel32 BasepAreExtensionPointsBlocked, user32's LoadAppInitDlls in
				// its ClientThreadSetup callback; P44 x11) - MEASURED (tm_nt mit.6): Length 8, ReturnLength NULL = STATUS_SUCCESS, flags 0
				if ( pol == 6 && len == 8 && !pret )
				{
					if ( !user_buffer( out, 8, UC_PROT_WRITE ) ) { r = done_( status::access_violation, w + ": ProcessInformation not writable" ); return true; }
					r = done_( status::success, w + ": ProcessExtensionPointDisablePolicy flags 0 (measured: tm_nt mit.6)" );
					const uint32_t zero6 = 0;
					if ( !put( out + 4, &zero6, 4, r ) ) r = stop_( "writing the result failed after the probe" );
					return true;
				}
				if ( pol != 0xB || ( len != 8 && len != 4 ) ) { r = stop_( w + ": not measured (tm_nt mit: policy 0BH, Length 8 / 4; 6 with Length 8)" ); return true; }
				if ( len == 4 ) { r = done_( status::info_length_mismatch, w + ": STATUS_INFO_LENGTH_MISMATCH (measured: tm_nt mit.b.4)" ); return true; }
				if ( !user_buffer( out, 8, UC_PROT_WRITE ) ) { r = done_( status::access_violation, w + ": ProcessInformation not writable" ); return true; }
				r = done_( status::success, w + ": ProcessSystemCallFilterPolicy flags 0, ReturnLength not written (measured: tm_nt mit.b / mit.b.rl)" );
				const uint32_t zero = 0;
				if ( !put( out + 4, &zero, 4, r ) ) r = stop_( "writing the result failed after the probe" );
				return true;
			}
			case 0x22:
			{
				data.assign( 4, 0 );
				const uint32_t v = 0x1 | 0x4 | 0x8;
				std::memcpy( data.data(), &v, 4 );
				what = "ProcessExecuteFlags 0xD (MEM_EXECUTE_OPTION_DISABLE | DISABLE_THUNK_EMULATION | PERMANENT: a native 64-bit process)";
				break;
			}
			case 0x24:
			{
				// P39 (U2052): MEASURED natively (tm_nt qip.24*): a NON-ZERO 4-byte value, equal on every query in one process (a
				// per-process secret: Windows picks it at random - the value itself is never recorded). Replaces Wine's stub 0 (P36). The
				// emulator's value is deterministic: U2093 (plan 3.M6, "any emulated random source" of --seeded = CTR_DRBG) - the first
				// 64-bit value of the environment's CTR_DRBG (emu/ctr_drbg.hpp: NIST SP 800-90A Rev. 1 10.2.1, AES-128, the seed of decision
				// D8, personalization "NoVmp environment"), low 32 bits, never 0 - a model choice with the measured shape, not a Windows
				// value (P39 used SplitMix64)
				ctr_drbg env_rng( f_.rng_seed, environment_personalization );
				uint32_t v = uint32_t( env_rng.next() );
				if ( !v ) v = 1;
				data.assign( 4, 0 );
				std::memcpy( data.data(), &v, 4 );
				what = "ProcessCookie " + hex( v ) + " (non-zero, per process - measured shape tm_nt qip.24; the value: the environment's CTR_DRBG of the seed, a model choice)";
				break;
			}
			case 0x31:
			{
				// P41 (U2070): ProcessConsoleHostProcess (phnt ntpsapi.h:222 / :809-821, a ULONG_PTR). MEASURED (tm_nt con.chp.*, a process
				// started detached): Length 8 = STATUS_SUCCESS, ReturnLength 8, the value its kernelbase set (1); Length 4 / 10H =
				// STATUS_INFO_LENGTH_MISMATCH, ReturnLength not written (the generic rule below). Before any set: not measured - stop.
				if ( !console_host_set_ )
				{
					r = stop_( "NtQueryInformationProcess ProcessConsoleHostProcess (31H) before the process set it: not measured" );
					return true;
				}
				data.assign( 8, 0 );
				std::memcpy( data.data(), &console_host_, 8 );
				what = "ProcessConsoleHostProcess " + hex( console_host_ ) + " (set by the process; measured: tm_nt con.chp.8)";
				break;
			}
			default:
				return false;
		}
		if ( pret && !user_buffer( pret, 4, UC_PROT_WRITE ) )
		{
			r = done_( status::access_violation, "ReturnLength " + hex( pret ) + " not writable" );
			return true;
		}
		const uint32_t rl = uint32_t( data.size() );
		if ( len != data.size() )
		{
			// P39 (U2052): MEASURED (tm_nt qip.22.8 / qip.24.8): STATUS_INFO_LENGTH_MISMATCH and ReturnLength NOT written (Wine writes it)
			r = done_( status::info_length_mismatch, "class " + hex( cls ) + ": length " + hex( len ) + ", needs " + hex( data.size() ) + " (STATUS_INFO_LENGTH_MISMATCH, ReturnLength not written - measured)" );
			(void)rl;
			return true;
		}
		if ( !user_buffer( out, data.size(), UC_PROT_WRITE ) )
		{
			r = done_( status::access_violation, "ProcessInformation " + hex( out ) + " not writable" );
			return true;
		}
		r = done_( status::success, what );
		if ( !put( out, data.data(), data.size(), r ) || ( pret && !put( pret, &rl, 4, r ) ) ) r = stop_( "writing the result failed after the probe" );
		return true;
	}

	// ------------------------------------------------------------------------------------------------ NtQueryVirtualMemory class 6
	// MemoryImageInformation (Wine 11.0 include/winternl.h:2491-2506 MEMORY_IMAGE_INFORMATION {ImageBase, SizeOfImage, ImageFlags:
	// ImagePartialMap 1, ImageNotExecutable 1, ImageSigningLevel 4 bits}, 18H bytes; dlls/ntdll/unix/virtual.c:6040-6073
	// get_memory_image_info: a length below 18H = STATUS_INFO_LENGTH_MISMATCH; an image view: base, size, signing level 12; memory
	// that is not an image but allocated: STATUS_SUCCESS with the structure zeroed; free memory: STATUS_INVALID_ADDRESS)
	reply kernel::query_memory_image( const call& c )
	{
		if ( c.args[ 0 ] != current_process ) return stop_( "NtQueryVirtualMemory for ProcessHandle " + hex( c.args[ 0 ] ) + ": only NtCurrentProcess() is modelled" );
		if ( !c.stack_args ) return stop_( "NtQueryVirtualMemory: the stack arguments (Length, ReturnLength) are not readable" );
		const uint64_t addr = c.args[ 1 ], out = c.args[ 3 ], len = c.args[ 4 ], pret = c.args[ 5 ];
		if ( len < 0x18 ) return done_( status::info_length_mismatch, "MemoryImageInformation: length " + hex( len ) + " below 18H" );
		if ( !user_buffer( out, 0x18, UC_PROT_WRITE ) || ( pret && !user_buffer( pret, 8, UC_PROT_WRITE ) ) )
			return done_( status::access_violation, "MemoryInformation " + hex( out ) + " / ReturnLength " + hex( pret ) + ": not writable user memory (probe)" );
		uint8_t m[ 0x18 ] = {};
		std::string what;
		const view* v = view_of( addr );
		if ( v && v->type == mem2::image )
		{
			const uint32_t flags = 12u << 2;   // ImageSigningLevel 12 (Wine)
			std::memcpy( m, &v->base, 8 );
			std::memcpy( m + 8, &v->size, 8 );
			std::memcpy( m + 0x10, &flags, 4 );
			what = "MemoryImageInformation of " + hex( addr ) + ": the image " + v->name + " [" + hex( v->base ) + ", +" + hex( v->size ) + "), signing level 12 (Wine)";
		}
		else
		{
			bool used = v != nullptr || owner( addr & ~uint64_t( f_.page_size - 1 ), 1 ) != nullptr;
			for ( const auto& rg : e_.regions() )
				if ( addr >= rg.base && addr - rg.base < rg.size ) used = true;
			if ( !used ) return done_( status::invalid_address, "MemoryImageInformation of " + hex( addr ) + ": free memory (STATUS_INVALID_ADDRESS)" );
			what = "MemoryImageInformation of " + hex( addr ) + ": not an image - the structure zeroed (Wine)";
		}
		reply r = done_( status::success, what );
		const uint64_t rl = 0x18;
		if ( !put( out, m, 0x18, r ) || ( pret && !put( pret, &rl, 8, r ) ) ) return stop_( "writing the result failed after the probe" );
		return r;
	}

	// the protection of the page at ADDRESS: a private allocation's / a view's record, else an environment region's engine protection
	// (the stack, PEB, TEB, KUSER: committed private memory in Windows); 0 = not in use
	uint32_t kernel::page_protect_at( uint64_t a, bool& committed, bool& shared )
	{
		committed = shared = false;
		const uint64_t ps = f_.page_size, pg = a & ~( ps - 1 );
		if ( allocation* al = owner( pg, ps ) )
		{
			const uint32_t p = al->pages[ size_t( ( pg - al->base ) / ps ) ];
			committed = p != 0;
			return p;
		}
		if ( view* v = view_of( pg ) )
		{
			const uint32_t p = v->pages[ size_t( ( pg - v->base ) / ps ) ];
			committed = p != 0;
			shared = true;
			return p;
		}
		for ( const auto& rg : e_.regions() )
			if ( pg >= rg.base && pg - rg.base < rg.size )
			{
				committed = true;
				const bool rd = rg.prot & UC_PROT_READ, wr = rg.prot & UC_PROT_WRITE, ex = rg.prot & UC_PROT_EXEC;
				return ex ? ( wr ? page::execute_readwrite : page::execute_read ) : wr ? page::readwrite : rd ? page::readonly : page::noaccess;
			}
		return 0;
	}

	// ------------------------------------------------------------------------------------------------ NtQueryVirtualMemory class 4
	// MemoryWorkingSetExInformation: an array of {VirtualAddress, VirtualAttributes} (10H each; the caller sets the addresses; the
	// attributes = SDK um\psapi.h:404 PSAPI_WORKING_SET_EX_BLOCK: Valid bit 0, ShareCount bits 1-3, Win32Protection bits 4-14, Shared
	// bit 15); semantics per Wine 11.0 dlls/ntdll/unix/virtual.c:5938-6000 get_working_set_ex / :5888-5920 fill_working_set_info
	// (length below 10H = STATUS_INFO_LENGTH_MISMATCH; every attribute cleared, then for a committed page: Valid when it has access
	// and is resident - every emulated page is resident -, Shared for a section view, ShareCount 1 when shared and valid (Wine
	// "FIXME"), Win32Protection when valid; ReturnLength = the length)
	reply kernel::query_working_set_ex( const call& c )
	{
		if ( c.args[ 0 ] != current_process ) return stop_( "NtQueryVirtualMemory for ProcessHandle " + hex( c.args[ 0 ] ) + ": only NtCurrentProcess() is modelled" );
		if ( !c.stack_args ) return stop_( "NtQueryVirtualMemory: the stack arguments (Length, ReturnLength) are not readable" );
		const uint64_t out = c.args[ 3 ], len = c.args[ 4 ], pret = c.args[ 5 ];
		if ( len < 0x10 ) return done_( status::info_length_mismatch, "MemoryWorkingSetExInformation: length " + hex( len ) + " below 10H" );
		if ( len > 0x10000 ) return stop_( "MemoryWorkingSetExInformation: more than 1000H entries - not modelled" );
		if ( !user_buffer( out, len, UC_PROT_READ | UC_PROT_WRITE ) || ( pret && !user_buffer( pret, 8, UC_PROT_WRITE ) ) )
			return done_( status::access_violation, "MemoryInformation " + hex( out ) + " / ReturnLength " + hex( pret ) + ": not writable user memory (probe)" );
		const uint64_t n = len / 0x10;
		std::vector<uint64_t> buf( size_t( n * 2 ), 0 );
		std::string err;
		e_.read( out, buf.data(), n * 0x10, err );
		std::string what = "MemoryWorkingSetExInformation, " + std::to_string( n ) + " entr" + ( n == 1 ? "y" : "ies" ) + ":";
		for ( uint64_t i = 0; i < n; i++ )
		{
			bool committed = false, shared = false;
			const uint32_t p = page_protect_at( buf[ size_t( 2 * i ) ], committed, shared );
			uint64_t attr = 0;
			const bool valid = committed && p != page::noaccess && !( p & page::guard );
			if ( valid ) attr |= 1 | ( uint64_t( p & 0x7FF ) << 4 );
			if ( valid && shared ) attr |= ( 1ull << 15 ) | ( 1ull << 1 );
			buf[ size_t( 2 * i + 1 ) ] = attr;
			what += " " + hex( buf[ size_t( 2 * i ) ] ) + "=" + hex( attr );
		}
		reply r = done_( status::success, what );
		if ( !put( out, buf.data(), n * 0x10, r ) || ( pret && !put( pret, &len, 8, r ) ) ) return stop_( "writing the result failed after the probe" );
		return r;
	}

	// ------------------------------------------------------------------------------------------------ NtQuerySystemInformation
	// winternl.h NtQuerySystemInformation( SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength ) (SDK
	// um\winternl.h; the classes winternl.h names: SYSTEM_INFORMATION_CLASS). Answered classes and their layouts:
	//   0 SystemBasicInformation (40H bytes): winternl.h documents only NumberOfProcessors (after BYTE Reserved1[24] + PVOID
	//     Reserved2[4] = 38H); the members before it per Wine 11.0 include/winternl.h:2859-2876 (unknown, KeMaximumIncrement,
	//     PageSize, MmNumberOfPhysicalPages, MmLowestPhysicalPage, MmHighestPhysicalPage, AllocationGranularity, LowestUserAddress,
	//     HighestUserAddress, ActiveProcessorsAffinityMask) and their values per dlls/ntdll/unix/virtual.c:3723-3762 (lowest physical
	//     page 1, the number of pages = highest - lowest, KeMaximumIncrement 0 "FIXME" in Wine); a length other than 40H =
	//     STATUS_INFO_LENGTH_MISMATCH (system.c:3167-3179)
	// every other class = a loud stop naming it (demand-driven)
	reply kernel::svc_query_system_information( const call& c )
	{
		const uint32_t cls = uint32_t( c.args[ 0 ] ), len = uint32_t( c.args[ 2 ] );
		const uint64_t out = c.args[ 1 ], pret = c.args[ 3 ];
		std::vector<uint8_t> data;
		std::string what;
		auto p32 = [ & ]( size_t at, uint32_t v ) { std::memcpy( data.data() + at, &v, 4 ); };
		auto p64 = [ & ]( size_t at, uint64_t v ) { std::memcpy( data.data() + at, &v, 8 ); };
		switch ( cls )
		{
			case 0:
			case 0x3E:   // SystemEmulationBasicInformation: Wine system.c:3608-3618 - the same structure (a native 64-bit process: the same values)
			case 0x72:   // SystemNativeBasicInformation: Wine system.c:3164-3166 - on 64-bit the same as SystemBasicInformation
			{
				data.assign( 0x40, 0 );
				const uint64_t highest = std::max<uint64_t>( 1, f_.total_phys / f_.page_size );
				p32( 4, 0 );                       // KeMaximumIncrement (Wine: 0)
				p32( 8, f_.page_size );
				p32( 0xC, uint32_t( highest - 1 ) );
				p32( 0x10, 1 );
				p32( 0x14, uint32_t( highest ) );
				p64( 0x18, f_.allocation_granularity );
				p64( 0x20, f_.min_app_address );
				p64( 0x28, f_.max_app_address );
				p64( 0x30, f_.active_processor_mask );
				data[ 0x38 ] = uint8_t( f_.processors );
				what = std::string( cls == 0 ? "SystemBasicInformation" : cls == 0x3E ? "SystemEmulationBasicInformation" : "SystemNativeBasicInformation" ) + ": page " + hex( f_.page_size ) + ", physical pages 1.." + hex( highest ) + ", granularity " + hex( f_.allocation_granularity ) +
					   ", user range [" + hex( f_.min_app_address ) + ", " + hex( f_.max_app_address ) + "], affinity " + hex( f_.active_processor_mask ) + ", " +
					   std::to_string( f_.processors ) + " processors";
				break;
			}
			case 0xC0:   // SystemFlushInformation (the name: the guest ntdll.pdb enum _SYSTEM_INFORMATION_CLASS)
				// P39 (U2052): MEASURED natively (tm_nt qsi.c0*) - replaces the P36 answer STATUS_INVALID_INFO_CLASS ("NOT PROVIDED": no layout
				// source then); the layout from phnt ntexapi.h:7057 SYSTEM_FLUSH_INFORMATION (nt_kernel_measured.cpp query_system_measured)
				if ( reply m; query_system_measured( c, m ) ) return m;
				return stop_( "SystemInformationClass 0xC0: the measured answer is missing" );
			default:
				if ( reply m; query_system_measured( c, m ) ) return m;   // P39: undocumented classes (phnt layouts, measured; nt_kernel_measured.cpp)
				return stop_( "SystemInformationClass " + hex( cls ) + " (length " + hex( len ) + "): not modelled yet (demand-driven; the class's layout needs a source)" );
		}
		if ( pret && !user_buffer( pret, 4, UC_PROT_WRITE ) ) return done_( status::access_violation, "ReturnLength " + hex( pret ) + " not writable" );
		const uint32_t rl = uint32_t( data.size() );
		if ( len != data.size() )
		{
			reply rr = done_( status::info_length_mismatch, "class " + hex( cls ) + ": length " + hex( len ) + ", needs " + hex( data.size() ) + " (STATUS_INFO_LENGTH_MISMATCH)" );
			if ( pret && !put( pret, &rl, 4, rr ) ) return stop_( "writing ReturnLength failed after the probe" );
			return rr;
		}
		if ( !user_buffer( out, data.size(), UC_PROT_WRITE ) ) return done_( status::access_violation, "SystemInformation " + hex( out ) + " not writable" );
		reply rr = done_( status::success, what );
		if ( !put( out, data.data(), data.size(), rr ) || ( pret && !put( pret, &rl, 4, rr ) ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// ------------------------------------------------------------------------------------------------ helpers
	bool kernel::guest_ustring( uint64_t us, std::string& out, std::string& why )
	{
		// UNICODE_STRING {USHORT Length, USHORT MaximumLength, PWSTR Buffer} (shared\ntdef.h); characters outside 20H..7EH shown as '?'
		out.clear();
		if ( !user_buffer( us, 16, UC_PROT_READ ) )
		{
			why = "UNICODE_STRING " + hex( us ) + " not readable";
			return false;
		}
		uint16_t len = 0;
		uint64_t buf = 0;
		std::string err;
		e_.read( us, &len, 2, err );
		e_.read( us + 8, &buf, 8, err );
		if ( len & 1 || ( len && !user_buffer( buf, len, UC_PROT_READ ) ) )
		{
			why = "UNICODE_STRING " + hex( us ) + " Length " + hex( len ) + " / Buffer " + hex( buf ) + " not readable";
			return false;
		}
		std::vector<uint16_t> w( len / 2 );
		if ( len ) e_.read( buf, w.data(), len, err );
		for ( uint16_t ch : w ) out += ( ch >= 0x20 && ch < 0x7F ) ? char( ch ) : '?';
		return true;
	}

	bool kernel::guest_object_attributes( uint64_t oa, object_attributes& a, std::string& why )
	{
		// OBJECT_ATTRIBUTES {ULONG Length; HANDLE RootDirectory; PUNICODE_STRING ObjectName; ULONG Attributes; PVOID SecurityDescriptor;
		// PVOID SecurityQualityOfService} (shared\ntdef.h InitializeObjectAttributes; x64 size 30H)
		a = object_attributes{};
		if ( !oa ) return true;
		if ( !user_buffer( oa, 0x30, UC_PROT_READ ) )
		{
			why = "ObjectAttributes " + hex( oa ) + " not readable";
			return false;
		}
		uint32_t length = 0;
		uint64_t name = 0;
		std::string err;
		e_.read( oa, &length, 4, err );
		e_.read( oa + 8, &a.root, 8, err );
		e_.read( oa + 0x10, &name, 8, err );
		e_.read( oa + 0x18, &a.attributes, 4, err );
		if ( length != 0x30 )
		{
			why = "ObjectAttributes Length " + hex( length ) + " (not 30H)";
			return false;
		}
		a.present = true;
		return !name || guest_ustring( name, a.name, why );
	}

	// ------------------------------------------------------------------------------------------------ events
	// ZwCreateEvent (WDK km\ntifs.h:31233: EventHandle, DesiredAccess, ObjectAttributes, EventType, InitialState), ZwSetEvent (:31496:
	// EventHandle, PreviousState optional), ZwWaitForSingleObject (:31483: Handle, Alertable, Timeout optional), NtResetEvent /
	// NtClearEvent (no prototype; Wine 11.0 dlls/ntdll/unix/sync.c NtResetEvent / NtClearEvent: the event non-signaled, ResetEvent
	// returns the previous state). EVENT_TYPE NotificationEvent 0 / SynchronizationEvent 1 (shared\ntdef.h). One thread: a wait on a
	// non-signaled object with no timeout would never return - a loud stop (deadlock); a zero timeout = STATUS_TIMEOUT (102H).
	reply kernel::svc_event( const call& c )
	{
		const std::string& n = c.name;
		if ( n == "NtCreateEvent" )
		{
			if ( !c.stack_args ) return stop_( "NtCreateEvent: the stack argument InitialState is not readable" );
			object_attributes oa;
			std::string why;
			if ( !guest_object_attributes( c.args[ 2 ], oa, why ) ) return done_( status::access_violation, why );
			if ( !oa.name.empty() ) return stop_( "NtCreateEvent of the named event \"" + oa.name + "\": named objects (an object directory) are not modelled" );
			const uint32_t type = uint32_t( c.args[ 3 ] );
			if ( type > 1 ) return done_( status::invalid_parameter, "EventType " + hex( type ) + " (NotificationEvent 0 / SynchronizationEvent 1)" );
			if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, "EventHandle " + hex( c.args[ 0 ] ) + " not writable" );
			object o;
			o.kind = type ? "event (synchronization)" : "event (notification)";
			o.value = ( c.args[ 4 ] & 0xFF ) ? 1 : 0;
			const uint64_t h = open_handle( o );
			reply rr = done_( status::success, "event handle " + hex( h ) + ": " + o.kind + ", initial state " + std::to_string( o.value ) );
			if ( !put( c.args[ 0 ], &h, 8, rr ) ) return stop_( "writing the handle failed after the probe" );
			return rr;
		}
		const auto it = handles_.find( c.args[ 0 ] );
		if ( it == handles_.end() || it->second.kind.rfind( "event", 0 ) != 0 )
		{
			if ( n == "NtWaitForSingleObject" && it != handles_.end() && it->second.kind == "mutant" ) return svc_mutant_wait_release( c );   // P41 (U2076)
			if ( n == "NtWaitForSingleObject" && it != handles_.end() )
				return stop_( "NtWaitForSingleObject on the " + it->second.kind + " handle " + hex( c.args[ 0 ] ) + ": waiting on this object type is not modelled" );
			return done_( it == handles_.end() ? status::invalid_handle : status::object_type_mismatch, "handle " + hex( c.args[ 0 ] ) + " is not an event of this process" );
		}
		object& ev = it->second;
		if ( n == "NtWaitForSingleObject" )
		{
			if ( ev.value )
			{
				if ( ev.kind == "event (synchronization)" ) ev.value = 0;   // a synchronization event is reset by a satisfied wait
				return done_( status::success, "event " + hex( c.args[ 0 ] ) + " signaled: the wait is satisfied" );
			}
			if ( !c.args[ 2 ] )
				return stop_( "NtWaitForSingleObject on the non-signaled event " + hex( c.args[ 0 ] ) + " without a timeout: the process has one thread - nothing can signal it" );
			// U2094 (plan 3.M6): the time-out passes on the one virtual time base, then STATUS_TIMEOUT (102H) - MEASURED tm_nt time.wait /
			// time.wait.past (a relative 10 ms and an absolute past time-out on a non-signaled notification event: 102H); a zero time-out = 102H
			uint64_t hns = 0;
			reply wr;
			if ( !wait_interval( c.args[ 2 ], "Timeout", hns, wr ) ) return wr;
			std::string why;
			if ( !time_advance( hns, why ) ) return stop_( "NtWaitForSingleObject: " + why );
			return done_( 0x102, "event " + hex( c.args[ 0 ] ) + " not signaled: the time-out of " + std::to_string( hns ) +
									 " x 100 ns passed on the virtual time base - STATUS_TIMEOUT" );
		}
		const uint64_t pprev = n == "NtClearEvent" ? 0 : c.args[ 1 ];
		if ( pprev && !user_buffer( pprev, 4, UC_PROT_WRITE ) ) return done_( status::access_violation, "PreviousState " + hex( pprev ) + " not writable" );
		const uint64_t prev = ev.value;
		ev.value = n == "NtSetEvent" ? 1 : 0;
		reply rr = done_( status::success, n + ": event " + hex( c.args[ 0 ] ) + " " + std::to_string( prev ) + " -> " + std::to_string( ev.value ) );
		const uint32_t p32 = uint32_t( prev );
		if ( pprev && !put( pprev, &p32, 4, rr ) ) return stop_( "writing PreviousState failed after the probe" );
		return rr;
	}

	// NtProtectVirtualMemory( ProcessHandle, *BaseAddress, *RegionSize, NewProtect, *OldProtect ): no SDK / WDK prototype (the
	// VirtualProtect documentation describes the Win32 layer); semantics after Wine 11.0 dlls/ntdll/unix/virtual.c:5467-5530
	// (OldProtect NULL = STATUS_ACCESS_VIOLATION; the range = the pages of [base, base + size); it must lie in ONE allocation /
	// view, else STATUS_INVALID_PARAMETER; every page committed, else STATUS_NOT_COMMITTED; OldProtect = the first page's
	// protection; base / size written back), set_protection :1998 (private memory: no WRITECOPY - STATUS_INVALID_PAGE_PROTECTION;
	// a view: no access beyond the view's maximum) and get_vprot_flags :1879 (in an image view PAGE_READWRITE / PAGE_EXECUTE_READWRITE
	// mean the copy-on-write forms). Loud stops: another process, size 0, PAGE_GUARD / NOCACHE / WRITECOMBINE (M7), the
	// environment's own regions (stack, PEB, TEB, KUSER: no allocation record yet).
	reply kernel::svc_protect_virtual_memory( const call& c )
	{
		if ( c.args[ 0 ] != current_process ) return stop_( "NtProtectVirtualMemory for ProcessHandle " + hex( c.args[ 0 ] ) + ": only NtCurrentProcess() is modelled" );
		if ( !c.stack_args ) return stop_( "NtProtectVirtualMemory: the stack argument OldProtect is not readable" );
		const uint64_t pbase = c.args[ 1 ], psize = c.args[ 2 ], pold = c.args[ 4 ];
		const uint32_t prot = uint32_t( c.args[ 3 ] );
		if ( !pold || !user_buffer( pold, 4, UC_PROT_WRITE ) ) return done_( status::access_violation, "OldProtect " + hex( pold ) + ": NULL or not writable" );
		if ( !user_buffer( pbase, 8, UC_PROT_READ | UC_PROT_WRITE ) || !user_buffer( psize, 8, UC_PROT_READ | UC_PROT_WRITE ) )
			return done_( status::access_violation, "BaseAddress " + hex( pbase ) + " / RegionSize " + hex( psize ) + ": not writable user memory (probe)" );
		uint64_t base = 0, size = 0;
		get64( pbase, base );
		get64( psize, size );
		if ( !size ) return stop_( "NtProtectVirtualMemory with *RegionSize 0: Windows' answer is not documented - not modelled" );
		if ( prot & ( page::guard | page::nocache | page::writecombine ) )
			return stop_( "NewProtect " + hex( prot ) + ": PAGE_GUARD / PAGE_NOCACHE / PAGE_WRITECOMBINE not modelled (guard pages: milestone M7)" );
		const uint64_t ps = f_.page_size, first = base & ~( ps - 1 );
		if ( base > UINT64_MAX - size ) return stop_( "*BaseAddress + *RegionSize above 2^64" );
		const uint64_t end = ( base + size + ps - 1 ) & ~( ps - 1 );
		std::vector<uint32_t>* pages = nullptr;
		uint64_t area_base = 0;
		bool image = false, valloc = false;
		uint32_t view_max = 0;
		if ( allocation* a = owner( first, end - first ) )
		{
			pages = &a->pages;
			area_base = a->base;
			valloc = true;
		}
		else if ( view* v = view_of( first ) )
		{
			if ( end - v->base > v->size ) return done_( status::invalid_parameter, "[" + hex( first ) + ", " + hex( end ) + ") crosses the end of the view " + v->name );
			pages = &v->pages;
			area_base = v->base;
			image = v->type == mem2::image;
			view_max = image ? page::execute_writecopy : v->allocation_protect;
		}
		else
		{
			for ( const auto& rg : e_.regions() )
				if ( first >= rg.base && first - rg.base < rg.size )
					return stop_( "NtProtectVirtualMemory of " + hex( first ) + " in the environment region \"" + rg.name + "\": no allocation record for it - not modelled" );
			return done_( status::invalid_parameter, "[" + hex( first ) + ", " + hex( end ) + ") is in no allocation or view (Wine virtual.c:5517: STATUS_INVALID_PARAMETER)" );
		}
		const size_t i0 = size_t( ( first - area_base ) / ps ), cnt = size_t( ( end - first ) / ps );
		for ( size_t i = i0; i < i0 + cnt; i++ )
			if ( !( *pages )[ i ] ) return done_( status::not_committed, "page " + hex( area_base + i * ps ) + " is not committed (STATUS_NOT_COMMITTED)" );
		// the new protection (get_vprot_flags): the base values; in an image the writable forms are copy-on-write
		uint32_t np = prot & 0xFF;
		switch ( np )
		{
			case page::noaccess: case page::readonly: case page::execute: case page::execute_read: case page::writecopy: case page::execute_writecopy: break;
			case page::readwrite: if ( image ) np = page::writecopy; break;
			case page::execute_readwrite: if ( image ) np = page::execute_writecopy; break;
			default: return done_( status::invalid_page_protection, "NewProtect " + hex( prot ) + " is no page protection (STATUS_INVALID_PAGE_PROTECTION)" );
		}
		if ( valloc && ( np == page::writecopy || np == page::execute_writecopy ) )
			return done_( status::invalid_page_protection, "copy-on-write protection for private memory (set_protection: STATUS_INVALID_PAGE_PROTECTION)" );
		if ( !valloc )
		{
			auto acc = []( uint32_t p ) {
				uint32_t a = 0;   // R 1, W 2, X 4 (copy-on-write counts as W)
				if ( p == page::readonly || p == page::writecopy || p == page::readwrite || p >= page::execute_read ) a |= 1;
				if ( p == page::readwrite || p == page::writecopy || p == page::execute_readwrite || p == page::execute_writecopy ) a |= 2;
				if ( p >= page::execute ) a |= 4;
				return a;
			};
			if ( ( acc( view_max ) & acc( np ) ) != acc( np ) )
				return done_( status::invalid_page_protection, "NewProtect " + page_text( np ) + " beyond the view's maximum " + page_text( view_max ) + " (STATUS_INVALID_PAGE_PROTECTION)" );
		}
		uint32_t ucp = 0;
		if ( np == page::writecopy ) ucp = UC_PROT_READ | UC_PROT_WRITE;
		else if ( np == page::execute_writecopy ) ucp = UC_PROT_ALL;
		else uc_prot_of_page( np, ucp );
		const uint32_t old = ( *pages )[ i0 ];
		std::string why;
		if ( !e_.protect( first, end - first, ucp, why ) ) return stop_( "protect: " + why );
		for ( size_t i = i0; i < i0 + cnt; i++ ) ( *pages )[ i ] = np;
		reply rr = done_( status::success, "[" + hex( first ) + ", +" + hex( end - first ) + ") " + page_text( old ) + " -> " + page_text( np ) );
		const uint64_t sz = end - first;
		if ( !put( pbase, &first, 8, rr ) || !put( psize, &sz, 8, rr ) || !put( pold, &old, 4, rr ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}
}
