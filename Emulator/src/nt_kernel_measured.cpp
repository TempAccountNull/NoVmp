// emu::nt (plan 3.M5 --Sys, P39; ledgers U2050-U2079): the UNDOCUMENTED system services the environment's real ntdll / kernelbase /
// kernel32 call while a process starts under --Sys --loader. USER DECISION 2026-10-25 (plan line 3.M5): the PROTOTYPE of each service
// comes from System Informer's MIT-licensed phnt headers (v4.0.26241.138, E:\sources\oss\phnt-4.0.26241.138, reference only - our own
// declarations, file:line cited per service); its BEHAVIOUR is MEASURED by running our own self-written test PE tm_nt
// (Emulator/tests/pe/tm_nt, never sample-derived) natively in the VMware guest (decision 3.0.7) with the arguments the loader uses
// (taken from the emulated trace); the measured output shapes are recorded in Emulator/data/nt_measured_vmware_guest.txt (real
// identifiers replaced by synthesized placeholders of the same shape) and implemented here. Nothing is forwarded to the host at run
// time; an input combination that was not measured is a loud stop with the decoded arguments (never a guess).
#include <emu/nt_kernel.hpp>

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

		// the token pseudo handles (phnt ntpsapi.h:2489-2491 NtCurrentProcessToken() -4, NtCurrentThreadToken() -5,
		// NtCurrentThreadEffectiveToken() -6)
		constexpr uint64_t current_process_token = ~0ull - 3, current_thread_token = ~0ull - 4, current_thread_effective_token = ~0ull - 5;
	}

	bool kernel::dispatch_measured( const call& c, reply& r )
	{
		if ( c.name == "NtQuerySecurityAttributesToken" )
		{
			r = svc_query_security_attributes_token( c );
			return true;
		}
		return false;
	}

	// NtQuerySecurityAttributesToken( TokenHandle, Attributes (PCUNICODE_STRING array, NumberOfAttributes entries), NumberOfAttributes,
	// Buffer (TOKEN_SECURITY_ATTRIBUTES_INFORMATION), Length, ReturnLength ) - prototype phnt ntseapi.h:947-954; the output layout
	// TOKEN_SECURITY_ATTRIBUTES_INFORMATION {USHORT Version, USHORT Reserved, ULONG AttributeCount, PTOKEN_SECURITY_ATTRIBUTE_V1
	// AttributeV1} ntseapi.h:416-424, TOKEN_SECURITY_ATTRIBUTE_V1 ntseapi.h:375-391
	reply kernel::svc_query_security_attributes_token( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtQuerySecurityAttributesToken: the stack arguments (Length, ReturnLength) are not readable" );
		const uint64_t token = c.args[ 0 ], names = c.args[ 1 ], buf = c.args[ 3 ], pret = c.args[ 5 ];
		const uint32_t count = uint32_t( c.args[ 2 ] ), len = uint32_t( c.args[ 4 ] );
		std::string what = "TokenHandle " + std::string( token == current_process_token ? "NtCurrentProcessToken()" : token == current_thread_token ? "NtCurrentThreadToken()"
																					 : token == current_thread_effective_token ? "NtCurrentThreadEffectiveToken()" : hex( token ) ) +
						   ", " + std::to_string( count ) + " name(s) [";
		if ( count > 64 ) return stop_( "NtQuerySecurityAttributesToken: NumberOfAttributes " + std::to_string( count ) + " (more than 64 not modelled)" );
		if ( count && !user_buffer( names, uint64_t( count ) * 16, UC_PROT_READ ) ) return stop_( "NtQuerySecurityAttributesToken: the name array " + hex( names ) + " is not readable - not measured" );
		std::vector<std::string> nm;
		for ( uint32_t i = 0; i < count; i++ )
		{
			std::string s, why;
			if ( !guest_ustring( names + 16ull * i, s, why ) ) return stop_( "NtQuerySecurityAttributesToken: name " + std::to_string( i ) + ": " + why + " - not measured" );
			nm.push_back( s );
			what += ( i ? ", \"" : "\"" ) + s + "\"";
		}
		what += "], Buffer " + hex( buf ) + ", Length " + hex( len ) + ", ReturnLength " + hex( pret );
		// MEASURED (tm_nt case "sat", Emulator/data/nt_measured_vmware_guest.txt lines sat.0 - sat.name1; our own unpackaged process, the
		// process token): every non-empty subset of the names the loader asks - "WIN://SYSAPPID", "WIN://PKG" (the attributes of a
		// packaged application) - answers STATUS_NOT_FOUND (C0000225H) for every Length (0 .. 1000H, incl. the loader's 330H), writes
		// *ReturnLength = 0 and nothing into the buffer. Not measured (= loud stop): another token, other names, no names (the call
		// then returns the process' own attributes, e.g. TSA://ProcUnique - measured shape sat.none, values not modelled), ReturnLength NULL.
		bool known = token == current_process_token && count > 0 && pret != 0;
		for ( const auto& s : nm ) known = known && ( s == "WIN://SYSAPPID" || s == "WIN://PKG" );
		if ( !known ) return stop_( "NtQuerySecurityAttributesToken: " + what + " - this input is not measured (P39: phnt prototype ntseapi.h:947, behaviour from tm_nt case sat)" );
		if ( !user_buffer( pret, 4, UC_PROT_WRITE ) ) return stop_( "NtQuerySecurityAttributesToken: ReturnLength " + hex( pret ) + " not writable - not measured" );
		reply rr = done_( status::not_found, what + ": the process token holds no such attribute (an unpackaged process) - STATUS_NOT_FOUND, *ReturnLength = 0, the buffer untouched "
													"(measured: tm_nt sat)" );
		const uint32_t zero = 0;
		if ( !put( pret, &zero, 4, rr ) ) return stop_( "writing ReturnLength failed after the probe" );
		return rr;
	}

	// NtQuerySystemInformation( SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength ) - prototype phnt
	// ntexapi.h:8339-8344 (and um\winternl.h); the undocumented classes the loader asks:
	//   C5H SystemHypervisorSharedPageInformation (phnt ntexapi.h:2568): SYSTEM_HYPERVISOR_SHARED_PAGE_INFORMATION {PVOID
	//       HypervisorSharedUserVa} (ntexapi.h:7190-7197). MEASURED (tm_nt qsi.c5*, hvp.*): Length 8 / 10H = STATUS_SUCCESS, 8 bytes
	//       written (a non-NULL address), ReturnLength 8 when given; Length 0 / 4 = STATUS_INFO_LENGTH_MISMATCH with ReturnLength 8 and
	//       nothing written. The page: page-aligned, MEM_PRIVATE, MEM_COMMIT, PAGE_READONLY (AllocationProtect PAGE_READONLY), RegionSize
	//       1000H. Its contents (SYSTEM_HYPERVISOR_USER_SHARED_DATA ntexapi.h:7139: TimeUpdateLock, QpcMultiplier non-zero natively,
	//       QpcBias 0, the rest 0) are the host hypervisor's timing values - NOT MODELLED here (the page reads zero): they belong to the
	//       one virtual time base (milestone M6); the emulated KUSER_SHARED_DATA QpcData is 0 (natively 83H: bypass via this page +
	//       RDTSCP), so ntdll's QueryPerformanceCounter takes the system call (the virtual clock) and never reads the page. The page is
	//       reserved + committed read-only at the first query (top-down; Windows maps it when the process is created - the address is
	//       not observable earlier through any modelled service).
	//   37H SystemNumaProcessorMap (phnt ntexapi.h:2426): SYSTEM_NUMA_INFORMATION {ULONG HighestNodeNumber, ULONG Reserved,
	//       GROUP_AFFINITY ActiveProcessorsGroupAffinity[40H]} (ntexapi.h:3859-3869; GROUP_AFFINITY {KAFFINITY Mask, WORD Group, WORD
	//       Reserved[3]} um\winnt.h). MEASURED (tm_nt qsi.37*): one node (HighestNodeNumber 0), node 0 = Mask = the active processors
	//       (FFH for the 8 of the guest profile), Group 0, Reserved 0; Length 0 = STATUS_INFO_LENGTH_MISMATCH, ReturnLength 4, nothing
	//       written; Length 4 .. 17H = STATUS_SUCCESS, only HighestNodeNumber written (4 bytes), ReturnLength 4; Length 18H .. 410H (incl.
	//       the loader's 408H) = STATUS_SUCCESS, HighestNodeNumber + the node 0 entry written (Reserved at +4 NOT written), ReturnLength
	//       18H. Not measured (stop): Length 1 .. 3. One NUMA node is this environment's configuration (the profile's processors, one
	//       group); another node count is not modelled.
	bool kernel::query_system_measured( const call& c, reply& r )
	{
		const uint32_t cls = uint32_t( c.args[ 0 ] ), len = uint32_t( c.args[ 2 ] );
		const uint64_t out = c.args[ 1 ], pret = c.args[ 3 ];
		if ( cls != 0xC5 && cls != 0x37 && cls != 0xC0 ) return false;
		if ( pret && !user_buffer( pret, 4, UC_PROT_WRITE ) )
		{
			r = done_( status::access_violation, "ReturnLength " + hex( pret ) + " not writable" );
			return true;
		}
		auto finish = [ & ]( uint32_t st, const std::string& what, const std::vector<uint8_t>& data, uint32_t rl ) {
			if ( !data.empty() && !user_buffer( out, data.size(), UC_PROT_WRITE ) )
			{
				r = done_( status::access_violation, "SystemInformation " + hex( out ) + " not writable" );
				return;
			}
			r = done_( st, what );
			if ( ( !data.empty() && !put( out, data.data(), data.size(), r ) ) || ( pret && !put( pret, &rl, 4, r ) ) ) r = stop_( "writing the result failed after the probe" );
		};
		if ( cls == 0xC0 )
		{
			// C0H SystemFlushInformation (phnt ntexapi.h:2563): SYSTEM_FLUSH_INFORMATION {ULONG SupportedFlushMethods (bit 0 always present,
			// bits 1 / 2 processor feature bits 35 / 36), ULONG ProcessorCacheFlushSize, ULONGLONG SystemFlushCapabilities, ULONGLONG
			// Reserved[2]} (ntexapi.h:7057-7084), 20H bytes. MEASURED (tm_nt qsi.c0*): Length 20H / 28H = STATUS_SUCCESS, 20H bytes
			// written - SupportedFlushMethods 7, ProcessorCacheFlushSize 40H, the rest 0 - ReturnLength 20H; Length 0 / 10H / 1FH =
			// STATUS_INFO_LENGTH_MISMATCH, ReturnLength 20H, nothing written. Not measured (stop): Length above 28H.
			if ( len < 0x20 )
			{
				finish( status::info_length_mismatch, "SystemFlushInformation: Length " + hex( len ) + " below 20H - STATUS_INFO_LENGTH_MISMATCH, ReturnLength 20H (measured: tm_nt qsi.c0.0 / .10 / .1f)", {}, 0x20 );
				return true;
			}
			if ( len > 0x28 )
			{
				r = stop_( "SystemFlushInformation: Length " + hex( len ) + " - not measured (up to 28H is)" );
				return true;
			}
			std::vector<uint8_t> d( 0x20, 0 );
			d[ 0 ] = 7;
			d[ 4 ] = 0x40;
			finish( status::success, "SystemFlushInformation: SupportedFlushMethods 7, ProcessorCacheFlushSize 40H, SystemFlushCapabilities 0 (measured: tm_nt qsi.c0 / qsi.c0r / qsi.c0.28)", d, 0x20 );
			return true;
		}
		if ( cls == 0xC5 )
		{
			if ( len == 0 || len == 4 )
			{
				finish( status::info_length_mismatch, "SystemHypervisorSharedPageInformation: Length " + hex( len ) + " - STATUS_INFO_LENGTH_MISMATCH, ReturnLength 8 (measured: tm_nt qsi.c5.0 / qsi.c5.4)", {}, 8 );
				return true;
			}
			if ( len != 8 && len != 0x10 )
			{
				r = stop_( "SystemHypervisorSharedPageInformation: Length " + hex( len ) + " - not measured (0, 4, 8, 10H are)" );
				return true;
			}
			std::string how = "the existing page";
			if ( !hv_page_ )
			{
				uint64_t base = 0, size = f_.page_size;
				reply a = allocate( base, size, mem::commit | mem::reserve | mem::top_down, page::readonly );
				if ( a.kind != reply_kind::done || a.status != status::success )
				{
					r = stop_( "SystemHypervisorSharedPageInformation: mapping the hypervisor shared page failed: " + a.text );
					return true;
				}
				hv_page_ = base;
				how = "mapped now: MEM_PRIVATE, PAGE_READONLY, 1000H bytes";
			}
			std::vector<uint8_t> d( 8 );
			std::memcpy( d.data(), &hv_page_, 8 );
			finish( status::success, "SystemHypervisorSharedPageInformation: HypervisorSharedUserVa " + hex( hv_page_ ) + " (" + how +
										  "; contents zero - the timing values are milestone M6; measured: tm_nt qsi.c5 / hvp)", d, 8 );
			return true;
		}
		// 37H
		if ( len >= 1 && len <= 3 )
		{
			r = stop_( "SystemNumaProcessorMap: Length " + hex( len ) + " - not measured (0, 4 .. 410H are)" );
			return true;
		}
		if ( len == 0 )
		{
			finish( status::info_length_mismatch, "SystemNumaProcessorMap: Length 0 - STATUS_INFO_LENGTH_MISMATCH, ReturnLength 4 (measured: tm_nt qsi.37.0)", {}, 4 );
			return true;
		}
		if ( len < 0x18 )
		{
			finish( status::success, "SystemNumaProcessorMap: Length " + hex( len ) + " - HighestNodeNumber 0 only, ReturnLength 4 (measured: tm_nt qsi.37.4 .. qsi.37.17)", std::vector<uint8_t>( 4, 0 ), 4 );
			return true;
		}
		if ( len > 0x410 )
		{
			r = stop_( "SystemNumaProcessorMap: Length " + hex( len ) + " - not measured (up to 410H is)" );
			return true;
		}
		// HighestNodeNumber 0, Reserved NOT written: write [0, 4) and the node 0 GROUP_AFFINITY at [8, 18H) separately
		const uint32_t highest = 0;
		std::vector<uint8_t> node( 16, 0 );
		std::memcpy( node.data(), &f_.active_processor_mask, 8 );
		if ( !user_buffer( out, 0x18, UC_PROT_WRITE ) )
		{
			r = done_( status::access_violation, "SystemInformation " + hex( out ) + " not writable" );
			return true;
		}
		r = done_( status::success, "SystemNumaProcessorMap: one node - HighestNodeNumber 0, node 0 Mask " + hex( f_.active_processor_mask ) +
										" Group 0 (Reserved at +4 not written), ReturnLength 18H (measured: tm_nt qsi.37 / qsi.37r / qsi.37.18 / qsi.37.410)" );
		const uint32_t rl = 0x18;
		if ( !put( out, &highest, 4, r ) || !put( out + 8, node.data(), 16, r ) || ( pret && !put( pret, &rl, 4, r ) ) ) r = stop_( "writing the result failed after the probe" );
		return true;
	}
}
