// emu::nt (plan 3.M5 --Sys, P39; ledgers U2050-U2079): the UNDOCUMENTED system services the environment's real ntdll / kernelbase /
// kernel32 call while a process starts under --Sys --loader. USER DECISION 2026-10-25 (plan line 3.M5): the PROTOTYPE of each service
// comes from System Informer's MIT-licensed phnt headers (v4.0.26241.138, E:\sources\oss\phnt-4.0.26241.138, reference only - our own
// declarations, file:line cited per service); its BEHAVIOUR is MEASURED by running our own self-written test PE tm_nt
// (Emulator/tests/pe/tm_nt, never sample-derived) natively in the VMware guest (decision 3.0.7) with the arguments the loader uses
// (taken from the emulated trace); the measured output shapes are recorded in Emulator/data/nt_measured_vmware_guest.txt (real
// identifiers replaced by synthesized placeholders of the same shape) and implemented here. Nothing is forwarded to the host at run
// time; an input combination that was not measured is a loud stop with the decoded arguments (never a guess).
#include <emu/nt_kernel.hpp>
#include <emu/pe_image.hpp>
#include <emu/win_process.hpp>   // U2105: emu::win::system_image_slot (the KnownDlls slots)
#include <emu/vclock.hpp>        // U2107: the model's boot time (the file-time placeholder)

#include <algorithm>   // P45 (U2135): std::min / std::max
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
		if ( c.name == "NtCreateWaitCompletionPacket" )
		{
			r = svc_create_wait_completion_packet( c );
			return true;
		}
		if ( c.name == "NtCreateIoCompletion" )
		{
			r = svc_create_io_completion( c );
			return true;
		}
		if ( c.name == "NtCreateWorkerFactory" )
		{
			r = svc_create_worker_factory( c );
			return true;
		}
		if ( c.name == "NtCreateTimer2" )
		{
			// NtCreateTimer2( *TimerHandle, TimerId, ObjectAttributes, Attributes, DesiredAccess ) - prototype phnt ntexapi.h:1410-1416
			// (attributes :1356-1361). MEASURED (tm_nt tmr2): TimerId NULL, ObjectAttributes NULL, Attributes 8 (TIMER2_ATTRIBUTE_NO_WAKE),
			// DesiredAccess 100002H = STATUS_SUCCESS, a new handle (non-zero, multiple of 4); NtClose = STATUS_SUCCESS. The timer is not set
			// (a set timer needs the virtual time base, M6). Not measured (stop): a TimerId, other attributes / access.
			if ( c.args[ 1 ] || uint32_t( c.args[ 3 ] ) != 8 ) r = stop_( "NtCreateTimer2 TimerId " + hex( c.args[ 1 ] ) + ", Attributes " + hex( uint32_t( c.args[ 3 ] ) ) + ": not measured (NULL, 8 are)" );
			else r = create_measured_object( c, c.args[ 0 ], uint32_t( c.args[ 4 ] ), c.args[ 2 ], "timer2", 8, { 0x100002u }, "tm_nt tmr2" );
			return true;
		}
		if ( c.name == "NtAssociateWaitCompletionPacket" )
		{
			r = svc_associate_wait_completion_packet( c );
			return true;
		}
		if ( c.name == "NtSetInformationWorkerFactory" )
		{
			r = svc_set_information_worker_factory( c );
			return true;
		}
		if ( c.name == "NtTraceControl" )
		{
			r = svc_trace_control( c );
			return true;
		}
		if ( c.name == "NtOpenDirectoryObject" || c.name == "NtOpenSymbolicLinkObject" )
		{
			r = svc_open_namespace_object( c );
			return true;
		}
		if ( c.name == "NtQuerySymbolicLinkObject" )
		{
			r = svc_query_symbolic_link( c );
			return true;
		}
		if ( c.name == "NtOpenSection" )
		{
			r = svc_open_section( c );
			return true;
		}
		if ( c.name == "NtOpenFile" )
		{
			r = svc_open_file( c );
			return true;
		}
		if ( c.name == "NtQueryVolumeInformationFile" )
		{
			r = svc_query_volume_information_file( c );
			return true;
		}
		if ( c.name == "NtMapViewOfSection" )
		{
			r = svc_map_view_of_section( c );
			return true;
		}
		if ( c.name == "NtSetInformationProcess" )
		{
			r = svc_set_information_process( c );
			return true;
		}
		if ( c.name == "NtOpenProcessToken" )
		{
			r = svc_open_process_token( c );
			return true;
		}
		if ( c.name == "NtQueryInformationToken" )
		{
			r = svc_query_information_token( c );
			return true;
		}
		if ( c.name == "NtAccessCheck" )
		{
			r = svc_access_check( c );
			return true;
		}
		if ( c.name == "NtReleaseMutant" || ( c.name == "NtWaitForSingleObject" && handles_.count( c.args[ 0 ] ) && handles_.at( c.args[ 0 ] ).kind == "mutant" ) )
		{
			r = svc_mutant_wait_release( c );
			return true;
		}
		if ( c.name == "NtOpenSemaphore" || c.name == "NtCreateSemaphore" )
		{
			r = svc_wil_semaphore( c );
			return true;
		}
		if ( c.name == "NtOpenMutant" || c.name == "NtOpenEvent" )
		{
			r = svc_named_decode( c );
			return true;
		}
		if ( c.name == "NtSetWnfProcessNotificationEvent" || c.name == "NtSubscribeWnfStateChange" )
		{
			r = svc_wnf_more( c );
			return true;
		}
		if ( c.name == "NtTestAlert" )
		{
			// P41 (U2081): NtTestAlert() - phnt ntpsapi.h (no arguments); the end of the loader's process initialisation. MEASURED (tm_nt alert):
			// a thread with no queued APC that was not alerted = STATUS_SUCCESS (twice). The emulated process has one thread, no APC is ever
			// queued (no APC service is modelled) and nothing alerts it: STATUS_SUCCESS.
			r = done_( status::success, "NtTestAlert(): no APC queued, not alerted - STATUS_SUCCESS (measured: tm_nt alert)" );
			return true;
		}
		if ( c.name == "NtSetTimer2" )
		{
			r = svc_set_timer2( c );
			return true;
		}
		if ( c.name == "NtQueryWnfStateData" )
		{
			r = svc_query_wnf_state_data( c );
			return true;
		}
		if ( c.name == "NtCreateMutant" )
		{
			r = svc_create_mutant( c );
			return true;
		}
		if ( c.name == "NtWriteFile" )
		{
			r = svc_write_file( c );
			return true;
		}
		if ( c.name == "NtApphelpCacheControl" )
		{
			r = svc_apphelp_cache_control( c );
			return true;
		}
		if ( c.name == "NtSetInformationVirtualMemory" )
		{
			r = svc_set_information_virtual_memory( c );
			return true;
		}
		if ( c.name == "NtCreateSection" )
		{
			r = svc_create_section( c );
			return true;
		}
		if ( c.name == "NtConnectPort" )
		{
			r = svc_connect_port( c );
			return true;
		}
		if ( c.name == "NtQuerySystemInformationEx" )
		{
			r = svc_query_system_information_ex( c );
			return true;
		}
		if ( c.name == "NtInitializeNlsFiles" )
		{
			r = svc_initialize_nls_files( c );
			return true;
		}
		if ( c.name == "NtQueryAttributesFile" )
		{
			r = svc_query_attributes_file( c );   // P43 (U2107)
			return true;
		}
		if ( c.name == "NtCreateFile" )
		{
			if ( !svc_create_file_measured( c, r ) ) r = vfs_create_file( c );   // P45 (U2129): the emulated file system (nt_kernel_vfs.cpp)
			return true;
		}
		if ( c.name == "NtAddAtomEx" || c.name == "NtFindAtom" || c.name == "NtDeleteAtom" || c.name == "NtQueryInformationAtom" )
		{
			r = svc_add_atom( c );   // P45 (U2137): the atom table
			return true;
		}
		if ( c.name == "NtReadVirtualMemory" )
		{
			r = svc_read_virtual_memory( c );   // P45 (U2135)
			return true;
		}
		if ( c.name == "NtQueryFullAttributesFile" )
		{
			r = svc_query_full_attributes_file( c );   // P45 (U2129)
			return true;
		}
		if ( c.name == "NtQueryDirectoryFile" || c.name == "NtQueryDirectoryFileEx" )
		{
			r = svc_query_directory_file( c, c.name == "NtQueryDirectoryFileEx" );   // P45 (U2129)
			return true;
		}
		return false;
	}

	// NtQuerySystemInformationEx( SystemInformationClass, InputBuffer, InputBufferLength, SystemInformation, SystemInformationLength,
	// ReturnLength ) - prototype phnt ntexapi.h:8362-8369
	reply kernel::svc_query_system_information_ex( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtQuerySystemInformationEx: the stack arguments are not readable" );
		const uint32_t cls = uint32_t( c.args[ 0 ] ), ilen = uint32_t( c.args[ 2 ] ), len = uint32_t( c.args[ 4 ] );
		const uint64_t in = c.args[ 1 ], out = c.args[ 3 ], pret = c.args[ 5 ];
		std::string what = "class " + hex( cls ) + ", InputBuffer " + hex( in ) + " (" + hex( ilen ) + " bytes";
		if ( ilen && ilen <= 16 && user_buffer( in, ilen, UC_PROT_READ ) )
		{
			uint8_t b[ 16 ] = {};
			std::string err;
			e_.read( in, b, ilen, err );
			what += ":";
			for ( uint32_t i = 0; i < ilen; i++ ) what += " " + hex( b[ i ] );
		}
		what += "), SystemInformation " + hex( out ) + ", Length " + hex( len ) + ", ReturnLength " + hex( pret );
		// class 6BH SystemLogicalProcessorAndGroupInformation (phnt ntexapi.h:2478; input LOGICAL_PROCESSOR_RELATIONSHIP, output
		// SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX - um\winnt.h, documented with GetLogicalProcessorInformationEx). MEASURED (tm_nt qsix.6b.*,
		// every relationship 0 / 1 / 2 / 3 / 4 / 7 / FFFFH at Length 0 / 50H / 2000H): the loader asks RelationNumaNode (1): one record of
		// 30H bytes {Relationship 1, Size 30H, NUMA_NODE_RELATIONSHIP NodeNumber 0, Reserved (20 bytes) 0, GroupMask {Mask = the active
		// processors FFH, Group 0, Reserved 0}} - Length 50H / 2000H = STATUS_SUCCESS, 30H bytes, ReturnLength 30H; Length 0 =
		// STATUS_INFO_LENGTH_MISMATCH, ReturnLength 30H, nothing written. The other relationships are measured (data file) but not
		// modelled yet (stop), as are other lengths and input sizes.
		if ( cls != 0x6B || ilen != 4 || !user_buffer( in, 4, UC_PROT_READ ) ) return stop_( "NtQuerySystemInformationEx " + what + " - not measured" );
		uint32_t rel = 0;
		{
			std::string err;
			e_.read( in, &rel, 4, err );
		}
		if ( rel != 1 ) return stop_( "NtQuerySystemInformationEx " + what + " - relationship " + hex( rel ) + " measured (tm_nt qsix.6b." + hex( rel ) + ".*) but not modelled yet" );
		if ( len != 0 && ( len < 0x50 || len > 0x2000 ) ) return stop_( "NtQuerySystemInformationEx " + what + " - RelationNumaNode Length not measured (0, 50H .. 2000H are)" );
		if ( pret && !user_buffer( pret, 4, UC_PROT_WRITE ) ) return done_( status::access_violation, "ReturnLength " + hex( pret ) + " not writable" );
		const uint32_t rl = 0x30;
		if ( len == 0 )
		{
			reply rr = done_( status::info_length_mismatch, "SystemLogicalProcessorAndGroupInformation RelationNumaNode, Length 0: STATUS_INFO_LENGTH_MISMATCH, ReturnLength 30H (measured: tm_nt qsix.6b.1.0)" );
			if ( pret && !put( pret, &rl, 4, rr ) ) return stop_( "writing ReturnLength failed after the probe" );
			return rr;
		}
		if ( !user_buffer( out, 0x30, UC_PROT_WRITE ) ) return done_( status::access_violation, "SystemInformation " + hex( out ) + " not writable" );
		std::vector<uint8_t> d( 0x30, 0 );
		d[ 0 ] = 1;      // Relationship RelationNumaNode
		d[ 4 ] = 0x30;   // Size
		std::memcpy( d.data() + 0x20, &f_.active_processor_mask, 8 );   // GroupMask.Mask; Group 0
		reply rr = done_( status::success, "SystemLogicalProcessorAndGroupInformation RelationNumaNode: one node 0, GroupMask " + hex( f_.active_processor_mask ) +
											   " group 0, 30H bytes (measured: tm_nt qsix.6b.1.50 / .2000)" );
		if ( !put( out, d.data(), d.size(), rr ) || ( pret && !put( pret, &rl, 4, rr ) ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtCreateWaitCompletionPacket( *WaitCompletionPacketHandle, DesiredAccess, ObjectAttributes ) - prototype phnt ntioapi.h:3512-3516
	// (Microsoft devnotes "NtCreateWaitCompletionPacket" named there). MEASURED (tm_nt wcp*): DesiredAccess 1 (the loader's) and 1F0003H
	// with ObjectAttributes NULL = STATUS_SUCCESS and a new handle (non-zero, a multiple of 4); NtClose of it = STATUS_SUCCESS. The
	// object: a wait completion packet of this process' handle table, not associated (NtAssociateWaitCompletionPacket gives it a target).
	// Not measured (stop): ObjectAttributes not NULL (a named / attributed packet), another access mask.
	reply kernel::svc_create_wait_completion_packet( const call& c )
	{
		return create_measured_object( c, c.args[ 0 ], uint32_t( c.args[ 1 ] ), c.args[ 2 ], "wait completion packet", 0, { 1u, 0x1F0003u }, "tm_nt wcp" );
	}

	// NtCreateIoCompletion( *IoCompletionHandle, DesiredAccess, ObjectAttributes, NumberOfConcurrentThreads ) - prototype phnt
	// ntioapi.h:3327-3332 (the Win32 layer: CreateIoCompletionPort). MEASURED (tm_nt iocp.*): DesiredAccess 1F0003H, ObjectAttributes NULL,
	// Count 9 (the loader's) / 0 / 1 / 8 = STATUS_SUCCESS and a new handle (non-zero, a multiple of 4); NtClose of it = STATUS_SUCCESS.
	// The object: an I/O completion port of this process' handle table with an empty packet queue (value = the concurrency count).
	reply kernel::svc_create_io_completion( const call& c )
	{
		return create_measured_object( c, c.args[ 0 ], uint32_t( c.args[ 1 ] ), c.args[ 2 ], "io completion", uint32_t( c.args[ 3 ] ), { 0x1F0003u }, "tm_nt iocp" );
	}

	// NtCreateWorkerFactory( *WorkerFactoryHandleReturn, DesiredAccess, ObjectAttributes, CompletionPortHandle, WorkerProcessHandle,
	// StartRoutine, StartParameter, MaxThreadCount, StackReserve, StackCommit ) - prototype phnt ntexapi.h:2093-2104
	reply kernel::svc_create_worker_factory( const call& c )
	{
		// MEASURED (tm_nt wkf*): the loader's arguments - DesiredAccess F00FFH, ObjectAttributes NULL, an I/O completion port handle, the
		// current process, a start routine + parameter, MaxThreadCount 200H, StackReserve 100000H, StackCommit 1000H - = STATUS_SUCCESS and a
		// new handle (non-zero, a multiple of 4); NO thread runs the start routine (0 runs, also after 200 ms: the factory creates workers
		// only on demand); a CompletionPortHandle of another object type (the process) = STATUS_OBJECT_TYPE_MISMATCH (C0000024H), nothing
		// written; NtClose of the factory = STATUS_SUCCESS. Not measured (stop): other access / thread / stack values, another process,
		// a port handle that is not in the table.
		if ( !c.stack_args ) return stop_( "NtCreateWorkerFactory: the stack arguments are not readable" );
		std::string a;
		for ( int i = 0; i < 10; i++ ) a += ( i ? ", " : "" ) + hex( c.args[ size_t( i ) ] );
		const uint64_t port = c.args[ 3 ];
		// P42 (U2099, sample run 7): StackReserve 800000H too - the sample's own SizeOfStackReserve that the loader passes (P41 run 6 system
		// call 40); MEASURED tm_nt wkf.800: the same call with 800000H on a fresh port = STATUS_SUCCESS and a handle, as with 100000H
		if ( c.args[ 4 ] != current_process || uint32_t( c.args[ 7 ] ) != 0x200 || ( c.args[ 8 ] != 0x100000 && c.args[ 8 ] != 0x800000 ) || c.args[ 9 ] != 0x1000 )
			return stop_( "NtCreateWorkerFactory( " + a + " ) - not measured (tm_nt wkf / wkf.800: NtCurrentProcess(), 200H, 100000H or 800000H, 1000H)" );
		if ( port == current_process ) return done_( status::object_type_mismatch, "NtCreateWorkerFactory: CompletionPortHandle NtCurrentProcess() is not an I/O completion port - STATUS_OBJECT_TYPE_MISMATCH (measured: tm_nt wkf.badport)" );
		const auto it = handles_.find( port );
		if ( it == handles_.end() ) return stop_( "NtCreateWorkerFactory( " + a + " ) - CompletionPortHandle is not a handle of this process: not measured" );
		if ( it->second.kind != "io completion" ) return done_( status::object_type_mismatch, "NtCreateWorkerFactory: CompletionPortHandle " + hex( port ) + " is a " + it->second.kind + ", not an I/O completion port - STATUS_OBJECT_TYPE_MISMATCH (measured: tm_nt wkf.badport)" );
		reply r = create_measured_object( c, c.args[ 0 ], uint32_t( c.args[ 1 ] ), c.args[ 2 ], "worker factory", port, { 0xF00FFu }, "tm_nt wkf" );
		if ( r.kind == reply_kind::done ) r.text += "; start routine " + hex( c.args[ 5 ] ) + " / parameter " + hex( c.args[ 6 ] ) + " - no worker thread is created (measured: 0 runs)";
		return r;
	}

	// NtAssociateWaitCompletionPacket( WaitCompletionPacketHandle, IoCompletionHandle, TargetObjectHandle, KeyContext, ApcContext, IoStatus,
	// IoStatusInformation, AlreadySignaled ) - prototype phnt ntioapi.h:3552-3561 (Microsoft devnotes named there)
	reply kernel::svc_associate_wait_completion_packet( const call& c )
	{
		// MEASURED (tm_nt awcp.*): a packet + an I/O completion port + a target that is NOT signaled - a timer2 that is not set (the loader's
		// case), a non-signaled event, the process - with IoStatusInformation 0 / 1 = STATUS_SUCCESS and *AlreadySignaled = FALSE (when a
		// pointer is given; NULL accepted); a SIGNALED synchronization event = STATUS_SUCCESS, *AlreadySignaled = TRUE (the packet is then
		// queued to the port - not modelled here: stop). The association is recorded on the packet (a later signal of the target would
		// queue the packet: one thread, no waiter - not modelled; any such signal stops at the service that would cause it).
		if ( !c.stack_args ) return stop_( "NtAssociateWaitCompletionPacket: the stack arguments are not readable" );
		std::string a;
		for ( int i = 0; i < 8; i++ ) a += ( i ? ", " : "" ) + hex( c.args[ size_t( i ) ] );
		const auto pk = handles_.find( c.args[ 0 ] ), io = handles_.find( c.args[ 1 ] ), tg = handles_.find( c.args[ 2 ] );
		if ( pk == handles_.end() || pk->second.kind != "wait completion packet" || io == handles_.end() || io->second.kind != "io completion" )
			return stop_( "NtAssociateWaitCompletionPacket( " + a + " ): packet / port handles of other kinds - not measured" );
		bool signaled = false;
		std::string tk;
		if ( c.args[ 2 ] == current_process ) tk = "the process (not signaled)";
		else if ( tg == handles_.end() ) return stop_( "NtAssociateWaitCompletionPacket( " + a + " ): the target is not a handle of this process - not measured" );
		else if ( tg->second.kind == "timer2" ) tk = "timer2 " + hex( c.args[ 2 ] ) + " (not set)";
		else if ( tg->second.kind.rfind( "event", 0 ) == 0 )
		{
			signaled = tg->second.value != 0;
			tk = tg->second.kind + " " + hex( c.args[ 2 ] ) + ( signaled ? " (signaled)" : " (not signaled)" );
			if ( signaled && tg->second.kind != "event (synchronization)" ) return stop_( "NtAssociateWaitCompletionPacket( " + a + " ): a signaled notification event - not measured" );
		}
		else return stop_( "NtAssociateWaitCompletionPacket( " + a + " ): target " + tg->second.kind + " - not measured" );
		if ( signaled ) return stop_( "NtAssociateWaitCompletionPacket( " + a + " ): target " + tk + " - measured (AlreadySignaled TRUE, the packet queued), the port queue is not modelled" );
		if ( c.args[ 6 ] > 1 || c.args[ 5 ] ) return stop_( "NtAssociateWaitCompletionPacket( " + a + " ): IoStatus / IoStatusInformation not measured (0 / 0, 1 are)" );
		const uint64_t pflag = c.args[ 7 ];
		if ( pflag && !user_buffer( pflag, 1, UC_PROT_WRITE ) ) return stop_( "NtAssociateWaitCompletionPacket( " + a + " ): AlreadySignaled not writable - not measured" );
		pk->second.value = c.args[ 1 ];
		pk->second.target = "associated: port " + hex( c.args[ 1 ] ) + ", target " + hex( c.args[ 2 ] ) + ", key " + hex( c.args[ 3 ] ) + ", apc " + hex( c.args[ 4 ] );
		reply rr = done_( status::success, "packet " + hex( c.args[ 0 ] ) + " associated with port " + hex( c.args[ 1 ] ) + " and " + tk + ", AlreadySignaled FALSE (measured: tm_nt awcp)" );
		const uint8_t f = 0;
		if ( pflag && !put( pflag, &f, 1, rr ) ) return stop_( "writing AlreadySignaled failed after the probe" );
		return rr;
	}

	// NtSetInformationWorkerFactory( WorkerFactoryHandle, WorkerFactoryInformationClass, WorkerFactoryInformation, Length ) - prototype phnt
	// ntexapi.h:2122-2127 (classes :2038-2057)
	reply kernel::svc_set_information_worker_factory( const call& c )
	{
		// MEASURED (tm_nt swf.*; a factory created as the loader creates it): every (class, Length, value) below with its status; the
		// value is recorded on the factory (no observable effect here). Class 4 (WorkerFactoryThreadMinimum) = 1 makes Windows START a
		// worker thread (measured: the start routine ran) - threads are not modelled (one thread): a loud stop. The loader's own sequence on
		// a fresh factory (class 5 = 300H, 14 = 180H, 3 = 1) starts NO worker (tm_nt swf.loader.*: no run within 2 s). Every other
		// combination: stop.
		struct row { uint32_t cls, len; uint64_t v; uint32_t st; const char* name; };
		static const row rows[] = {
			{ 5, 4, 0x300, 0, "WorkerFactoryThreadMaximum" }, { 5, 4, 0x200, 0, "WorkerFactoryThreadMaximum" }, { 5, 4, 1, 0, "WorkerFactoryThreadMaximum" },
			{ 5, 4, 0x10, 0, "WorkerFactoryThreadMaximum" }, { 4, 4, 0, 0, "WorkerFactoryThreadMinimum" }, { 3, 4, 1, 0, "WorkerFactoryBindingCount" },
			{ 6, 4, 0, status::not_implemented, "WorkerFactoryPaused" }, { 9, 4, 0, 0xC0000001, "WorkerFactoryCallbackType" },
			{ 10, 4, 0x100000, status::info_length_mismatch, "WorkerFactoryStackInformation" }, { 11, 4, 8, 0, "WorkerFactoryThreadBasePriority" },
			{ 12, 4, 0, 0, "WorkerFactoryTimeoutWaiters" }, { 13, 4, 0, 0, "WorkerFactoryFlags" }, { 14, 4, 0x180, 0, "WorkerFactoryThreadSoftMaximum" }, { 14, 4, 0x200, 0, "WorkerFactoryThreadSoftMaximum" },
			{ 0, 8, 0xFFFFFFFFFF676980ull, status::invalid_info_class, "WorkerFactoryTimeout" }, { 1, 8, 0xFFFFFFFFFF676980ull, status::invalid_info_class, "WorkerFactoryRetryTimeout" },
			{ 2, 8, 0xFFFFFFFFDC3CBA00ull, 0, "WorkerFactoryIdleTimeout" }, { 5, 8, 0x200, status::info_length_mismatch, "WorkerFactoryThreadMaximum" },
			{ 5, 0, 0x200, status::info_length_mismatch, "WorkerFactoryThreadMaximum" },
			// P41 (U2080): the loader's second factory (exploration x5) - MEASURED tm_nt swf.13.4.1 / swf.2.8.ffffffffee1e5d00 / swf.5.4.3
			{ 13, 4, 1, 0, "WorkerFactoryFlags" }, { 2, 8, 0xFFFFFFFFEE1E5D00ull, 0, "WorkerFactoryIdleTimeout" }, { 5, 4, 3, 0, "WorkerFactoryThreadMaximum" } };
		const uint32_t cls = uint32_t( c.args[ 1 ] ), len = uint32_t( c.args[ 3 ] );
		const auto it = handles_.find( c.args[ 0 ] );
		if ( it == handles_.end() || it->second.kind != "worker factory" ) return stop_( "NtSetInformationWorkerFactory: handle " + hex( c.args[ 0 ] ) + " is not a worker factory - not measured" );
		uint64_t v = 0;
		if ( len == 4 || len == 8 )
		{
			if ( !user_buffer( c.args[ 2 ], len, UC_PROT_READ ) ) return stop_( "NtSetInformationWorkerFactory: the value " + hex( c.args[ 2 ] ) + " is not readable - not measured" );
			std::string err;
			e_.read( c.args[ 2 ], &v, len, err );
		}
		const std::string what = "NtSetInformationWorkerFactory( " + hex( c.args[ 0 ] ) + ", class " + std::to_string( cls ) + ", value " + hex( v ) + ", Length " + hex( len ) + " )";
		if ( cls == 4 && len == 4 && v >= 1 )
			return stop_( what + " - WorkerFactoryThreadMinimum >= 1: Windows starts a worker thread (measured: tm_nt swf.4.4.1) - threads are not modelled" );
		for ( const auto& rw : rows )
			if ( rw.cls == cls && rw.len == len && ( len == 0 || rw.v == v ) )
				return done_( rw.st, what + ": " + rw.name + " - status " + hex( rw.st ) + ( rw.st ? "" : ", recorded" ) + " (measured: tm_nt swf." + std::to_string( cls ) + "." + std::to_string( len ) + ")" );
		return stop_( what + " - not measured" );
	}

	// NtTraceControl( FunctionCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength, ReturnLength ) - prototype phnt
	// ntwmi.h:6557-6564 (function codes :6348-6395)
	reply kernel::svc_trace_control( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtTraceControl: the stack arguments are not readable" );
		const uint32_t fn = uint32_t( c.args[ 0 ] ), ilen = uint32_t( c.args[ 2 ] ), olen = uint32_t( c.args[ 4 ] );
		uint64_t in = 0;
		if ( ilen && ilen <= 8 && user_buffer( c.args[ 1 ], ilen, UC_PROT_READ ) )
		{
			std::string err;
			e_.read( c.args[ 1 ], &in, ilen, err );
		}
		const auto it = handles_.find( in );
		const std::string what = "NtTraceControl( " + hex( fn ) + ", input " + hex( in ) + ( it != handles_.end() ? " (" + it->second.kind + ")" : std::string() ) + " (" + hex( ilen ) +
								 " bytes), output " + hex( c.args[ 3 ] ) + " / " + hex( olen ) + ", ReturnLength " + hex( c.args[ 5 ] ) + " )";
		// P41 (U2072): the input bytes for a stop's text (up to A0H; the measurement replays them)
		std::string inhex;
		if ( ilen && ilen <= 0xA0 && user_buffer( c.args[ 1 ], ilen, UC_PROT_READ ) )
		{
			uint8_t ib[ 0xA0 ] = {};
			std::string err;
			e_.read( c.args[ 1 ], ib, ilen, err );
			for ( uint32_t i = 0; i < ilen; i++ ) { char t2[ 4 ]; std::snprintf( t2, sizeof( t2 ), "%02X", ib[ i ] ); inhex += ( i ? " " : "" ) + std::string( t2 ); }
		}
		// function 1BH EtwAddNotificationEvent, 4 input bytes = a synchronization event handle of the process (the loader's call), no
		// output. MEASURED (tm_nt etw.*): in a native process (whose ntdll already made this call during its own start) the call answers
		// STATUS_ALREADY_REGISTERED (C0000718H) - so the process' FIRST registration succeeded (the only earlier call of function 1BH in
		// that process is the loader's own): the first call = STATUS_SUCCESS (inferred from the measurement, not observed directly), every
		// later one = C0000718H; input handle 0 = STATUS_INVALID_PARAMETER; 8 input bytes = STATUS_INVALID_PARAMETER; ReturnLength is
		// never written. The event is never signaled here (no ETW notification exists in the emulated environment). Every other function /
		// input: stop.
		// function 0FH EtwRegisterGuidsCode (phnt ntwmi.h:6362; in ETW_UM_REGISTRATION_INFORMATION / out ETW_UM_REGISTRATION_REPLY
		// :6439-6473, A0H bytes). MEASURED (tm_nt etw.reg / etw.reg2 / etw.reg3) with the loader's three inputs (in = out, A0H): ProviderGuid
		// 3595AB5C-042A-4C8E-B942-2D059BFEB1B1 (RegType 3, LoggerId 1, the rest 0), 305FC87B-002A-5E26-D297-60223012CA9C (RegType 3,
		// LoggerId 2, ReplyCallback set) and 901D2AFA-4FF6-46D7-8D0E-53645E1A47F5 (RegType 3, LoggerId 3, ReplyCallback set) = each
		// STATUS_SUCCESS, ReturnLength A0H; exactly two output fields change: ReplyHandle (+18H) = a new handle (non-zero, multiple of 4;
		// NtClose = STATUS_SUCCESS) and Reserved1 (+2CH) = A0H; no enable information (no ETW session enables these providers in the guest:
		// EnableCallbackContext .. HasPayload stay 0). Other providers / inputs (their enable state is the environment's): stop until measured.
		if ( fn == 0xF )
		{
			struct prov { uint8_t guid[ 16 ]; uint16_t logger; bool callback; const char* tag; };
			static const prov provs[] = {
				{ { 0x5C, 0xAB, 0x95, 0x35, 0x2A, 0x04, 0x8E, 0x4C, 0xB9, 0x42, 0x2D, 0x05, 0x9B, 0xFE, 0xB1, 0xB1 }, 1, false, "tm_nt etw.reg" },
				{ { 0x7B, 0xC8, 0x5F, 0x30, 0x2A, 0x00, 0x26, 0x5E, 0xD2, 0x97, 0x60, 0x22, 0x30, 0x12, 0xCA, 0x9C }, 2, true, "tm_nt etw.reg2" },
				{ { 0xFA, 0x2A, 0x1D, 0x90, 0xF6, 0x4F, 0xD7, 0x46, 0x8D, 0x0E, 0x53, 0x64, 0x5E, 0x1A, 0x47, 0xF5 }, 3, true, "tm_nt etw.reg3" },
				// P41 (U2072): kernelbase's provider of a process started without a console (trace c3, system call 105: RegType 3, LoggerId 4,
				// ReplyCallback set) - MEASURED tm_nt etw.reg4: STATUS_SUCCESS, ReplyHandle + Reserved1 written, not enabled
				{ { 0xB9, 0xAF, 0x74, 0x3C, 0x82, 0x8D, 0xE3, 0x44, 0xB5, 0x2C, 0x36, 0x5D, 0xBF, 0x48, 0x38, 0x2A }, 4, true, "tm_nt etw.reg4" },
				// P41 (U2072): three more providers of the start of a process without a console (exploration x4: kernelbase's 0A27EFF1-... /
				// FE5EF905-..., kernel32's 58446CE3-...; LoggerId 5 / 6 / 7, ReplyCallback set) - MEASURED tm_nt etw.reg5 / .reg6 / .reg7
				// provider 5 is ENABLED in the guest (an ETW session of the measuring environment enables it): the reply carries its enable
				// information (tm_nt etw.reg5.fields: EnableCallbackContext (+70H) FFH << 32 | 1, EnableFilterDesc (+78H) 41H, EnableCallback (+80H)
				// A0H << 56, ProviderTraits (+90H) 8, TraitsFlags (+98H) 1) - written as measured below (enabled_ = true)
				{ { 0x0A, 0x27, 0xEF, 0xF1, 0x32, 0x0D, 0x52, 0x43, 0xBA, 0x52, 0xDB, 0xAB, 0x41, 0xE1, 0xD8, 0x59 }, 5, true, "tm_nt etw.reg5" },
				{ { 0xFE, 0x5E, 0xF9, 0x05, 0x75, 0x7F, 0xC7, 0x49, 0xA9, 0x94, 0x60, 0xA5, 0x5C, 0xC0, 0x95, 0x71 }, 6, true, "tm_nt etw.reg6" },
				{ { 0x58, 0x44, 0x6C, 0xE3, 0x80, 0xED, 0xD7, 0x4A, 0xA8, 0xBE, 0x52, 0xDD, 0xA1, 0xEB, 0x5F, 0x1C }, 7, true, "tm_nt etw.reg7" },
				// P44 (U2121): user32's providers (T6 exploration x2, system calls 284 / 286 / 288: Microsoft.Windows.User32 2ECD5BF2-...,
				// InputCore AC5EB75F-..., Microsoft.Windows.OneCoreTransforms 250E91E0-...; LoggerId 8 / 9 / 10, ReplyCallback set) - MEASURED
				// tm_nt etw.reg8 / .reg9 / .reg10: STATUS_SUCCESS, ReplyHandle + Reserved1 written, not enabled
				{ { 0x2E, 0xCD, 0x5B, 0xF2, 0x90, 0x26, 0xDC, 0x55, 0x3B, 0xC4, 0x07, 0xB6, 0x5B, 0x1B, 0x41, 0xC9 }, 8, true, "tm_nt etw.reg8" },
				{ { 0xAC, 0x5E, 0xB7, 0x5F, 0x0B, 0x9F, 0x0C, 0x55, 0x33, 0x9F, 0xFC, 0x21, 0xFD, 0xE9, 0x66, 0xCD }, 9, true, "tm_nt etw.reg9" },
				{ { 0x25, 0x0E, 0x91, 0xE0, 0xCC, 0x6F, 0x32, 0x4F, 0x8F, 0x5E, 0xA4, 0x2C, 0xF7, 0xAC, 0xC5, 0xC3 }, 10, true, "tm_nt etw.reg10" },
				// P45 (U2139): dxgi's provider 03BBE5B8-C788-4D0B-B47E-5B5731398A89 (sample exploration x4 system call 601: RegType 3, LoggerId 11,
				// ReplyCallback set) - MEASURED tm_nt etw.reg11: STATUS_SUCCESS, ReplyHandle + Reserved1 written, not enabled
				{ { 0xB8, 0xE5, 0xBB, 0x03, 0x88, 0xC7, 0x0B, 0x4D, 0xB4, 0x7E, 0x5B, 0x57, 0x31, 0x39, 0x8A, 0x89 }, 11, true, "tm_nt etw.reg11" },
				// P45 (U2139): dxgi's second provider CA11C036-0102-4A2D-A6AD-F03CFED5D3C9 (sample exploration x6 system call 603: LoggerId 12,
				// ReplyCallback set) - MEASURED tm_nt etw.reg12: STATUS_SUCCESS, ReplyHandle + Reserved1 written, not enabled
				{ { 0x36, 0xC0, 0x11, 0xCA, 0x02, 0x01, 0x2D, 0x4A, 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 }, 12, true, "tm_nt etw.reg12" } };
			uint8_t d[ 0xA0 ] = {};
			std::string err, guid;
			if ( ilen != 0xA0 || olen != 0xA0 || c.args[ 1 ] != c.args[ 3 ] || !user_buffer( c.args[ 1 ], 0xA0, UC_PROT_READ | UC_PROT_WRITE ) )
				return stop_( what + " - EtwRegisterGuidsCode with another buffer layout: not measured (A0H in = out is)" );
			e_.read( c.args[ 1 ], d, 0xA0, err );
			for ( int i = 0; i < 16; i++ ) { char t[ 4 ]; std::snprintf( t, sizeof( t ), "%02X", d[ i ] ); guid += t; }
			const prov* hit = nullptr;
			for ( const auto& pv : provs )
			{
				uint64_t cb = 0;
				std::memcpy( &cb, d + 0x20, 8 );
				bool same = std::memcmp( d, pv.guid, 16 ) == 0 && d[ 0x10 ] == 3 && d[ 0x11 ] == 0 && d[ 0x12 ] == 0 && d[ 0x13 ] == 0 && d[ 0x14 ] == pv.logger && d[ 0x15 ] == 0 &&
							( cb != 0 ) == pv.callback;
				for ( int i = 0x16; i < 0xA0; i++ ) same = same && ( ( i >= 0x20 && i < 0x28 ) || d[ i ] == 0 );
				if ( same ) hit = &pv;
			}
			if ( !hit ) return stop_( what + " - EtwRegisterGuidsCode for provider bytes " + guid + " / another input: not measured (the provider's enable state is the environment's)" + "; input bytes " + inhex );
			if ( c.args[ 5 ] && !user_buffer( c.args[ 5 ], 4, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": ReturnLength not writable" );
			object o;
			o.kind = "etw registration";
			o.name = guid;
			const uint64_t h = open_handle( o );
			const bool enabled = hit->logger == 5;
			reply rr = done_( status::success, what + ": provider " + guid + " (LoggerId " + std::to_string( d[ 0x14 ] ) + ", ReplyCallback " + ( ( d[ 0x20 ] | d[ 0x21 ] | d[ 0x22 ] | d[ 0x23 ] | d[ 0x24 ] | d[ 0x25 ] | d[ 0x26 ] | d[ 0x27 ] ) ? "set" : "NULL" ) +
												 ") registered, ReplyHandle " + hex( h ) + ( enabled ? ", ENABLED as in the guest (the measured enable fields +70H .. +98H)" : ", not enabled" ) + " (measured: " + hit->tag + ")" );
			const uint32_t a0 = 0xA0;
			if ( !put( c.args[ 1 ] + 0x18, &h, 8, rr ) || !put( c.args[ 1 ] + 0x2C, &a0, 4, rr ) || ( c.args[ 5 ] && !put( c.args[ 5 ], &a0, 4, rr ) ) )
				return stop_( "writing the result failed after the probe" );
			if ( enabled )
			{
				const uint64_t ctx = ( uint64_t( 0xFF ) << 32 ) | 1, filt = 0x41, cb = uint64_t( 0xA0 ) << 56, traits = 8;
				const uint32_t tf = 1;
				if ( !put( c.args[ 1 ] + 0x70, &ctx, 8, rr ) || !put( c.args[ 1 ] + 0x78, &filt, 8, rr ) || !put( c.args[ 1 ] + 0x80, &cb, 8, rr ) || !put( c.args[ 1 ] + 0x90, &traits, 8, rr ) ||
					 !put( c.args[ 1 ] + 0x98, &tf, 4, rr ) )
					return stop_( "writing the result failed after the probe" );
			}
			return rr;
		}
		if ( fn == 0x1E )
		{
			// P41 (U2072): function 1EH EtwSetProviderTraitsCode (phnt ntwmi.h:6377; no layout in phnt) as kernelbase calls it after its
			// provider registration (etw.reg4): input 18H {the ReplyHandle, a pointer to the traits blob, USHORT size 33H, 6 more bytes},
			// output 78H, ReturnLength. MEASURED (tm_nt etwt.*, the same blob of the capture's kernelbase build, a fresh registration):
			// STATUS_SUCCESS, ReturnLength 0, no output byte written, the input unchanged. Other shapes / handles: stop.
			uint64_t h = 0, blob = 0;
			uint16_t sz = 0;
			if ( ilen != 0x18 || olen != 0x78 || !user_buffer( c.args[ 1 ], 0x18, UC_PROT_READ ) || !user_buffer( c.args[ 3 ], 0x78, UC_PROT_WRITE ) )
				return stop_( what + " - EtwSetProviderTraitsCode with another shape: not measured (18H in, 78H out is); input bytes " + inhex );
			std::string err;
			e_.read( c.args[ 1 ], &h, 8, err );
			e_.read( c.args[ 1 ] + 8, &blob, 8, err );
			e_.read( c.args[ 1 ] + 0x10, &sz, 2, err );
			const auto hr = handles_.find( h );
			// the measured (provider, traits size) pairs: tm_nt etwt (B9AF743C, 33H), etwt5 (0A27EFF1, 25H), etwt6 (FE5EF905, 39H), etwt7 (58446CE3, 39H)
			const std::string pn = hr != handles_.end() ? hr->second.name : std::string();
			const bool known = ( pn == "B9AF743C828DE344B52C365DBF48382A" && sz == 0x33 ) || ( pn == "0A27EFF1320D5243BA52DBAB41E1D859" && sz == 0x25 ) ||
							   ( pn == "FE5EF905757FC749A99460A55CC09571" && sz == 0x39 ) || ( pn == "58446CE380EDD74AA8BE52DDA1EB5F1C" && sz == 0x39 ) ||
							   // P44 (U2121): user32's three traits (tm_nt etwt8 / etwt9 / etwt10: STATUS_SUCCESS, ReturnLength 0, no output)
							   ( pn == "2ECD5BF29026DC553BC407B65B1B41C9" && sz == 0x2E ) || ( pn == "AC5EB75F0B9F0C55339FFC21FDE966CD" && sz == 0x0C ) ||
							   ( pn == "250E91E0CC6F324F8F5EA42CF7ACC5C3" && sz == 0x26 ) ||
							   // P45 (U2139): dxgi's traits (tm_nt etwt11: STATUS_SUCCESS, ReturnLength 0, no output)
							   ( pn == "B8E5BB0388C70B4DB47E5B5731398A89" && sz == 0x35 );
			if ( hr == handles_.end() || hr->second.kind != "etw registration" || !known || !user_buffer( blob, sz, UC_PROT_READ ) )
				return stop_( what + " - EtwSetProviderTraitsCode for handle " + hex( h ) + ", traits size " + hex( sz ) + ": not measured (tm_nt etwt*: the four providers' blobs are)" );
			if ( c.args[ 5 ] && !user_buffer( c.args[ 5 ], 4, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": ReturnLength not writable" );
			reply rr = done_( status::success, what + ": EtwSetProviderTraitsCode for registration " + hex( h ) + " (provider " + hr->second.name + "), traits " + hex( sz ) + " bytes at " + hex( blob ) + " -" +
												 " STATUS_SUCCESS, ReturnLength 0, no output written (measured: tm_nt etwt*.set)" );
			const uint32_t zero = 0;
			if ( c.args[ 5 ] && !put( c.args[ 5 ], &zero, 4, rr ) ) return stop_( "writing the result failed after the probe" );
			return rr;
		}
		if ( fn != 0x1B || c.args[ 3 ] || olen ) return stop_( what + " - not measured; input bytes " + inhex );
		if ( ilen == 8 ) return done_( status::invalid_parameter, what + ": 8 input bytes - STATUS_INVALID_PARAMETER (measured: tm_nt etw.event8)" );
		if ( ilen != 4 ) return stop_( what + " - input length not measured" );
		if ( in == 0 ) return done_( status::invalid_parameter, what + ": handle 0 - STATUS_INVALID_PARAMETER (measured: tm_nt etw.zero)" );
		if ( it == handles_.end() || it->second.kind != "event (synchronization)" ) return stop_( what + " - not a synchronization event: not measured" );
		if ( etw_notification_event_ ) return done_( 0xC0000718, what + ": a notification event is already registered (event " + hex( etw_notification_event_ ) + ") - STATUS_ALREADY_REGISTERED (measured: tm_nt etw.event)" );
		etw_notification_event_ = in;
		return done_( status::success, what + ": the process' ETW notification event registered (first call: STATUS_SUCCESS - inferred from tm_nt etw.event: a later call is STATUS_ALREADY_REGISTERED); never signaled here" );
	}

	// ------------------------------------------------------------------------------------------------ the object namespace: \KnownDlls
	// The environment's \KnownDlls object directory - MEASURED (tm_nt kd.*) in the VMware guest: NtOpenDirectoryObject( &h, 3, "\KnownDlls"
	// OBJ_CASE_INSENSITIVE ) = STATUS_SUCCESS + a handle; 43 entries - the symbolic link KnownDllPath and 42 sections, named below (system
	// DLL names, no identity); the link opened with access 1 = STATUS_SUCCESS; its target = this system's GetSystemDirectoryW()
	// ("<SystemRoot>\System32", 13H characters here), NtQuerySymbolicLinkObject with a large buffer = STATUS_SUCCESS, Length = the
	// target's bytes, MaximumLength unchanged, ReturnedLength = Length + 2. Prototypes: phnt ntobapi.h:548-552 / :751-755 / :770-774 (and
	// the WDK's ZwOpenDirectoryObject / ZwOpenSymbolicLinkObject / ZwQuerySymbolicLinkObject). Not measured (stop): other directories /
	// links / access masks, a target buffer that is too small.
	namespace
	{
		const char* const known_dll_sections[] = { "kernel32.dll", "ucrtbase.dll", "WS2_32.dll", "MSCTF.dll", "SHLWAPI.dll", "kernelbase.dll", "wow64.dll", "msvcp_win.dll",
												   "gdiplus.dll", "user32.dll", "bcrypt.dll", "COMCTL32.dll", "cfgmgr32.dll", "combase.dll", "IMM32.dll", "rpcrt4.dll",
												   "bcryptPrimitives.dll", "ntdll.dll", "coml2.dll", "win32u.dll", "wow64cpu.dll", "COMDLG32.dll", "gdi32full.dll", "IMAGEHLP.dll",
												   "SHELL32.dll", "sechost.dll", "WINTRUST.dll", "NORMALIZ.dll", "difxapi.dll", "Setupapi.dll", "CRYPT32.dll", "gdi32.dll",
												   "MSVCRT.dll", "advapi32.dll", "wow64win.dll", "PSAPI.DLL", "NSI.dll", "WLDAP32.dll", "OLEAUT32.dll", "SHCORE.dll", "ole32.dll",
												   "clbcatq.dll" };

		std::string lower( const std::string& s )
		{
			std::string o;
			for ( char ch : s ) o += char( ( ch >= 'A' && ch <= 'Z' ) ? ch - 'A' + 'a' : ch );
			return o;
		}
	}

	reply kernel::svc_open_namespace_object( const call& c )
	{
		object_attributes oa;
		std::string why;
		if ( !guest_object_attributes( c.args[ 2 ], oa, why ) ) return done_( status::access_violation, why );
		const uint32_t access = uint32_t( c.args[ 1 ] );
		std::string root_name;
		if ( oa.root )
		{
			const auto it = handles_.find( oa.root );
			if ( it == handles_.end() || it->second.kind != "directory" ) return stop_( c.name + ": RootDirectory " + hex( oa.root ) + " is not a directory handle of this process - not measured" );
			root_name = it->second.name;
		}
		const std::string full = root_name.empty() ? oa.name : root_name + "\\" + oa.name;
		const std::string what = c.name + "( access " + hex( access ) + ", \"" + full + "\", attributes " + hex( oa.attributes ) + " )";
		if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": the handle pointer is not writable" );
		object o;
		if ( c.name == "NtOpenDirectoryObject" )
		{
			// P41 (U2075): the session's named-object directory - MEASURED (tm_nt bno.*): "\Sessions\<the PEB SessionId>\BaseNamedObjects",
			// OBJ_CASE_INSENSITIVE, access FH (kernelbase's) or 3 = STATUS_SUCCESS + a handle (object type "Directory"); another session
			// number = STATUS_OBJECT_PATH_NOT_FOUND (C000003AH) with the handle written NULL
			uint32_t sid = 0;
			std::string err;
			e_.read( f_.peb + 0x2C0, &sid, 4, err );   // PEB SessionId (2C0H, ntdll.pdb _PEB)
			const std::string bno = "\\sessions\\" + std::to_string( sid ) + "\\basenamedobjects";
			const std::string lf = lower( full );
			if ( !oa.root && ( oa.attributes & 0x40 ) && ( access == 0xF || access == 3 ) && lf.rfind( "\\sessions\\", 0 ) == 0 && lf.size() > 27 &&
				 lf.substr( lf.size() - 17 ) == "\\basenamedobjects" && lf != bno )
			{
				const uint64_t zero = 0;
				reply rn = done_( status::object_path_not_found, what + ": another session's directory - STATUS_OBJECT_PATH_NOT_FOUND, handle NULL (measured: tm_nt bno.other)" );
				if ( !put( c.args[ 0 ], &zero, 8, rn ) ) return stop_( "writing the handle failed after the probe" );
				return rn;
			}
			if ( lf == bno && !oa.root && ( oa.attributes & 0x40 ) && ( access == 0xF || access == 3 ) )
			{
				o.kind = "directory";
				o.name = "\\Sessions\\" + std::to_string( sid ) + "\\BaseNamedObjects";
			}
			else
			{
				if ( lower( full ) != "\\knowndlls" || access != 3 ) return stop_( what + " - not measured (tm_nt kd.open: \"\\KnownDlls\", 3)" );
				o.kind = "directory";
				o.name = "\\KnownDlls";
			}
		}
		else if ( c.name == "NtOpenSymbolicLinkObject" )
		{
			if ( lower( full ) != "\\knowndlls\\knowndllpath" || access != 1 ) return stop_( what + " - not measured (tm_nt kd.link: \\KnownDlls\\KnownDllPath, 1)" );
			o.kind = "symlink";
			o.name = "\\KnownDlls\\KnownDllPath";
			o.target = f_.system_root + "\\System32";
		}
		else
			return stop_( what + " - not modelled" );
		const uint64_t h = open_handle( o );
		reply rr = done_( status::success, what + ": " + o.kind + " handle " + hex( h ) + " (measured: tm_nt kd)" );
		if ( !put( c.args[ 0 ], &h, 8, rr ) ) return stop_( "writing the handle failed after the probe" );
		return rr;
	}

	reply kernel::svc_query_symbolic_link( const call& c )
	{
		const auto it = handles_.find( c.args[ 0 ] );
		if ( it == handles_.end() || it->second.kind != "symlink" ) return stop_( "NtQuerySymbolicLinkObject: handle " + hex( c.args[ 0 ] ) + " is not a symbolic link of this process - not measured" );
		const uint64_t us = c.args[ 1 ], pret = c.args[ 2 ];
		if ( !user_buffer( us, 16, UC_PROT_READ | UC_PROT_WRITE ) ) return done_( status::access_violation, "NtQuerySymbolicLinkObject: LinkTarget " + hex( us ) + " not writable" );
		uint16_t hdr[ 2 ] = {};
		uint64_t buf = 0;
		std::string err;
		e_.read( us, hdr, 4, err );
		e_.read( us + 8, &buf, 8, err );
		std::vector<uint16_t> w;
		for ( char ch : it->second.target ) w.push_back( uint16_t( uint8_t( ch ) ) );
		const uint16_t need = uint16_t( w.size() * 2 );
		if ( hdr[ 1 ] < need + 2u ) return stop_( "NtQuerySymbolicLinkObject: MaximumLength " + hex( hdr[ 1 ] ) + " below the target + terminator - not measured" );
		if ( !user_buffer( buf, need, UC_PROT_WRITE ) || ( pret && !user_buffer( pret, 4, UC_PROT_WRITE ) ) ) return done_( status::access_violation, "NtQuerySymbolicLinkObject: Buffer / ReturnedLength not writable" );
		reply rr = done_( status::success, "NtQuerySymbolicLinkObject: \"" + it->second.name + "\" -> \"" + it->second.target + "\" (the environment's SystemRoot + \\System32; measured shape: tm_nt kd.target)" );
		const uint32_t rl = need + 2u;
		if ( !put( buf, w.data(), need, rr ) || !put( us, &need, 2, rr ) || ( pret && !put( pret, &rl, 4, rr ) ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtOpenSection( *SectionHandle, DesiredAccess, ObjectAttributes ) - phnt ntmmapi.h:1449-1453 (the WDK's ZwOpenSection). MEASURED
	// (tm_nt kd.section.*): access 0DH (SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_EXECUTE), root = the \KnownDlls handle,
	// OBJ_CASE_INSENSITIVE: a name of the directory ("KERNEL32.DLL", "kernelbase.dll") = STATUS_SUCCESS + a handle (non-zero, multiple
	// of 4); a name that is not there = STATUS_OBJECT_NAME_NOT_FOUND with *SectionHandle = NULL. The section's image is the capture's file of that
	// name (mapping it needs the file in the capture - else the map stops). Other roots / access masks: stop.
	reply kernel::svc_open_section( const call& c )
	{
		object_attributes oa;
		std::string why;
		if ( !guest_object_attributes( c.args[ 2 ], oa, why ) ) return done_( status::access_violation, why );
		const uint32_t access = uint32_t( c.args[ 1 ] );
		const auto it = oa.root ? handles_.find( oa.root ) : handles_.end();
		const std::string what = "NtOpenSection( access " + hex( access ) + ", root " + ( it != handles_.end() ? "\"" + it->second.name + "\"" : hex( oa.root ) ) + ", \"" + oa.name +
								 "\", attributes " + hex( oa.attributes ) + " )";
		// the CSR server's shared section of the session - MEASURED (tm_nt csr.open): "\Sessions\<the PEB SessionId>\Windows\SharedSection",
		// access 4 (SECTION_MAP_READ), OBJ_CASE_INSENSITIVE, no root = STATUS_SUCCESS + a handle (non-zero, multiple of 4); its view
		// (tm_nt csr.map: 100000H bytes, MEM_MAPPED, PAGE_READONLY) holds the CSR server's shared heap - P39 stopped at its map; P40 (U2065):
		// the view as a connected client sees it (map_csr_shared_view, measured tm_nt csrc.*)
		if ( !oa.root && access == 4 && ( oa.attributes & 0x40 ) )
		{
			uint32_t sid = 0;
			std::string err;
			e_.read( f_.peb + 0x2C0, &sid, 4, err );   // PEB SessionId (2C0H, ntdll.pdb _PEB)
			if ( lower( oa.name ) == "\\sessions\\" + std::to_string( sid ) + "\\windows\\sharedsection" )
			{
				if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": SectionHandle not writable" );
				object o;
				o.kind = "section (csr shared)";
				o.name = oa.name;
				const uint64_t h = open_handle( o );
				reply rr = done_( status::success, what + ": the CSR server's shared section of session " + std::to_string( sid ) + ", handle " + hex( h ) + " (measured: tm_nt csr.open)" );
				if ( !put( c.args[ 0 ], &h, 8, rr ) ) return stop_( "writing the handle failed after the probe" );
				return rr;
			}
		}
		if ( it == handles_.end() || it->second.kind != "directory" || it->second.name != "\\KnownDlls" || access != 0xD || !( oa.attributes & 0x40 ) )
			return stop_( what + " - not measured (tm_nt kd.section: root \\KnownDlls, 0DH, OBJ_CASE_INSENSITIVE)" );
		const std::string want = lower( oa.name );
		const char* hit = nullptr;
		for ( const char* n : known_dll_sections )
			if ( lower( n ) == want ) hit = n;
		if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": SectionHandle not writable" );
		if ( !hit )
		{
			reply nf = done_( status::object_name_not_found, what + ": no such entry in \\KnownDlls - STATUS_OBJECT_NAME_NOT_FOUND, *SectionHandle = NULL (measured: tm_nt kd.section.2)" );
			const uint64_t zero = 0;
			if ( !put( c.args[ 0 ], &zero, 8, nf ) ) return stop_( "writing the handle failed after the probe" );
			return nf;
		}
		object o;
		o.kind = "section (image)";
		o.name = "\\KnownDlls\\" + std::string( hit );
		o.target = want;   // the capture file name (lower case)
		const uint64_t h = open_handle( o );
		reply rr = done_( status::success, what + ": the KnownDlls section " + hit + ", handle " + hex( h ) + ( f_.files && f_.files->count( want ) ? " (its image is in the capture)" : " (its image is NOT in the capture - a map stops)" ) +
										" (measured: tm_nt kd.section)" );
		if ( !put( c.args[ 0 ], &h, 8, rr ) ) return stop_( "writing the handle failed after the probe" );
		return rr;
	}

	// NtMapViewOfSection( SectionHandle, ProcessHandle, *BaseAddress, ZeroBits, CommitSize, *SectionOffset, *ViewSize, InheritDisposition,
	// AllocationType, PageProtection ) - phnt ntmmapi.h:1475-1486 (the WDK's ZwMapViewOfSection documents the call). MEASURED (tm_nt
	// kd.map.*): the loader's arguments - a \KnownDlls section, NtCurrentProcess(), *BaseAddress 0, ZeroBits 0, CommitSize 0,
	// SectionOffset NULL, *ViewSize 0, ViewShare (1), MEM_DIFFERENT_IMAGE_BASE_OK (800000H), PAGE_EXECUTE_WRITECOPY (80H) - for a KnownDll
	// the process has not mapped yet = STATUS_SUCCESS, *BaseAddress = a 64 KiB-aligned address, *ViewSize = the image's SizeOfImage, the
	// view's header ImageBase = the view's base (the section's image is relocated to it), MEMORY_BASIC_INFORMATION at the base:
	// AllocationProtect 80H, MEM_COMMIT, PAGE_READONLY (the headers), MEM_IMAGE; a second view of an image the process already maps =
	// STATUS_IMAGE_AT_DIFFERENT_BASE (40000036H) - not modelled (stop). The emulated view: the capture's file of the section mapped
	// (emu::pe::map, relocated) at the environment's fixed slot of that DLL (emu::win::system_image_slot: kernel32 7FFB10000000H,
	// kernelbase 7FFB20000000H - the --HLE layout; U2105 (P43): every other measured \KnownDlls section at 7FFB30000000H + i * 10000000H;
	// Windows: the boot-time ASLR base), the header's ImageBase set to that base, registered as a MEM_IMAGE view. Other sections /
	// arguments / DLLs without a slot or capture file, an image larger than its slot, a slot range that is not free: stop.
	reply kernel::svc_map_view_of_section( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtMapViewOfSection: the stack arguments are not readable" );
		std::string a;
		for ( int i = 0; i < 10; i++ ) a += ( i ? ", " : "" ) + hex( c.args[ size_t( i ) ] );
		const auto it = handles_.find( c.args[ 0 ] );
		const std::string what = "NtMapViewOfSection( " + a + " ) section " + ( it != handles_.end() ? it->second.name : std::string( "?" ) );
		if ( it != handles_.end() && it->second.kind == "section (csr shared)" )
			// P39 stopped here (no layout source, csrss not measurable); P40 (U2065, USER DECISION 2026-10-26): the view as a CONNECTED
			// client sees it, measured from inside our own test PE (tm_nt csrc.*) - map_csr_shared_view below
			return map_csr_shared_view( c, what );
		// P44 (U2121): the view of a DATA section of a captured file - MEASURED tm_nt u32k.fl.map (NtCurrentProcess(), *BaseAddress 0, 0, 0, NULL,
		// *ViewSize 0, ViewShare, AllocationType 0, PAGE_READONLY): STATUS_SUCCESS, the base 64 KiB-aligned, ViewSize = the file size rounded
		// up to pages, the bytes = the file's, MBI {AllocationProtect PAGE_READONLY, MEM_COMMIT, PAGE_READONLY, MEM_MAPPED, RegionSize = ViewSize}
		if ( it != handles_.end() && it->second.kind == "section (data)" )
		{
			const uint64_t pb = c.args[ 2 ], ps = c.args[ 6 ];
			if ( c.args[ 1 ] != current_process || c.args[ 3 ] || c.args[ 4 ] || c.args[ 5 ] || uint32_t( c.args[ 7 ] ) != 1 || uint32_t( c.args[ 8 ] ) || uint32_t( c.args[ 9 ] ) != page::readonly )
				return stop_( what + " - other arguments: not measured (tm_nt u32k.fl.map)" );
			if ( !user_buffer( pb, 8, UC_PROT_READ | UC_PROT_WRITE ) || !user_buffer( ps, 8, UC_PROT_READ | UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": BaseAddress / ViewSize not writable" );
			uint64_t wb = 0, ws = 0;
			get64( pb, wb );
			get64( ps, ws );
			if ( wb || ws ) return stop_( what + " - *BaseAddress / *ViewSize not 0: not measured" );
			if ( !f_.files || !f_.files->count( it->second.target ) ) return stop_( what + " - the file is not in the capture" );
			const auto& bytes = f_.files->at( it->second.target );
			uint64_t base = 0;
			std::string err;
			if ( !map_data_view( bytes, "data view of " + it->second.target, base, err ) ) return stop_( what + " - " + err );
			const uint64_t vs = ( uint64_t( bytes.size() ) + f_.page_size - 1 ) & ~uint64_t( f_.page_size - 1 );
			reply rv = done_( status::success, what + ": a read-only view of the captured " + it->second.target + " at " + hex( base ) + ", ViewSize " + hex( vs ) + " (measured: tm_nt u32k.fl.map)" );
			if ( !put( pb, &base, 8, rv ) || !put( ps, &vs, 8, rv ) ) return stop_( "writing the result failed after the probe" );
			return rv;
		}
		if ( it == handles_.end() || it->second.kind != "section (image)" ) return stop_( what + " - not a KnownDlls image section of this process: not measured" );
		const uint64_t pbase = c.args[ 2 ], psize = c.args[ 6 ];
		if ( c.args[ 1 ] != current_process || c.args[ 3 ] || c.args[ 4 ] || c.args[ 5 ] || uint32_t( c.args[ 7 ] ) != 1 || uint32_t( c.args[ 8 ] ) != 0x800000 ||
			 uint32_t( c.args[ 9 ] ) != page::execute_writecopy )
			return stop_( what + " - other arguments: not measured (tm_nt kd.map)" );
		if ( !user_buffer( pbase, 8, UC_PROT_READ | UC_PROT_WRITE ) || !user_buffer( psize, 8, UC_PROT_READ | UC_PROT_WRITE ) )
			return done_( status::access_violation, what + ": BaseAddress / ViewSize not writable" );
		uint64_t want_base = 0, want_size = 0;
		get64( pbase, want_base );
		get64( psize, want_size );
		if ( want_base || want_size ) return stop_( what + " - *BaseAddress / *ViewSize not 0: not measured" );
		const std::string& file = it->second.target;
		// P43 (U2107): an image section of a captured System32 file that is NOT a KnownDll (the loader's path search) - MEASURED (tm_nt
		// sp.*.map, the same arguments as kd.map): STATUS_SUCCESS, the base 64 KiB-aligned, ViewSize = SizeOfImage, the header's ImageBase
		// = the base, MEMORY_BASIC_INFORMATION at the base AllocationProtect 80H, MEM_COMMIT, PAGE_READONLY, MEM_IMAGE (= kd.map). The
		// emulated base: the highest free 64 KiB-aligned range (kernel::find_free top-down - the model's choice; Windows: an ASLR base)
		const bool from_file = it->second.name.rfind( "\\KnownDlls\\", 0 ) != 0;
		uint64_t slot = file == "ntdll.dll" ? 0 : win::system_image_slot( file );   // U2105 (ntdll: the process' own, never a second view)
		if ( from_file && ( slot || file == "ntdll.dll" ) ) return stop_( what + " - a KnownDll / ntdll opened as a system-directory file: not measured" );
		if ( ( !from_file && !slot ) || !f_.files || !f_.files->count( file ) ) return stop_( what + " - " + file + " has no slot / is not in the environment's capture (capture_sysdlls.py --dll)" );
		for ( const auto& [ vb, v ] : views_ )
			if ( ( slot && v.base == slot ) || v.name == file ) return stop_( what + " - a second view of " + file + " (measured: STATUS_IMAGE_AT_DIFFERENT_BASE) - not modelled" );
		const std::vector<uint8_t>& bytes = f_.files->at( file );
		pe::image img;
		pe::mapping mp;
		std::string err;
		if ( !pe::parse( bytes, img, err ) ) return stop_( what + " - mapping the capture's " + file + " failed: " + err );
		// U2105: the image must fit its slot, and the slot range must be free (nothing mapped / reserved there)
		uint64_t free_at = 0;
		if ( !img.size_of_image || ( !from_file && img.size_of_image > win::system_image_slot_size ) )
			return stop_( what + " - " + file + " SizeOfImage " + hex( img.size_of_image ) + " does not fit its slot (" + hex( win::system_image_slot_size ) + ")" );
		if ( from_file )
		{
			if ( !find_free( img.size_of_image, true, slot, 0, 0, 0x10000 ) ) return stop_( what + " - no free 64 KiB-aligned range for " + file );
		}
		else if ( !find_free( img.size_of_image, false, free_at, slot, slot + ( img.size_of_image - 1 ), 0x10000 ) || free_at != slot )
			return stop_( what + " - the slot of " + file + " at " + hex( slot ) + " is not free" );
		if ( !pe::map( e_, bytes, img, slot, file, mp, err ) ) return stop_( what + " - mapping the capture's " + file + " failed: " + err );
		// the view's header ImageBase = the base (IMAGE_OPTIONAL_HEADER64.ImageBase at e_lfanew + 18H + 18H, um\winnt.h)
		if ( !e_.write( slot + img.e_lfanew + 0x18 + 0x18, &slot, 8, err ) ) return stop_( what + " - writing the header ImageBase failed: " + err );
		if ( !add_image_view( slot, mp.size, file, err ) ) return stop_( what + " - " + err );
		reply rr = done_( status::success, what + ": " + file + " (the capture's file) mapped at " + hex( slot ) + ", ViewSize " + hex( img.size_of_image ) +
											   ( mp.relocated ? ", relocated (" + std::to_string( mp.relocations ) + " fixups)" : "" ) + ", header ImageBase = the base (measured: tm_nt " + ( from_file ? "sp.map" : "kd.map" ) + ")" );
		const uint64_t vs = img.size_of_image;
		if ( !put( pbase, &slot, 8, rr ) || !put( psize, &vs, 8, rr ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtOpenFile( *FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions ) - phnt ntioapi.h:2294-2301 (the
	// WDK's ZwOpenFile documents the call). The emulated file system holds exactly what the environment presents: the process image's
	// directory (the CurrentDirectory of its parameters). MEASURED (tm_nt of.*): the loader's call - access 100020H, "\??\<current
	// directory>\" OBJ_CASE_INSENSITIVE, ShareAccess 3, OpenOptions 21H - = STATUS_SUCCESS, a handle (non-zero, multiple of 4), IoStatus
	// {STATUS_SUCCESS, Information 1 = FILE_OPENED}; NtClose = STATUS_SUCCESS. P45 (U2129, USER DECISION 2026-10-27 "virtual FS, measured
	// misses"): every other path / argument set is answered by the emulated file system (nt_kernel_vfs.cpp): a miss with the measured status
	// of its kind (tm_nt fs.* / of.missing), a path of the model with an operation that was not measured = a loud stop.
	reply kernel::svc_open_file( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtOpenFile: the stack arguments are not readable" );
		object_attributes oa;
		std::string why;
		if ( !guest_object_attributes( c.args[ 2 ], oa, why ) ) return done_( status::access_violation, why );
		const uint32_t access = uint32_t( c.args[ 1 ] ), share = uint32_t( c.args[ 4 ] ), options = uint32_t( c.args[ 5 ] );
		const uint64_t ph = c.args[ 0 ], iosb = c.args[ 3 ];
		const std::string what = "NtOpenFile( access " + hex( access ) + ", \"" + oa.name + "\", attributes " + hex( oa.attributes ) + ", share " + hex( share ) + ", options " +
								 hex( options ) + ( oa.root ? ", root " + hex( oa.root ) : std::string() ) + " )";
		std::string dir = lower( f_.image_dir );
		while ( !dir.empty() && dir.back() == '\\' ) dir.pop_back();
		std::string want = lower( oa.name );
		while ( !want.empty() && want.back() == '\\' ) want.pop_back();
		const bool curdir = !oa.root && !dir.empty() && want == "\\??\\" + dir;
		// P43 (U2107): a system DLL that is not a KnownDll - the loader opens "\??\<SystemRoot>\SYSTEM32\<name>" with access 100021H
		// (SYNCHRONIZE | FILE_EXECUTE | FILE_READ_DATA), OBJ_CASE_INSENSITIVE | OBJ_IGNORE_IMPERSONATED_DEVICEMAP (840H), ShareAccess 5,
		// OpenOptions 60H (FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE). MEASURED (tm_nt sp.*.open, d3d11 / dxgi / dcomp / dwmapi /
		// kernel.appcore): STATUS_SUCCESS, a handle (non-zero, multiple of 4), IoStatus {STATUS_SUCCESS, Information 1 = FILE_OPENED}. The
		// emulated file system holds System32's CAPTURED files (the environment's capture); other files / arguments: stop.
		std::string sysfile;
		if ( sysdir_file( oa, sysfile ) )
		{
			// P44 (U2121): access 100001H (SYNCHRONIZE | FILE_READ_DATA) - ntdll LdrpFindLoadedDllByMappingFile for GetModuleHandleW( a full path ),
			// user32 _InitializeImmEntryTable - MEASURED tm_nt u32k.fl.open: STATUS_SUCCESS, a handle, IoStatus {0, 1 FILE_OPENED} (as sp.open)
			if ( ( access != 0x100021 && access != 0x100001 ) || share != 5 || options != 0x60 || oa.attributes != 0x840 )
				return stop_( what + " - a captured system-directory file with other arguments: not measured (tm_nt sp.open: 100021H, 840H, 5, 60H)" );
			if ( !user_buffer( ph, 8, UC_PROT_WRITE ) || !user_buffer( iosb, 16, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": FileHandle / IoStatusBlock not writable" );
			object o;
			o.kind = "file (system image)";
			o.name = oa.name;
			o.target = sysfile;
			const uint64_t h = open_handle( o );
			reply rs = done_( status::success, what + ": the captured system-directory file " + sysfile + ", handle " + hex( h ) + ", IoStatus FILE_OPENED (measured: tm_nt sp.open)" );
			const uint64_t io[ 2 ] = { 0, 1 };
			if ( !put( ph, &h, 8, rs ) || !put( iosb, io, 16, rs ) ) return stop_( "writing the result failed after the probe" );
			return rs;
		}
		if ( !curdir || access != 0x100020 || share != 3 || options != 0x21 || oa.attributes != 0x40 ) return vfs_open_file( c, oa, what );   // P45 (U2129)
		if ( !user_buffer( ph, 8, UC_PROT_WRITE ) || !user_buffer( iosb, 16, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": FileHandle / IoStatusBlock not writable" );
		object o;
		o.kind = "file (directory)";
		o.name = f_.image_dir;
		const uint64_t h = open_handle( o );
		reply rr = done_( status::success, what + ": the image's directory (the current directory), handle " + hex( h ) + ", IoStatus FILE_OPENED (measured: tm_nt of.curdir)" );
		const uint64_t io[ 2 ] = { 0, 1 };
		if ( !put( ph, &h, 8, rr ) || !put( iosb, io, 16, rr ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtQueryVolumeInformationFile( FileHandle, IoStatusBlock, FsInformation, Length, FsInformationClass ) - phnt ntioapi.h:2709 (the WDK's
	// ZwQueryVolumeInformationFile documents the call and FILE_FS_DEVICE_INFORMATION {DEVICE_TYPE DeviceType, ULONG Characteristics}).
	// MEASURED (tm_nt of.fsdev): on the image's directory, class 4 FileFsDeviceInformation, Length 8 = STATUS_SUCCESS, IoStatus
	// {STATUS_SUCCESS, Information 8}, DeviceType 7 (FILE_DEVICE_DISK), Characteristics 20020H, exactly 8 bytes written. Other classes /
	// lengths / handles: stop. P40 (U2062): Characteristics belong to the VOLUME of the directory - the emulated image directory lies on
	// the system volume (the environment's SystemRoot drive), so the value is the one measured with the current directory on the guest's
	// system volume C: (20020H; tm_nt of.fsdev run by nt_measured.py from <SystemRoot>); P39 had measured 60020H from a worktree on E:,
	// another volume of the guest (seen by the coordinator's verify_merge in the main checkout on C:, P40).
	reply kernel::svc_query_volume_information_file( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtQueryVolumeInformationFile: the stack arguments are not readable" );
		const auto it = handles_.find( c.args[ 0 ] );
		const uint32_t len = uint32_t( c.args[ 3 ] ), cls = uint32_t( c.args[ 4 ] );
		const std::string what = "NtQueryVolumeInformationFile( " + hex( c.args[ 0 ] ) + ( it != handles_.end() ? " (" + it->second.kind + ")" : std::string() ) + ", class " +
								 std::to_string( cls ) + ", Length " + hex( len ) + " )";
		if ( it == handles_.end() || it->second.kind != "file (directory)" || cls != 4 || len != 8 ) return stop_( what + " - not measured (tm_nt of.fsdev: the image directory, class 4, 8)" );
		if ( !user_buffer( c.args[ 1 ], 16, UC_PROT_WRITE ) || !user_buffer( c.args[ 2 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": IoStatusBlock / FsInformation not writable" );
		reply rr = done_( status::success, what + ": FILE_FS_DEVICE_INFORMATION DeviceType 7 (FILE_DEVICE_DISK), Characteristics 20020H (the system volume; measured: tm_nt of.fsdev)" );
		const uint32_t d[ 2 ] = { 7, 0x20020 };
		const uint64_t io[ 2 ] = { 0, 8 };
		if ( !put( c.args[ 2 ], d, 8, rr ) || !put( c.args[ 1 ], io, 16, rr ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtSetInformationProcess( ProcessHandle, ProcessInformationClass, ProcessInformation, ProcessInformationLength ) - phnt
	// ntpsapi.h:2557-2562
	reply kernel::svc_set_information_process( const call& c )
	{
		// class 23H ProcessTlsInformation (phnt ntpsapi.h PROCESSINFOCLASS 35; PROCESS_TLS_INFORMATION :981-989, THREAD_TLS_INFORMATION
		// :960-966 - this build's thread entry is 18H bytes {ULONG Flags, the TLS vector pointer, the thread id}, after a 10H-byte header
		// {Flags, OperationType, ThreadDataCount, TlsIndex / PreviousCount}). MEASURED (tm_nt tls.*; a process with exactly one thread):
		// header {0, 1 ProcessTlsReplaceVector, 1, 0} + one entry {Flags 0, new vector}, Length 28H = STATUS_SUCCESS; the thread's TEB
		// ThreadLocalStoragePointer := the new vector; the entry becomes {Flags 2, the OLD vector, the thread id}; the header and the bytes
		// beyond 28H are not written; 0 entries with Length 10H = STATUS_INFO_LENGTH_MISMATCH. The emulated process has one thread. Other
		// classes / shapes: stop.
		const uint32_t cls = uint32_t( c.args[ 1 ] ), len = uint32_t( c.args[ 3 ] );
		const uint64_t in = c.args[ 2 ];
		std::string hx;
		uint8_t d[ 0x80 ] = {};
		if ( len && len <= 0x80 && user_buffer( in, len, UC_PROT_READ ) )
		{
			std::string err;
			e_.read( in, d, len, err );
			for ( uint32_t i = 0; i < len; i++ ) { char t[ 4 ]; std::snprintf( t, sizeof( t ), "%02X", d[ i ] ); hx += ( i ? " " : "" ) + std::string( t ); }
		}
		const std::string what = "NtSetInformationProcess( " + hex( c.args[ 0 ] ) + ", class " + hex( cls ) + ", " + hex( in ) + ", Length " + hex( len ) + " ) bytes: " + hx;
		if ( c.args[ 0 ] == current_process && cls == 0x31 )
		{
			// P41 (U2070): ProcessConsoleHostProcess (phnt ntpsapi.h:222, PROCESS_CONSOLE_HOST_PROCESS_INFORMATION :809-821, a ULONG_PTR).
			// kernelbase of a process started WITHOUT a console (ConsoleHandle RTL_USER_PROC_DETACHED_PROCESS) sets it to 1 (the emulated
			// trace, P41 run c1). MEASURED (tm_nt con.chp.* / con.chps.*, a detached process): the value reads 1 after its start (so
			// kernelbase's set succeeded - the default of a process is 0 - inferred); a further set of 1 with Length 8 = STATUS_SUCCESS;
			// Length 4 / 10H = STATUS_INFO_LENGTH_MISMATCH. Other values (a console host's process id): not measured - stop.
			if ( len != 8 ) return done_( status::info_length_mismatch, what + ": ProcessConsoleHostProcess needs Length 8 - STATUS_INFO_LENGTH_MISMATCH (measured: tm_nt con.chps.4 / .10)" );
			uint64_t v = 0;
			std::memcpy( &v, d, 8 );
			if ( v != 1 ) return stop_( what + " - ProcessConsoleHostProcess value " + hex( v ) + ": not measured (1 is: a process started without a console)" );
			console_host_set_ = true;
			console_host_ = v;
			return done_( status::success, what + ": ProcessConsoleHostProcess := 1 (a process without a console; measured: tm_nt con.chps.8 / con.chp.after)" );
		}
		if ( c.args[ 0 ] != current_process || cls != 0x23 ) return stop_( what + " - not measured" );
		uint32_t hdr[ 4 ];
		std::memcpy( hdr, d, 16 );
		if ( len == 0x10 && hdr[ 0 ] == 0 && hdr[ 1 ] == 1 && hdr[ 2 ] == 0 ) return done_( status::info_length_mismatch, what + ": no thread entry - STATUS_INFO_LENGTH_MISMATCH (measured: tm_nt tls.zero)" );
		if ( len != 0x28 || hdr[ 0 ] != 0 || hdr[ 1 ] > 1 || hdr[ 2 ] != 1 || ( hdr[ 1 ] == 1 && hdr[ 3 ] != 0 ) || d[ 0x10 ] || d[ 0x11 ] || d[ 0x12 ] || d[ 0x13 ] )
			return stop_( what + " - another ProcessTlsInformation shape: not measured (tm_nt tls.*: {0, 1 or 0, 1, index} + one entry, 28H)" );
		if ( !user_buffer( in, 0x28, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": ProcessInformation not writable" );
		uint64_t vnew = 0, vold = 0;
		std::memcpy( &vnew, d + 0x18, 8 );
		if ( hdr[ 1 ] == 0 )
		{
			// ProcessTlsReplaceIndex - MEASURED (tm_nt tls.idx*): the thread's TLS vector slot [TlsIndex] := the new value; the entry becomes
			// {Flags 2, the OLD slot value}; the +20H field (the thread id under ReplaceVector) is NOT written; STATUS_SUCCESS
			uint64_t vec = 0, slot_old = 0;
			if ( !get64( f_.teb + 0x58, vec ) ) return stop_( what + " - the TEB is not readable" );
			const uint64_t slot = vec + 8ull * hdr[ 3 ];
			if ( !vec || hdr[ 3 ] > 0x1000 || !user_buffer( slot, 8, UC_PROT_READ | UC_PROT_WRITE ) ) return stop_( what + " - the thread's TLS vector slot " + std::to_string( hdr[ 3 ] ) + " is not accessible: not measured" );
			get64( slot, slot_old );
			reply ri = done_( status::success, what + ": ProcessTlsReplaceIndex - TLS vector " + hex( vec ) + " slot " + std::to_string( hdr[ 3 ] ) + " " + hex( slot_old ) + " -> " + hex( vnew ) +
											", the entry {Flags 2, the old value} (measured: tm_nt tls.idx*)" );
			const uint32_t flags = 2;
			if ( !put( slot, &vnew, 8, ri ) || !put( in + 0x10, &flags, 4, ri ) || !put( in + 0x18, &slot_old, 8, ri ) ) return stop_( "writing the result failed after the probe" );
			return ri;
		}
		if ( !get64( f_.teb + 0x58, vold ) ) return stop_( what + " - the TEB is not readable" );
		reply rr = done_( status::success, what + ": ProcessTlsReplaceVector - the thread's TEB ThreadLocalStoragePointer " + hex( vold ) + " -> " + hex( vnew ) +
										", the entry {Flags 2, the old vector, thread id " + hex( f_.thread_id ) + "} (measured: tm_nt tls.set / tls.swap)" );
		const uint32_t flags = 2;
		if ( !put( f_.teb + 0x58, &vnew, 8, rr ) || !put( in + 0x10, &flags, 4, rr ) || !put( in + 0x18, &vold, 8, rr ) || !put( in + 0x20, &f_.thread_id, 8, rr ) )
			return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtApphelpCacheControl( ServiceClass, ServiceContext ) - phnt ntmisc.h:257-262 (classes :82-97, AHC_SERVICE_DATA :234-246)
	reply kernel::svc_apphelp_cache_control( const call& c )
	{
		// class 6 ApphelpCacheServiceLookupCdb with the loader's context layout (178H bytes: LookupCdb.Name at +E8H, ParamsOut at +168H,
		// ParamsOutSize 4 at +170H, the rest 0). MEASURED (tm_nt ahc.*): for "KERNELBASE.dll", "KERNEL32.DLL", "kernel32.dll", "ntdll.dll"
		// and a name no file has = STATUS_SUCCESS, the 4 output bytes = 0, no context byte changed (no compatibility-database entry for these
		// modules in the guest); P41 (U2080): the same for the test PEs' own image names "t2_import.exe", "t3_tls.exe", "t3_tls.vmware_guest.exe"
		// (tm_nt ahc.5 / .6 / .7) and "t5_stdout.exe" (ahc.8). Other names (an entry may exist for them in the environment's database): stop until measured; other classes
		// / layouts: stop.
		const uint32_t cls = uint32_t( c.args[ 0 ] );
		const uint64_t ctx = c.args[ 1 ];
		std::string hx, nm, w2;
		uint8_t d[ 0x178 ] = {};
		if ( ctx && user_buffer( ctx, 0x178, UC_PROT_READ ) )
		{
			std::string err;
			e_.read( ctx, d, 0x178, err );
			for ( int i = 0; i < 0x178; i++ ) if ( d[ i ] ) { char t[ 16 ]; std::snprintf( t, sizeof( t ), " +%02X=%02X", i, d[ i ] ); hx += t; }
			guest_ustring( ctx + 0xE8, nm, w2 );
		}
		const std::string what = "NtApphelpCacheControl( class " + std::to_string( cls ) + ", " + hex( ctx ) + " ) LookupCdb.Name \"" + nm + "\"";
		if ( cls != 6 || !ctx ) return stop_( what + " - not measured (class 6 is)" );
		bool layout = true;
		for ( int i = 0; i < 0x178; i++ )
			if ( d[ i ] && !( i >= 0xE8 && i < 0xF8 ) && !( i >= 0x168 && i < 0x170 ) && i != 0x170 ) layout = false;
		uint64_t pout = 0;
		std::memcpy( &pout, d + 0x168, 8 );
		if ( !layout || d[ 0x170 ] != 4 ) return stop_( what + " context bytes:" + hx + " - another layout: not measured" );
		const std::string ln = lower( nm );
		// P43 (U2108): the 24 names the loader asks after the sample's import processing (sample run 8, exploration x2 system calls 449-472;
		// tm_nt ahc.9 .. ahc.32, the loader's spellings): STATUS_SUCCESS, no context byte changed, ParamsOut 80H for msvcrt.dll, OLEAUT32.dll,
		// dwmapi.dll and SETUPAPI.dll (ahc.15 / .23 / .25 / .31 - the value the guest's database lookup returns; its meaning is not documented
		// and not interpreted here), 0 for the others (incl. the sample's image name, ahc.32)
		static const char* const measured0[] = { "kernelbase.dll", "kernel32.dll", "ntdll.dll", "t2_import.exe", "t3_tls.exe", "t3_tls.vmware_guest.exe", "t5_stdout.exe",
												  "win32u.dll", "ucrtbase.dll", "msvcp_win.dll", "gdi32full.dll", "gdi32.dll", "user32.dll", "dxgi.dll", "d3d11.dll", "rpcrt4.dll",
												  "combase.dll", "kernel.appcore.dll", "dcomp.dll", "ole32.dll", "shell32.dll", "ws2_32.dll", "bcrypt.dll", "sechost.dll",
												  "advapi32.dll", "cfgmgr32.dll", "transparent_makimav2.exe",
												  "t6_user32.exe", "imm32.dll",   // P44 (U2121): ahc.33 test PE T6's image name, ahc.34 IMM32.DLL (user32 loads it)
												  "t7_vfs.exe" };   // P45 (U2129): ahc.35 test PE T7's image name
		static const char* const measured80[] = { "msvcrt.dll", "oleaut32.dll", "dwmapi.dll", "setupapi.dll" };
		uint32_t outv = 0xFFFFFFFF;
		for ( const char* m : measured0 ) if ( ln == m ) outv = 0;
		for ( const char* m : measured80 ) if ( ln == m ) outv = 0x80;
		if ( outv == 0xFFFFFFFF ) return stop_( what + " - this module name is not measured (tm_nt ahc.0 .. ahc.35)" );
		if ( !user_buffer( pout, 4, UC_PROT_WRITE ) ) return stop_( what + " - ParamsOut not writable: not measured" );
		reply rr = done_( status::success, what + ": ParamsOut = " + hex( outv ) + ( outv ? " (as the guest's lookup answers this name)" : " (no compatibility-database entry)" ) + " (measured: tm_nt ahc)" );
		if ( !put( pout, &outv, 4, rr ) ) return stop_( "writing ParamsOut failed after the probe" );
		return rr;
	}

	// NtSetInformationVirtualMemory( ProcessHandle, VmInformationClass, NumberOfEntries, VirtualAddresses (MEMORY_RANGE_ENTRY[]),
	// VmInformation, VmInformationLength ) - phnt ntmmapi.h:1186-1193 (classes :1034-1045, MEMORY_RANGE_ENTRY :1161-1165)
	reply kernel::svc_set_information_virtual_memory( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtSetInformationVirtualMemory: the stack arguments are not readable" );
		const uint32_t cls = uint32_t( c.args[ 1 ] ), ilen = uint32_t( c.args[ 5 ] );
		const uint64_t n = c.args[ 2 ], ranges = c.args[ 3 ], info = c.args[ 4 ];
		std::string what = "NtSetInformationVirtualMemory( " + hex( c.args[ 0 ] ) + ", class " + std::to_string( cls ) + ", " + std::to_string( n ) + " range(s):";
		for ( uint64_t i = 0; i < n && i < 8; i++ )
		{
			uint64_t va = 0, sz = 0;
			get64( ranges + 16 * i, va );
			get64( ranges + 16 * i + 8, sz );
			const view* v = view_of( va );
			what += " [" + hex( va ) + ", +" + hex( sz ) + ")" + ( v ? " (" + v->name + ( va == v->base ? " base" : "" ) + ( sz == v->size ? ", whole view" : "" ) + ")" : std::string() );
		}
		uint64_t val = 0;
		if ( ilen && ilen <= 8 && user_buffer( info, ilen, UC_PROT_READ ) )
		{
			std::string err;
			e_.read( info, &val, ilen, err );
		}
		what += ", value " + hex( val ) + " (" + hex( ilen ) + " bytes) )";
		// MEASURED (tm_nt vmi.*): class 4 VmImageHotPatchInformation, one range {an image view's base, 1000H}, value 1 (4 bytes) =
		// STATUS_NOT_SUPPORTED (C00000BBH) - for a fresh view, again, and for an image the loader processed (hot patching is not enabled in
		// the guest). Other classes / shapes: stop.
		uint64_t va0 = 0, sz0 = 0;
		get64( ranges, va0 );
		get64( ranges + 8, sz0 );
		const view* v0 = view_of( va0 );
		if ( c.args[ 0 ] == current_process && cls == 4 && n == 1 && ilen == 4 && val == 1 && sz0 == 0x1000 && v0 && v0->type == mem2::image && va0 == v0->base )
			return done_( status::not_supported, what + ": VmImageHotPatchInformation - STATUS_NOT_SUPPORTED (measured: tm_nt vmi)" );
		return stop_( what + " - not measured" );
	}

	// P43 (U2107): "\??\<SystemRoot>\SYSTEM32\<name>" (case-insensitive, no root, no further backslash) with <name> a file of the
	// environment's capture (its manifest's file_ lines)
	bool kernel::sysdir_file( const object_attributes& oa, std::string& file ) const
	{
		const std::string n = lower( oa.name ), pre = "\\??\\" + lower( f_.system_root ) + "\\system32\\";
		if ( oa.root || n.size() <= pre.size() || n.compare( 0, pre.size(), pre ) != 0 ) return false;
		file = n.substr( pre.size() );
		return file.find( '\\' ) == std::string::npos && f_.files && f_.files->count( file );
	}

	// NtQueryAttributesFile( ObjectAttributes, FileInformation ) - phnt ntioapi.h NtQueryAttributesFile; FILE_BASIC_INFORMATION {CreationTime,
	// LastAccessTime, LastWriteTime, ChangeTime, FileAttributes} (WDK wdm.h; 28H bytes). P43 (U2107): the loader's DLL path search for a
	// system DLL that is not a KnownDll (sample run 8: d3d11, dxgi, dcomp, dwmapi, kernel.appcore). MEASURED (tm_nt sp.*), attributes 840H
	// (OBJ_CASE_INSENSITIVE | OBJ_IGNORE_IMPERSONATED_DEVICEMAP): a name in the image's directory that it does not hold =
	// STATUS_OBJECT_NAME_NOT_FOUND with NOTHING written when the image's directory lies on the system volume (P44, U2111: the sample's
	// directory does; a directory on another volume of this guest - E: - writes bytes 0..23H zero: the P43 recording, made from E:);
	// "\??\<SystemRoot>\SYSTEM32\<name>" of an existing
	// file = STATUS_SUCCESS, all 28H bytes: the four times non-zero, FileAttributes 20H (FILE_ATTRIBUTE_ARCHIVE), bytes 24H..27H 0. The
	// emulated image directory holds only the image; System32 holds the capture's files; the times are a placeholder (non-zero: the
	// model's boot time, emu::vclock - the capture records no file times). Other paths / attributes / the image's own name: stop.
	reply kernel::svc_query_attributes_file( const call& c )
	{
		object_attributes oa;
		std::string why;
		if ( !guest_object_attributes( c.args[ 0 ], oa, why ) ) return done_( status::access_violation, why );
		const uint64_t out = c.args[ 1 ];
		const std::string what = "NtQueryAttributesFile( \"" + oa.name + "\", attributes " + hex( oa.attributes ) + ( oa.root ? ", root " + hex( oa.root ) : std::string() ) +
								 ", FileInformation " + hex( out ) + " )";
		std::string file;
		if ( sysdir_file( oa, file ) )
		{
			if ( oa.attributes != 0x840 ) return stop_( what + " - a captured system-directory file with other attributes: not measured (tm_nt sp.sys: 840H)" );
			if ( !user_buffer( out, 0x28, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": FileInformation not writable" );
			uint8_t b[ 0x28 ] = {};
			for ( int k = 0; k < 4; k++ ) std::memcpy( b + 8 * k, &vclock::system_time_at_interrupt_zero, 8 );
			b[ 0x20 ] = 0x20;
			reply rs = done_( status::success, what + ": the captured system-directory file " + file + " - FileAttributes 20H, times = the placeholder 2026-01-01T00:00:00Z (measured shape: tm_nt sp.sys)" );
			if ( !put( out, b, 0x28, rs ) ) return stop_( "writing the result failed after the probe" );
			return rs;
		}
		// P45 (U2129): every other path - the emulated file system (nt_kernel_vfs.cpp): a name missing in the image's directory with 840H is
		// tm_nt fs.dir.qa2 (= sp.app, P44 U2111: the image's directory lies on the system volume - nothing written)
		return vfs_query_attributes( c, oa, false, what );
	}

	// NtCreateSection( *SectionHandle, DesiredAccess, ObjectAttributes, MaximumSize, SectionPageProtection, AllocationAttributes, FileHandle )
	// - phnt ntmmapi.h:1391-1399 (the WDK's ZwCreateSection)
	reply kernel::svc_create_section( const call& c )
	{
		// MEASURED (tm_nt sec.*): a page-file-backed section - access F001FH (SECTION_ALL_ACCESS), ObjectAttributes NULL, *MaximumSize 10000H
		// (the call's) / 20000H, PAGE_READWRITE, SEC_COMMIT (8000000H), FileHandle NULL = STATUS_SUCCESS, a handle (non-zero, multiple of
		// 4), *MaximumSize unchanged; a view of it is zero-filled MEM_MAPPED memory (tm_nt sec.*: ViewUnmap, PAGE_READWRITE, ViewSize = the
		// size, 64 KiB-aligned). Other arguments: stop.
		if ( !c.stack_args ) return stop_( "NtCreateSection: the stack arguments are not readable" );
		uint64_t ms = 0;
		if ( c.args[ 3 ] && ( !user_buffer( c.args[ 3 ], 8, UC_PROT_READ ) || !get64( c.args[ 3 ], ms ) ) ) return done_( status::access_violation, "NtCreateSection: MaximumSize not readable" );
		const std::string what = "NtCreateSection( " + hex( c.args[ 0 ] ) + ", access " + hex( uint32_t( c.args[ 1 ] ) ) + ", OA " + hex( c.args[ 2 ] ) + ", MaximumSize " + hex( ms ) +
								 ", protection " + hex( uint32_t( c.args[ 4 ] ) ) + ", attributes " + hex( uint32_t( c.args[ 5 ] ) ) + ", FileHandle " + hex( c.args[ 6 ] ) + " )";
		// P43 (U2107): the image section of a captured System32 file the loader opened (tm_nt sp.*.sec): access 0DH, ObjectAttributes NULL,
		// MaximumSize NULL, PAGE_EXECUTE (10H), SEC_IMAGE (1000000H), the file handle = STATUS_SUCCESS, a handle (non-zero, multiple of 4).
		// Its view: NtMapViewOfSection below (tm_nt sp.*.map).
		if ( c.args[ 6 ] )
		{
			const auto fh = handles_.find( c.args[ 6 ] );
			if ( fh == handles_.end() || fh->second.kind != "file (system image)" ) return stop_( what + " - FileHandle is not a captured system-directory file of this process: not measured" );
			// P44 (U2121): a DATA section of the file - access 4 SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT (ntdll LdrpFindLoadedDllByMappingFile)
			// - MEASURED tm_nt u32k.fl.sec: STATUS_SUCCESS, a handle; its view below (u32k.fl.map)
			if ( uint32_t( c.args[ 1 ] ) == 4 && !c.args[ 2 ] && !c.args[ 3 ] && uint32_t( c.args[ 4 ] ) == page::readonly && uint32_t( c.args[ 5 ] ) == 0x8000000 )
			{
				if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": SectionHandle not writable" );
				object o;
				o.kind = "section (data)";
				o.name = fh->second.name;
				o.target = fh->second.target;
				const uint64_t h = open_handle( o );
				reply rs = done_( status::success, what + ": a data section of the captured " + o.target + ", handle " + hex( h ) + " (measured: tm_nt u32k.fl.sec)" );
				if ( !put( c.args[ 0 ], &h, 8, rs ) ) return stop_( "writing the handle failed after the probe" );
				return rs;
			}
			if ( uint32_t( c.args[ 1 ] ) != 0xD || c.args[ 2 ] || c.args[ 3 ] || uint32_t( c.args[ 4 ] ) != 0x10 || uint32_t( c.args[ 5 ] ) != 0x1000000 )
				return stop_( what + " - other arguments: not measured (tm_nt sp.sec: 0DH, NULL, NULL, PAGE_EXECUTE, SEC_IMAGE)" );
			if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": SectionHandle not writable" );
			object o;
			o.kind = "section (image)";
			o.name = fh->second.name;   // the file's path (a KnownDlls section's name is "\KnownDlls\<name>")
			o.target = fh->second.target;
			const uint64_t h = open_handle( o );
			reply rs = done_( status::success, what + ": an image section of the captured " + o.target + ", handle " + hex( h ) + " (measured: tm_nt sp.sec)" );
			if ( !put( c.args[ 0 ], &h, 8, rs ) ) return stop_( "writing the handle failed after the probe" );
			return rs;
		}
		if ( !c.args[ 3 ] || ( ms != 0x10000 && ms != 0x20000 ) || uint32_t( c.args[ 4 ] ) != page::readwrite || uint32_t( c.args[ 5 ] ) != 0x8000000 || c.args[ 6 ] )
			return stop_( what + " - not measured (tm_nt sec: a page-file section of 10000H / 20000H, PAGE_READWRITE, SEC_COMMIT)" );
		reply r = create_measured_object( c, c.args[ 0 ], uint32_t( c.args[ 1 ] ), c.args[ 2 ], "section (pagefile)", ms, { 0xF001Fu }, "tm_nt sec" );
		if ( r.kind == reply_kind::done ) r.text = what + ": " + r.text;
		return r;
	}

	// ------------------------------------------------------------------------------------------------ P40: a CONNECTED CSR client
	// USER DECISION 2026-10-26 (plan 3.M5): the CSR frontier of P39 is passed by MEASURING WHAT A CONNECTED PROCESS SEES - our own test PE
	// tm_nt (case csrc) reads only its own memory after its own loader connected it to the session's CSR server (its PEB, the read-only
	// view of the shared section, its ntdll's data, its handle table), never a message to csrss. This is the MODEL OF A CONNECTED CSR
	// CLIENT, not a CSR server: the emulated process receives exactly what such a client sees and what its ntdll / kernelbase read
	// (the emulated trace of the real ntdll / kernelbase, P40 runs e1-e4); nothing else of csrss' shared heap is modelled (zero) and
	// nothing is forwarded to the host. Layout names: ReactOS 0.4.16 (reference only) sdk/include/reactos/subsys/csr/csrmsg.h:47-58
	// CSR_API_CONNECTINFO and sdk/include/reactos/subsys/win/base.h:119-143 BASE_STATIC_SERVER_DATA (NT 5.2: WindowsDirectory,
	// WindowsSystemDirectory, NamedObjectDirectory as the first three UNICODE_STRINGs - the measured Windows 10 x64 shape keeps those
	// three at +0 / +10H / +20H; the rest differs and is not used).
	namespace
	{
		// placeholders of the measured shapes (tm_nt csrc.*; the real values are machine / boot state and never printed)
		constexpr uint64_t csr_server_base = 0x00007DF500000000ull;     // the server's view base: 64K-aligned, not the client's base (csrc.peb)
		constexpr uint64_t csr_client_remote = 0x00007DF600000000ull;   // the client port view's ViewRemoteBase: delta non-zero (csrc.ntdll)
		constexpr uint64_t csr_server_pid = 0x248;                      // the server process id: non-zero, a multiple of 4 (csrc.ntdll)
		// measured offsets / sizes of the shared section's view (csr.map, csrc.peb, csrc.array, csrc.bssd, csrc.str*, csrc.map5)
		constexpr uint64_t csr_view_size = 0x100000, csr_view_committed = 0x5000;
		constexpr uint64_t csr_array = 0x750, csr_bssd = 0x790, csr_bssd_self = 0x9E8;
		constexpr uint64_t csr_str_at[ 3 ] = { 0xA10, 0xA30, 0xA60 }, csr_str_room[ 3 ] = { 0x20, 0x30, 0x40 };
		constexpr uint64_t csr_port_view_size = 0x10000;
	}

	// NtConnectPort( *PortHandle, PortName, SecurityQos, ClientView (PORT_VIEW), ServerView (REMOTE_PORT_VIEW), MaxMessageLength,
	// ConnectionInformation, ConnectionInformationLength ) - phnt ntlpcapi.h:263-272 (PORT_VIEW :115-123, REMOTE_PORT_VIEW :125-130).
	// The caller (P40 trace e1 / e2, system call 98): ntdll's CsrClientConnectToServer for kernelbase - port "\Sessions\<the PEB
	// SessionId>\Windows\ApiPort", SECURITY_QUALITY_OF_SERVICE {ImpersonationLevel 2, ContextTrackingMode 1, EffectiveOnly 1} (Length is
	// not initialized by the caller), ClientView {30H, the page-file section of 10000H it just created, 0, 10000H, 0, 0}, ServerView {18H,
	// 0, 0}, ConnectionInformation 30H bytes with +20H = 1 (ReactOS csrmsg.h:83-88 CSR_CLIENT_CONNECT ServerId BASESRV = 1). What ntdll
	// READS of the answer (trace e2 #1190597-#1190653): +24H (ULONG, compared signed against 0: an NTSTATUS, handed back to kernelbase as
	// the connection's status), +0 (-> PEB CsrServerReadOnlySharedMemoryBase 380H: the server's view base), +8 (-> PEB
	// ReadOnlyStaticServerData 98H = +8 - +0 + the client's base: a server pointer), +10H (-> ntdll+16CC30H = CsrGetProcessId(): the server
	// process id), ClientView ViewRemoteBase - ViewBase (-> ntdll+16CC40H), the port handle (-> ntdll+16CC48H). MEASURED as a connected
	// client sees them (tm_nt csrc.peb / csrc.ntdll): the server base 64K-aligned and not the client's; the server pid non-zero, a multiple
	// of 4, = CsrGetProcessId(); the remote delta non-zero; the port handle a handle of type "ALPC Port". The model answers with
	// placeholders of those shapes and +24H = STATUS_SUCCESS (0: the only status a connected process can have seen - ntdll fails the
	// connection on a negative one; inferred, not observable from inside). The client view: a zero-filled MEM_MAPPED read-write view of
	// the page-file section (measured: tm_nt sec.*), 64K-aligned. Not written (no connected process can observe them, ntdll never reads
	// them): MaxMessageLength, ServerView, the other ConnectionInformation bytes. Other ports / shapes and a second connection: stop.
	reply kernel::svc_connect_port( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtConnectPort: the stack arguments are not readable" );
		std::string port, why;
		if ( !guest_ustring( c.args[ 1 ], port, why ) ) return done_( status::access_violation, "NtConnectPort: PortName: " + why );
		uint32_t sid = 0, ilen = 0;
		std::string err;
		e_.read( f_.peb + 0x2C0, &sid, 4, err );   // PEB SessionId (2C0H, ntdll.pdb _PEB)
		const uint64_t ph = c.args[ 0 ], qos = c.args[ 2 ], cv = c.args[ 3 ], sv = c.args[ 4 ], pmml = c.args[ 5 ], ci = c.args[ 6 ], pcil = c.args[ 7 ];
		if ( pcil && user_buffer( pcil, 4, UC_PROT_READ ) ) e_.read( pcil, &ilen, 4, err );
		uint64_t section = 0;
		if ( cv && user_buffer( cv, 0x30, UC_PROT_READ ) ) get64( cv + 8, section );
		const auto it = handles_.find( section );
		const std::string what = "NtConnectPort( port \"" + port + "\", ClientView section " + hex( section ) +
								 ( it != handles_.end() ? " (" + it->second.kind + ", " + hex( it->second.value ) + " bytes)" : std::string() ) + ", ConnectionInformation " + hex( ci ) + " (" +
								 hex( ilen ) + " bytes) )";
		if ( lower( port ) != "\\sessions\\" + std::to_string( sid ) + "\\windows\\apiport" ) return stop_( what + " - not the session's CSR API port: not measured" );
		if ( csr_port_ ) return stop_( what + " - a second connection of the process to the CSR API port: not measured" );
		if ( !cv || !sv || !ci || !pcil ) return stop_( what + " - without a ClientView / ServerView / ConnectionInformation: not measured" );
		if ( !user_buffer( ph, 8, UC_PROT_WRITE ) || !user_buffer( qos, 12, UC_PROT_READ ) || !user_buffer( cv, 0x30, UC_PROT_READ | UC_PROT_WRITE ) ||
			 !user_buffer( sv, 0x18, UC_PROT_READ ) || ( pmml && !user_buffer( pmml, 4, UC_PROT_WRITE ) ) || !user_buffer( ci, 0x30, UC_PROT_READ | UC_PROT_WRITE ) ||
			 !user_buffer( pcil, 4, UC_PROT_READ ) )
			return done_( status::access_violation, what + ": an argument is not accessible user memory (probe)" );
		uint8_t q[ 12 ] = {}, v[ 0x30 ] = {}, s[ 0x18 ] = {}, inf[ 0x30 ] = {};
		e_.read( qos, q, 12, err );
		e_.read( cv, v, 0x30, err );
		e_.read( sv, s, 0x18, err );
		e_.read( ci, inf, 0x30, err );
		uint32_t vlen = 0, voff = 0, slen = 0, server_id = 0;
		uint64_t vsize = 0, vbase = 0, vremote = 0, ssize = 0, sbase = 0;
		std::memcpy( &vlen, v, 4 );
		std::memcpy( &voff, v + 0x10, 4 );
		std::memcpy( &vsize, v + 0x18, 8 );
		std::memcpy( &vbase, v + 0x20, 8 );
		std::memcpy( &vremote, v + 0x28, 8 );
		std::memcpy( &slen, s, 4 );
		std::memcpy( &ssize, s + 8, 8 );
		std::memcpy( &sbase, s + 0x10, 8 );
		std::memcpy( &server_id, inf + 0x20, 4 );
		if ( q[ 4 ] != 2 || q[ 8 ] != 1 || q[ 9 ] != 1 || vlen != 0x30 || it == handles_.end() || it->second.kind != "section (pagefile)" || it->second.value != csr_port_view_size ||
			 voff || vsize != csr_port_view_size || vbase || vremote || slen != 0x18 || ssize || sbase || ilen != 0x30 || server_id != 1 )
			return stop_( what + " - another shape (SecurityQos / ClientView / ServerView / ConnectionInformation): not measured (P40 trace e1: QoS {2, 1, 1}, ClientView {30H, a page-file "
								 "section of 10000H, 0, 10000H, 0, 0}, ServerView {18H, 0, 0}, 30H bytes with ServerId 1 at +20H)" );
		uint64_t cview = 0;
		if ( !find_free( csr_port_view_size, true, cview ) ) return stop_( what + " - no free range for the client view" );
		if ( !e_.map( cview, csr_port_view_size, UC_PROT_READ | UC_PROT_WRITE, "CSR API port client view (page-file section, zero-filled)", err ) ) return stop_( what + " - " + err );
		struct view vw;
		vw.base = cview;
		vw.size = csr_port_view_size;
		vw.type = mem2::mapped;
		vw.allocation_protect = page::readwrite;
		vw.pages.assign( size_t( csr_port_view_size / f_.page_size ), page::readwrite );
		vw.name = "CSR API port client view";
		views_[ cview ] = vw;
		object o;
		o.kind = "port (alpc)";
		o.name = port;
		const uint64_t h = open_handle( o );
		csr_port_ = h;
		reply rr = done_( status::success, what + ": the model of a CONNECTED CSR client (P40, measured shapes tm_nt csrc.*) - port handle " + hex( h ) + " (ALPC Port), client view " + hex( cview ) +
											   " (10000H, MEM_MAPPED read-write, zero), ViewRemoteBase " + hex( csr_client_remote ) + "; ConnectionInformation +0 server view base " +
											   hex( csr_server_base ) + ", +8 the server-data array " + hex( csr_server_base + csr_array ) + ", +10H server process id " + hex( csr_server_pid ) +
											   ", +24H STATUS_SUCCESS (placeholders of the measured shapes; MaxMessageLength / ServerView not written: not observable)" );
		const uint64_t sa = csr_server_base + csr_array;
		const uint32_t ok = 0;
		if ( !put( ph, &h, 8, rr ) || !put( cv + 0x20, &cview, 8, rr ) || !put( cv + 0x28, &csr_client_remote, 8, rr ) || !put( ci, &csr_server_base, 8, rr ) || !put( ci + 8, &sa, 8, rr ) ||
			 !put( ci + 0x10, &csr_server_pid, 8, rr ) || !put( ci + 0x24, &ok, 4, rr ) )
			return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtMapViewOfSection of the session's CSR shared section (\Sessions\<id>\Windows\SharedSection, opened by svc_open_section). The caller
	// (P40 trace e1, system call 100): NtCurrentProcess(), *BaseAddress 0, ZeroBits 0, CommitSize 0, SectionOffset NULL, *ViewSize 0,
	// ViewUnmap (2), AllocationType 500000H (the low 32 bits; P39 tm_nt csr.map used 0), PAGE_READONLY. MEASURED (tm_nt csr.map,
	// csrc.peb, csrc.map5): STATUS_SUCCESS, a 64K-aligned base (top-down: a second view lies below the first), *ViewSize 100000H,
	// MEMORY_BASIC_INFORMATION at the base {AllocationBase = base, AllocationProtect PAGE_READONLY, MEM_COMMIT, PAGE_READONLY, MEM_MAPPED,
	// RegionSize 5000H} - the rest of the view reserved. The CONTENTS a connected client's loader reads (trace e3 / e4, kernelbase
	// +48904H .. +48A74H: array[1], BASE_STATIC_SERVER_DATA +0 / +10H / +38H / +9E8H; the strings it converts) at the measured offsets:
	// the server-data array at +750H ([1] = the server's +790H, [0], [2..6] NULL - csrc.array), BASE_STATIC_SERVER_DATA at +790H with
	// +9E8H = its own server address (csrc.bssd: kernelbase converts the string buffers by + array[1] - [+9E8H] - server base + client
	// base), +30H .. +3FH zero (csrc.words; kernelbase reads the USHORT at +38H), and three UNICODE_STRINGs {Length, Length + 2, server
	// pointer} at +0 / +10H / +20H whose NUL-terminated characters lie at +A10H / +A30H / +A60H (csrc.str*, csrc.case): the Windows
	// directory (= the environment's SystemRoot), the system directory (SystemRoot + "\system32", lower case as measured), the named-object
	// directory ("\Sessions\<the PEB SessionId>\BaseNamedObjects"). Placeholders: the environment's SystemRoot and session number (from
	// the capture / the PEB), never the measuring machine's. Every other byte of the view is zero (csrss' shared heap is not modelled; no
	// path of the loader reads it). Other arguments: stop.
	reply kernel::map_csr_shared_view( const call& c, const std::string& what )
	{
		const uint64_t pbase = c.args[ 2 ], psize = c.args[ 6 ];
		const uint32_t type = uint32_t( c.args[ 8 ] );
		if ( c.args[ 1 ] != current_process || c.args[ 3 ] || c.args[ 4 ] || c.args[ 5 ] || uint32_t( c.args[ 7 ] ) != 2 || ( type != 0 && type != 0x500000 ) ||
			 uint32_t( c.args[ 9 ] ) != page::readonly )
			return stop_( what + " - other arguments: not measured (tm_nt csr.map / csrc.map5: NtCurrentProcess(), 0, 0, NULL, ViewUnmap, AllocationType 0 / 500000H, PAGE_READONLY)" );
		if ( !user_buffer( pbase, 8, UC_PROT_READ | UC_PROT_WRITE ) || !user_buffer( psize, 8, UC_PROT_READ | UC_PROT_WRITE ) )
			return done_( status::access_violation, what + ": BaseAddress / ViewSize not writable" );
		uint64_t want_base = 0, want_size = 0;
		get64( pbase, want_base );
		get64( psize, want_size );
		if ( want_base || want_size ) return stop_( what + " - *BaseAddress / *ViewSize not 0: not measured" );
		uint32_t sid = 0;
		std::string err;
		e_.read( f_.peb + 0x2C0, &sid, 4, err );
		const std::string strs[ 3 ] = { f_.system_root, f_.system_root + "\\system32", "\\Sessions\\" + std::to_string( sid ) + "\\BaseNamedObjects" };
		for ( int k = 0; k < 3; k++ )
			if ( f_.system_root.empty() || ( strs[ k ].size() + 1 ) * 2 > csr_str_room[ k ] )
				return stop_( what + " - the environment's SystemRoot \"" + f_.system_root + "\" / session " + std::to_string( sid ) + " does not fit the measured string slots (+A10H / +A30H / +A60H)" );
		uint64_t base = 0;
		if ( !find_free( csr_view_size, true, base ) ) return stop_( what + " - no free range for the view" );
		std::vector<uint8_t> b( size_t( csr_view_committed ), 0 );
		auto put64 = [ & ]( uint64_t off, uint64_t val ) { std::memcpy( b.data() + off, &val, 8 ); };
		put64( csr_array + 8, csr_server_base + csr_bssd );                     // array[1] = BASE_STATIC_SERVER_DATA (a server pointer)
		put64( csr_bssd + csr_bssd_self, csr_server_base + csr_bssd );          // its +9E8H = its own server address
		for ( int k = 0; k < 3; k++ )
		{
			const uint16_t len = uint16_t( strs[ k ].size() * 2 ), max = uint16_t( len + 2 );
			std::memcpy( b.data() + csr_bssd + 0x10 * k, &len, 2 );
			std::memcpy( b.data() + csr_bssd + 0x10 * k + 2, &max, 2 );
			put64( csr_bssd + 0x10 * k + 8, csr_server_base + csr_str_at[ k ] );
			for ( size_t i = 0; i < strs[ k ].size(); i++ ) b[ size_t( csr_str_at[ k ] + 2 * i ) ] = uint8_t( strs[ k ][ i ] );   // UTF-16LE, ASCII characters, NUL after
		}
		if ( !e_.map( base, csr_view_committed, UC_PROT_READ | UC_PROT_WRITE, "CSR shared section view (the connected-client model)", err ) || !e_.write( base, b.data(), b.size(), err ) ||
			 !e_.protect( base, csr_view_committed, UC_PROT_READ, err ) )
			return stop_( what + " - mapping the view failed: " + err );
		struct view vw;
		vw.base = base;
		vw.size = csr_view_size;
		vw.type = mem2::mapped;
		vw.allocation_protect = page::readonly;
		vw.pages.assign( size_t( csr_view_size / f_.page_size ), 0 );   // reserved ...
		for ( uint64_t i = 0; i < csr_view_committed / f_.page_size; i++ ) vw.pages[ size_t( i ) ] = page::readonly;   // ... but the first 5000H
		vw.name = "CSR shared section";
		views_[ base ] = vw;
		reply rr = done_( status::success, what + ": the CSR shared section as a connected client sees it (P40 model; measured: tm_nt csr.map / csrc.*) at " + hex( base ) +
											   ", ViewSize 100000H (5000H committed PAGE_READONLY, the rest reserved): server-data array at +750H, BASE_STATIC_SERVER_DATA at +790H - \"" +
											   strs[ 0 ] + "\", \"" + strs[ 1 ] + "\", \"" + strs[ 2 ] + "\" (the environment's), server pointers relative to " + hex( csr_server_base ) );
		const uint64_t vs = csr_view_size;
		if ( !put( pbase, &base, 8, rr ) || !put( psize, &vs, 8, rr ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtInitializeNlsFiles( *BaseAddress, *DefaultLocaleId, *DefaultCasingTableSize (optional) ) - phnt ntexapi.h:9886-9891. kernelbase's call
	// (P40 trace e4, system call 103): &BaseAddress, &DefaultLocaleId, DefaultCasingTableSize NULL. MEASURED (tm_nt nls.0 / nls.1):
	// STATUS_SUCCESS, a 64K-aligned read-only view {MEM_COMMIT, PAGE_READONLY, MEM_MAPPED, RegionSize = the size of <SystemRoot>\System32\
	// locale.nls rounded up to pages} whose bytes are the file's (every call a new view); DefaultLocaleId = the system default LCID
	// (= GetSystemDefaultLCID(); the value is not printed). The emulated view: the capture's locale.nls (capture_sysdlls.py --dll
	// locale.nls; local, never committed) mapped by map_data_view (top-down, read-only, MEM_MAPPED); DefaultLocaleId: the placeholder
	// 0409H (en-US, [MS-LCID]) - the locale whose ANSI / OEM code pages are the environment's captured 1252 / 437. Not measured (stop): a
	// DefaultCasingTableSize pointer; no locale.nls in the capture.
	reply kernel::svc_initialize_nls_files( const call& c )
	{
		const uint64_t pbase = c.args[ 0 ], plcid = c.args[ 1 ], pcase = c.args[ 2 ];
		const std::string what = "NtInitializeNlsFiles( " + hex( pbase ) + ", " + hex( plcid ) + ", " + hex( pcase ) + " )";
		if ( pcase ) return stop_( what + " - a DefaultCasingTableSize pointer: not measured (tm_nt nls: NULL)" );
		if ( !f_.files || !f_.files->count( "locale.nls" ) ) return stop_( what + " - the environment's capture holds no locale.nls (capture_sysdlls.py --dll locale.nls)" );
		if ( !user_buffer( pbase, 8, UC_PROT_WRITE ) || !user_buffer( plcid, 4, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": BaseAddress / DefaultLocaleId not writable" );
		uint64_t view = 0;
		std::string why;
		if ( !map_data_view( f_.files->at( "locale.nls" ), "locale.nls (NtInitializeNlsFiles)", view, why ) ) return stop_( what + " - " + why );
		reply rr = done_( status::success, what + ": the capture's locale.nls mapped read-only at " + hex( view ) + " (" + hex( f_.files->at( "locale.nls" ).size() ) +
												 " bytes), DefaultLocaleId 409H (placeholder: en-US, the captured code pages 1252 / 437; measured: tm_nt nls)" );
		const uint32_t lcid = 0x409;
		if ( !put( pbase, &view, 8, rr ) || !put( plcid, &lcid, 4, rr ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtCreateFile( *FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
	// CreateDisposition, CreateOptions, EaBuffer, EaLength ) - phnt ntioapi.h (the WDK's ZwCreateFile). P40 FRONTIER (the stop the user
	// asked for: "a service that cannot be measured safely"): after the CSR connection and the NLS files, kernelbase's start of a
	// CONSOLE-subsystem process whose RTL_USER_PROCESS_PARAMETERS carry no console (ConsoleHandle 0 - the environment's parameters; a process
	// started from a GUI parent without a console) allocates a NEW console: NtCreateFile( "\Device\ConDrv\Server", 12019FH, ... ) creates a
	// console-server object on the console driver, and kernelbase then starts a console host process (conhost.exe) for it. Answering means
	// modelling the console driver and starting / being a console host - not a service our own test PE can measure from inside itself
	// without creating a console server and a further process. A loud stop with the decoded call. Every other NtCreateFile: not handled
	// here (the generic loud stop of a known, unimplemented service).
	bool kernel::svc_create_file_measured( const call& c, reply& r )
	{
		if ( !c.stack_args ) return false;
		object_attributes oa;
		std::string why;
		if ( !guest_object_attributes( c.args[ 2 ], oa, why ) || lower( oa.name ) != "\\device\\condrv\\server" ) return false;
		std::string a;
		for ( int i = 0; i < 11; i++ ) a += ( i ? ", " : "" ) + hex( c.args[ size_t( i ) ] );
		r = stop_( "NtCreateFile( " + a + " ) \"" + oa.name + "\" attributes " + hex( oa.attributes ) +
				   " - kernelbase allocates a NEW console for this console-subsystem process (its process parameters carry no console): a console-server object on the console "
				   "driver, then a console host process - not measurable safely from inside our own test PE (P40 frontier)" );
		return true;
	}

	// the common part of the measured object-creating services: ObjectAttributes NULL and a measured access mask only (else a loud
	// stop), the handle pointer probed, a new handle of this process' table (a multiple of 4 - every measurement: non-zero, multiple of 4)
	reply kernel::create_measured_object( const call& c, uint64_t ph, uint32_t access, uint64_t oa, const char* kind, uint64_t value, std::initializer_list<uint32_t> accesses,
										   const char* measured )
	{
		if ( oa ) return stop_( c.name + " with ObjectAttributes " + hex( oa ) + ": not measured (" + measured + ": NULL)" );
		bool ok = false;
		for ( uint32_t a : accesses ) ok = ok || a == access;
		if ( !ok ) return stop_( c.name + " with DesiredAccess " + hex( access ) + ": not measured (" + measured + ")" );
		if ( !user_buffer( ph, 8, UC_PROT_WRITE ) ) return done_( status::access_violation, c.name + ": the handle pointer " + hex( ph ) + " is not writable" );
		object o;
		o.kind = kind;
		o.value = value;
		const uint64_t h = open_handle( o );
		reply rr = done_( status::success, std::string( kind ) + " handle " + hex( h ) + " (DesiredAccess " + hex( access ) + ", value " + hex( value ) + "; measured: " + measured + ")" );
		if ( !put( ph, &h, 8, rr ) ) return stop_( "writing the handle failed after the probe" );
		return rr;
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
		// P44 (U2121): also on NtCurrentThreadEffectiveToken() (the loader after user32's IMM lookup; the thread has no token of its own:
		// MEASURED tm_nt u32k.sat.eff.2 / .1 = the process token's answer)
		bool known = ( token == current_process_token || token == current_thread_effective_token ) && count > 0 && pret != 0;
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
		if ( cls != 0xC5 && cls != 0x37 && cls != 0xC0 && cls != 0x32 && cls != 1 ) return false;   // P45 (U2140): + class 1
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
		if ( cls == 1 )
		{
			// P45 (U2140): 1 SystemProcessorInformation (phnt ntexapi.h SYSTEM_INFORMATION_CLASS; SYSTEM_PROCESSOR_INFORMATION {USHORT
			// ProcessorArchitecture, USHORT ProcessorLevel, USHORT ProcessorRevision, USHORT MaximumProcessors, ULONG ProcessorFeatureBits},
			// CH bytes) - d3d11's DllMain (sample exploration x7 system call 632). MEASURED (tm_nt qsi.1*, the VMware guest): Length CH / 10H =
			// STATUS_SUCCESS, the 12 bytes 09 00 06 00 01 B7 08 00 FF BD 9B BD (AMD64, family 6, model B7H stepping 1, 8 processors, the
			// feature bits), ReturnLength CH; Length 0 / 8 = STATUS_INFO_LENGTH_MISMATCH, ReturnLength CH, nothing written. The environment
			// (its CPUID profile: family 6 model B7H stepping 1; 8 processors) is the measured one; other lengths: stop.
			if ( f_.processors != 8 ) { r = stop_( "SystemProcessorInformation: an environment with " + std::to_string( f_.processors ) + " processors - not measured (8 are)" ); return true; }
			if ( len == 0 || len == 8 ) { finish( status::info_length_mismatch, "SystemProcessorInformation: Length " + hex( len ) + " - STATUS_INFO_LENGTH_MISMATCH, ReturnLength CH (measured: tm_nt qsi.1.0 / .8)", {}, 0xC ); return true; }
			if ( len != 0xC && len != 0x10 ) { r = stop_( "SystemProcessorInformation: Length " + hex( len ) + " - not measured (0, 8, CH, 10H are)" ); return true; }
			const std::vector<uint8_t> d = { 0x09, 0x00, 0x06, 0x00, 0x01, 0xB7, 0x08, 0x00, 0xFF, 0xBD, 0x9B, 0xBD };
			finish( status::success, "SystemProcessorInformation: AMD64, level 6, revision B701H, 8 processors, feature bits BD9BBDFFH (measured: tm_nt qsi.1)", d, 0xC );
			return true;
		}
		if ( cls == 0x32 )
		{
			// 32H SystemRangeStartInformation (phnt ntexapi.h SYSTEM_INFORMATION_CLASS; ULONG_PTR). MEASURED (tm_nt qsi.32*): Length 8 =
			// STATUS_SUCCESS, FFFF800000000000H, ReturnLength 8; Length 4 / 10H = STATUS_INFO_LENGTH_MISMATCH, ReturnLength 8, nothing written
			if ( len != 8 )
			{
				if ( len == 4 || len == 0x10 ) finish( status::info_length_mismatch, "SystemRangeStartInformation: Length " + hex( len ) + " - STATUS_INFO_LENGTH_MISMATCH, ReturnLength 8 (measured: tm_nt qsi.32.4 / .10)", {}, 8 );
				else r = stop_( "SystemRangeStartInformation: Length " + hex( len ) + " - not measured (4, 8, 10H are)" );
				return true;
			}
			const uint64_t start = 0xFFFF800000000000ull;
			std::vector<uint8_t> d( 8 );
			std::memcpy( d.data(), &start, 8 );
			finish( status::success, "SystemRangeStartInformation: FFFF800000000000H (measured: tm_nt qsi.32)", d, 8 );
			return true;
		}
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

	// NtWriteFile( FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset, Key ) - phnt ntioapi.h:2911-2921.
	// P41 (U2071): a process started WITHOUT a console has no standard handles (tm_nt con.pp: StandardInput / Output / Error 0;
	// GetStdHandle = 0); a write to that handle 0 is what its "console output" becomes. MEASURED (tm_nt con.ntwf, a detached process):
	// NtWriteFile( NULL, NULL, NULL, NULL, &iosb, "tm", 2, NULL, NULL ) = STATUS_INVALID_HANDLE (C0000008H), the IoStatusBlock not written
	// (and WriteFile on GetStdHandle(STD_OUTPUT_HANDLE) = FALSE, LastError 6, written 0: tm_nt con.api.wfile). The emulator records every
	// such attempt in the console capture (win_env, observation only). Every other handle (a file, a pipe ...): not modelled - stop.
	reply kernel::svc_write_file( const call& c )
	{
		const std::string what = "NtWriteFile( " + hex( c.args[ 0 ] ) + ", Event " + hex( c.args[ 1 ] ) + ", Apc " + hex( c.args[ 2 ] ) + ", IoStatusBlock " + hex( c.args[ 4 ] ) +
								 ", Buffer " + hex( c.stack_args ? c.args[ 5 ] : 0 ) + ", Length " + hex( c.stack_args ? uint32_t( c.args[ 6 ] ) : 0 ) + " )";
		if ( c.args[ 0 ] == 0 && !c.args[ 1 ] && !c.args[ 2 ] )
			return done_( status::invalid_handle, what + ": handle 0 - the standard handle of a process without a console: STATUS_INVALID_HANDLE, IoStatusBlock not written (measured: tm_nt con.ntwf)" );
		return stop_( what + " - a write to this handle is not modelled (only the no-console standard handle 0 is measured: tm_nt con.ntwf)" );
	}

	// ------------------------------------------------------------------------------------------------ the process token (P41, U2073)
	// NtOpenProcessToken( ProcessHandle, DesiredAccess, *TokenHandle ) - phnt ntseapi.h:609-613. MEASURED (tm_nt tok.open / tok.open.read):
	// NtCurrentProcess() with 8 (TOKEN_QUERY) or 20008H = STATUS_SUCCESS + a handle (non-zero, multiple of 4). Other processes / access: stop.
	reply kernel::svc_open_process_token( const call& c )
	{
		const uint32_t access = uint32_t( c.args[ 1 ] );
		const std::string what = "NtOpenProcessToken( " + hex( c.args[ 0 ] ) + ", access " + hex( access ) + ", " + hex( c.args[ 2 ] ) + " )";
		// P44 (U2121): access 0AH TOKEN_DUPLICATE | TOKEN_QUERY (kernel32 BasepIsServiceSidBlocked, user32's LoadAppInitDlls) - MEASURED
		// tm_nt u32k.svc.open: STATUS_SUCCESS, a handle
		if ( c.args[ 0 ] != current_process || ( access != 8 && access != 0x20008 && access != 0xA ) ) return stop_( what + " - not measured (NtCurrentProcess(), 8 / 20008H / 0AH are)" );
		if ( !user_buffer( c.args[ 2 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": TokenHandle not writable" );
		object o;
		o.kind = "token (process)";
		o.value = access;
		const uint64_t h = open_handle( o );
		reply r = done_( status::success, what + ": the process token, handle " + hex( h ) + " (measured: tm_nt tok.open)" );
		if ( !put( c.args[ 2 ], &h, 8, r ) ) return stop_( "writing the handle failed after the probe" );
		return r;
	}

	// NtQueryInformationToken( TokenHandle, TokenInformationClass, TokenInformation, TokenInformationLength, ReturnLength ) - phnt
	// ntseapi.h:721-727 (TOKEN_INFORMATION_CLASS: TokenSessionId 0CH, TokenIsAppContainer 1DH, TokenPrivateNameSpace 2AH). MEASURED (tm_nt
	// tok.*; the same on NtCurrentProcessToken() and on a handle of NtOpenProcessToken): Length 4 or 8 = STATUS_SUCCESS, 4 bytes written,
	// ReturnLength 4; Length 0 = STATUS_BUFFER_TOO_SMALL (C0000023H), nothing written, ReturnLength 4. Values: 0CH = the PEB SessionId
	// (placeholder <the PEB SessionId>: the emulated PEB's), 1DH = 0 (not an app container), 2AH = 0. 2CH TokenBnoIsolation
	// (TOKEN_BNO_ISOLATION_INFORMATION {PWSTR IsolationPrefix, BOOLEAN IsolationEnabled}, um\winnt.h; the trace: Length 120H) - MEASURED
	// (tm_nt tok.h.2c*): Length 10H / 120H = STATUS_SUCCESS, 9 bytes written (IsolationPrefix NULL, IsolationEnabled FALSE; +9..+FH not
	// written), ReturnLength 10H; Length 0 / 8 = STATUS_BUFFER_TOO_SMALL, ReturnLength 10H. Other classes / lengths / handles: stop.
	reply kernel::svc_query_information_token( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtQueryInformationToken: the stack argument ReturnLength is not readable" );
		const uint64_t h = c.args[ 0 ], out = c.args[ 2 ], pret = c.args[ 4 ];
		const uint32_t cls = uint32_t( c.args[ 1 ] ), len = uint32_t( c.args[ 3 ] );
		const auto it = handles_.find( h );
		const bool tok = h == current_process_token || ( it != handles_.end() && it->second.kind == "token (process)" );
		const std::string what = "NtQueryInformationToken( " + hex( h ) + ( h == current_process_token ? " NtCurrentProcessToken()" : std::string() ) + ", class " + hex( cls ) +
								 ", " + hex( out ) + ", Length " + hex( len ) + ", ReturnLength " + hex( pret ) + " )";
		if ( cls == 1 && h == current_thread_effective_token && pret && ( len == 0 || len >= 0x2C ) )
		{
			// P41 (U2073): TokenUser (TOKEN_USER {SID_AND_ATTRIBUTES {PSID Sid, ULONG Attributes}} + the SID, um\winnt.h) on
			// NtCurrentThreadEffectiveToken() (-6, phnt ntpsapi.h:2491; the thread does not impersonate: the process token) - MEASURED (tm_nt
			// tok.eff.1 / .1.0): Length 58H = STATUS_SUCCESS, ReturnLength 2CH, 2CH bytes {Sid = +10H, Attributes 0, the SID: Revision 1,
			// IdentifierAuthority 5, 5 sub-authorities S-1-5-21-<domain>-<RID>, RID >= 1000}; Length 0 = STATUS_BUFFER_TOO_SMALL, ReturnLength
			// 2CH. The user SID is a REAL IDENTIFIER: the environment answers a PLACEHOLDER of the same shape, S-1-5-21-1000001-1000002-
			// 1000003-1001 (domain sub-authorities and RID chosen; never the measuring machine's).
			const uint32_t need = 0x2C;
			if ( !user_buffer( pret, 4, UC_PROT_WRITE ) || ( len && !user_buffer( out, len, UC_PROT_WRITE ) ) ) return done_( status::access_violation, what + ": an OUT buffer is not writable" );
			if ( len == 0 )
			{
				reply rz = done_( status::buffer_too_small, what + ": TokenUser - STATUS_BUFFER_TOO_SMALL, ReturnLength 2CH (measured: tm_nt tok.eff.1.0)" );
				if ( !put( pret, &need, 4, rz ) ) return stop_( "writing the result failed after the probe" );
				return rz;
			}
			uint8_t u[ 0x2C ] = {};
			const uint64_t sidp = out + 0x10;
			std::memcpy( u, &sidp, 8 );
			u[ 0x10 ] = 1;      // Revision
			u[ 0x11 ] = 5;      // SubAuthorityCount
			u[ 0x17 ] = 5;      // SECURITY_NT_AUTHORITY {0,0,0,0,0,5}
			const uint32_t sa[ 5 ] = { 21, 1000001u, 1000002u, 1000003u, 1001u };   // placeholders (shape S-1-5-21-<domain>-<RID>)
			std::memcpy( u + 0x18, sa, sizeof( sa ) );
			reply ru = done_( status::success, what + ": TokenUser = the PLACEHOLDER user SID S-1-5-21-1000001-1000002-1000003-1001, ReturnLength 2CH (measured shape: tm_nt tok.eff.1)" );
			if ( !put( out, u, 0x2C, ru ) || !put( pret, &need, 4, ru ) ) return stop_( "writing the result failed after the probe" );
			return ru;
		}
		const bool bno = cls == 0x2C;
		if ( !tok || !pret || ( !bno && ( ( cls != 0xC && cls != 0x1D && cls != 0x2A ) || ( len != 0 && len != 4 && len != 8 ) ) ) ||
			 ( bno && len != 0 && len != 8 && len != 0x10 && len != 0x120 ) )
			return stop_( what + " - not measured (the process token: classes 0CH / 1DH / 2AH with Length 0 / 4 / 8, 2CH with 0 / 8 / 10H / 120H, a ReturnLength)" );
		if ( !user_buffer( pret, 4, UC_PROT_WRITE ) || ( len && !user_buffer( out, len, UC_PROT_WRITE ) ) ) return done_( status::access_violation, what + ": an OUT buffer is not writable" );
		if ( bno )
		{
			const uint32_t need = 0x10;
			if ( len < need )
			{
				reply rs = done_( status::buffer_too_small, what + ": TokenBnoIsolation - STATUS_BUFFER_TOO_SMALL, ReturnLength 10H (measured: tm_nt tok.h.2c.0 / .8)" );
				if ( !put( pret, &need, 4, rs ) ) return stop_( "writing the result failed after the probe" );
				return rs;
			}
			const uint8_t z[ 9 ] = {};
			reply rb = done_( status::success, what + ": TokenBnoIsolation {IsolationPrefix NULL, IsolationEnabled FALSE} (9 bytes), ReturnLength 10H (measured: tm_nt tok.h.2c)" );
			if ( !put( out, z, 9, rb ) || !put( pret, &need, 4, rb ) ) return stop_( "writing the result failed after the probe" );
			return rb;
		}
		const uint32_t four = 4;
		if ( len == 0 )
		{
			reply r = done_( status::buffer_too_small, what + ": STATUS_BUFFER_TOO_SMALL, ReturnLength 4 (measured: tm_nt tok.*.0)" );
			if ( !put( pret, &four, 4, r ) ) return stop_( "writing the result failed after the probe" );
			return r;
		}
		uint32_t v = 0;
		std::string name = cls == 0x1D ? "TokenIsAppContainer 0" : "TokenPrivateNameSpace 0";
		if ( cls == 0xC )
		{
			std::string err;
			e_.read( f_.peb + 0x2C0, &v, 4, err );   // PEB SessionId (2C0H, ntdll.pdb _PEB)
			name = "TokenSessionId " + std::to_string( v ) + " (= the PEB SessionId)";
		}
		reply r = done_( status::success, what + ": " + name + ", ReturnLength 4 (measured: tm_nt tok.*)" );
		if ( !put( out, &v, 4, r ) || !put( pret, &four, 4, r ) ) return stop_( "writing the result failed after the probe" );
		return r;
	}

	// NtAccessCheck( SecurityDescriptor, ClientToken, DesiredAccess, GenericMapping, PrivilegeSet, *PrivilegeSetLength, *GrantedAccess,
	// *AccessStatus ) - phnt ntseapi.h:977-986. P41: decoded for the measurement (the security descriptor bytes, the mapping, the lengths).
	reply kernel::svc_access_check( const call& c )
	{
		auto dump = [ & ]( uint64_t a, uint32_t n ) {
			std::string s;
			if ( !a || !user_buffer( a, n, UC_PROT_READ ) ) return std::string( "<n/a>" );
			std::vector<uint8_t> b( n );
			std::string err;
			e_.read( a, b.data(), n, err );
			for ( uint32_t i = 0; i < n; i++ ) { char t[ 4 ]; std::snprintf( t, sizeof( t ), "%02X", b[ i ] ); s += ( i ? " " : "" ) + std::string( t ); }
			return s;
		};
		uint32_t psl = 0;
		if ( c.args[ 5 ] && user_buffer( c.args[ 5 ], 4, UC_PROT_READ ) ) { std::string err; e_.read( c.args[ 5 ], &psl, 4, err ); }
		// a SID at A as "S-R-AUTH-SUB-..." (winnt.h SID: Revision, SubAuthorityCount, IdentifierAuthority[6] big-endian, SubAuthority[])
		auto sid_text = [ & ]( uint64_t a ) {
			uint8_t h[ 8 ] = {};
			std::string err;
			if ( !a || !user_buffer( a, 8, UC_PROT_READ ) ) return std::string( "<n/a>" );
			e_.read( a, h, 8, err );
			if ( h[ 1 ] > 15 || !user_buffer( a + 8, 4ull * h[ 1 ], UC_PROT_READ ) ) return std::string( "<bad sid>" );
			uint64_t auth = 0;
			for ( int i = 2; i < 8; i++ ) auth = ( auth << 8 ) | h[ i ];
			std::string s = "S-" + std::to_string( h[ 0 ] ) + "-" + std::to_string( auth );
			for ( int i = 0; i < h[ 1 ]; i++ ) { uint32_t v = 0; e_.read( a + 8 + 4ull * i, &v, 4, err ); s += "-" + std::to_string( v ); }
			return s;
		};
		std::string sd;
		{
			uint8_t h[ 40 ] = {};
			std::string err;
			if ( user_buffer( c.args[ 0 ], 40, UC_PROT_READ ) )
			{
				e_.read( c.args[ 0 ], h, 40, err );
				uint16_t ctl = 0;
				uint64_t own = 0, grp = 0, dacl = 0;
				std::memcpy( &ctl, h + 2, 2 );
				std::memcpy( &own, h + 8, 8 );
				std::memcpy( &grp, h + 16, 8 );
				std::memcpy( &dacl, h + 32, 8 );
				sd = " {Revision " + std::to_string( h[ 0 ] ) + ", Control " + hex( ctl ) + ", Owner " + sid_text( own ) + ", Group " + sid_text( grp ) + ", DACL " + hex( dacl );
				uint8_t ah[ 8 ] = {};
				if ( !( ctl & 0x8000 ) && dacl && user_buffer( dacl, 8, UC_PROT_READ ) )
				{
					e_.read( dacl, ah, 8, err );
					uint16_t asz = 0, cnt = 0;
					std::memcpy( &asz, ah + 2, 2 );
					std::memcpy( &cnt, ah + 4, 2 );
					sd += " [AclRevision " + std::to_string( ah[ 0 ] ) + ", AclSize " + hex( asz ) + ", AceCount " + std::to_string( cnt );
					uint64_t ace = dacl + 8;
					for ( int i = 0; i < cnt && i < 8 && user_buffer( ace, 8, UC_PROT_READ ); i++ )
					{
						uint8_t eh[ 8 ] = {};
						e_.read( ace, eh, 8, err );
						uint16_t esz = 0;
						uint32_t mask = 0;
						std::memcpy( &esz, eh + 2, 2 );
						std::memcpy( &mask, eh + 4, 4 );
						sd += "; ACE type " + std::to_string( eh[ 0 ] ) + " flags " + hex( eh[ 1 ] ) + " size " + hex( esz ) + " mask " + hex( mask ) + " SID " + sid_text( ace + 8 );
						if ( esz < 8 ) break;
						ace += esz;
					}
					sd += "]";
				}
				sd += "}";
			}
		}
		// P44 (U2121): kernel32 BasepIsServiceSidBlocked (user32's LoadAppInitDlls): an ABSOLUTE SD {Revision 1, Control 4, Owner = Group = a
		// service SID, no SACL, DACL {AclRevision 2, AclSize 198H, 5 ACCESS_ALLOWED ACEs (flags 0, mask 1) for four service SIDs (S-1-5-80 +
		// 5 sub-authorities: hashes of service names, the same on every system) and S-1-5-83-0}}, the duplicated impersonation token,
		// DesiredAccess 1, GenericMapping {20001H, 20000H, 20000H, 1F0001H}, PrivilegeSetLength 38H - MEASURED tm_nt u32k.svc.acc: not a
		// member - STATUS_SUCCESS, GrantedAccess 0, AccessStatus STATUS_ACCESS_DENIED, PrivilegeSet {0, 0} (8 bytes), PrivilegeSetLength 38H
		{
			static const uint8_t sid_owner[ 32 ] = { 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x50, 0x00, 0x00, 0x00, 0x1C, 0x8F, 0x72, 0x3C,
													 0x73, 0xE4, 0x39, 0xC5, 0xB8, 0x0B, 0x81, 0xC6, 0xD2, 0x18, 0x3A, 0x10, 0x2C, 0xD3, 0x8F, 0x05 };
			static const uint8_t sid_a[ 32 ] = { 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x50, 0x00, 0x00, 0x00, 0xF0, 0x5B, 0x58, 0x07,
												 0xC3, 0x43, 0x8C, 0x9A, 0xC7, 0x8A, 0x72, 0xDD, 0x8F, 0x8C, 0xB4, 0xDF, 0x44, 0x47, 0xE7, 0xF8 };
			static const uint8_t sid_b[ 32 ] = { 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x50, 0x00, 0x00, 0x00, 0xBF, 0x55, 0x08, 0x72,
												 0x3B, 0xE0, 0x28, 0xD0, 0x89, 0x79, 0x4B, 0xF8, 0x91, 0x89, 0x6E, 0x7C, 0x40, 0x25, 0xEC, 0xF4 };
			static const uint8_t sid_c[ 32 ] = { 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x50, 0x00, 0x00, 0x00, 0x1B, 0x6D, 0x39, 0x16,
												 0x2D, 0x47, 0x25, 0x79, 0xA6, 0x40, 0xB1, 0xD0, 0xEE, 0xA7, 0x7C, 0x0C, 0x27, 0x4C, 0xEB, 0x61 };
			static const uint8_t sid_vm[ 16 ] = { 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
			static const uint8_t gm_svc[ 16 ] = { 1, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 1, 0, 0x1F, 0 };
			const auto th = handles_.find( c.args[ 1 ] );
			if ( th != handles_.end() && th->second.kind == "token (impersonation)" )
			{
				uint8_t h[ 40 ] = {}, gm[ 16 ] = {}, acl[ 0xC0 ] = {}, own[ 32 ] = {}, grp[ 32 ] = {};
				std::string err;
				bool ok = c.stack_args && user_buffer( c.args[ 0 ], 40, UC_PROT_READ ) && uint32_t( c.args[ 2 ] ) == 1 && psl == 0x38 && user_buffer( c.args[ 3 ], 16, UC_PROT_READ );
				uint64_t po = 0, pg = 0, psa = 0, pda = 0;
				if ( ok )
				{
					e_.read( c.args[ 0 ], h, 40, err );
					e_.read( c.args[ 3 ], gm, 16, err );
					std::memcpy( &po, h + 8, 8 );
					std::memcpy( &pg, h + 16, 8 );
					std::memcpy( &psa, h + 24, 8 );
					std::memcpy( &pda, h + 32, 8 );
					ok = h[ 0 ] == 1 && h[ 2 ] == 4 && h[ 3 ] == 0 && !psa && std::memcmp( gm, gm_svc, 16 ) == 0 && user_buffer( po, 32, UC_PROT_READ ) && user_buffer( pg, 32, UC_PROT_READ ) &&
						 user_buffer( pda, 0xC0, UC_PROT_READ );
				}
				if ( ok )
				{
					e_.read( po, own, 32, err );
					e_.read( pg, grp, 32, err );
					e_.read( pda, acl, 0xC0, err );
					const uint8_t* sids[ 5 ] = { sid_a, sid_b, sid_c, sid_vm, sid_owner };
					const uint32_t lens[ 5 ] = { 32, 32, 32, 16, 32 };
					ok = std::memcmp( own, sid_owner, 32 ) == 0 && std::memcmp( grp, sid_owner, 32 ) == 0 && acl[ 0 ] == 2 && acl[ 2 ] == 0x98 && acl[ 3 ] == 1 && acl[ 4 ] == 5 && acl[ 5 ] == 0;
					uint32_t at = 8;
					for ( int i = 0; ok && i < 5; i++ )
					{
						uint32_t mask = 0;
						std::memcpy( &mask, acl + at + 4, 4 );
						ok = acl[ at ] == 0 && acl[ at + 1 ] == 0 && acl[ at + 2 ] == 8 + lens[ i ] && acl[ at + 3 ] == 0 && mask == 1 && std::memcmp( acl + at + 8, sids[ i ], lens[ i ] ) == 0;
						at += 8 + lens[ i ];
					}
				}
				if ( ok )
				{
					const uint64_t ps = c.args[ 4 ], pgr = c.args[ 6 ], pas = c.args[ 7 ];
					if ( !user_buffer( ps, 0x38, UC_PROT_WRITE ) || !user_buffer( pgr, 4, UC_PROT_WRITE ) || !user_buffer( pas, 4, UC_PROT_WRITE ) )
						return done_( status::access_violation, "NtAccessCheck: an OUT buffer is not writable" );
					reply r = done_( status::success, "NtAccessCheck( the service-SID SD of BasepIsServiceSidBlocked, the duplicated impersonation token, 1 ): not a member - STATUS_SUCCESS, "
														  "GrantedAccess 0, AccessStatus STATUS_ACCESS_DENIED, PrivilegeSet {0, 0} (measured: tm_nt u32k.svc.acc)" );
					const uint64_t zero8 = 0;
					const uint32_t zero = 0, denied = 0xC0000022;
					if ( !put( ps, &zero8, 8, r ) || !put( pgr, &zero, 4, r ) || !put( pas, &denied, 4, r ) ) return stop_( "writing the result failed after the probe" );
					return r;
				}
			}
		}
		// P41 (U2074): MEASURED (tm_nt acc.N): ntdll's membership check of kernelbase's start - an ABSOLUTE SD {Revision 1, Control 4
		// (SE_DACL_PRESENT), Owner = Group = the SID, no SACL, DACL {AclRevision 2, 1 ACE: ACCESS_ALLOWED type 0, flags 0, size 34H, mask 1,
		// the SID}}, NtCurrentProcessToken(), DesiredAccess 1, GenericMapping {20001H, 20000H, 20000H, 1F0001H}, PrivilegeSetLength 38H -
		// with a CAPABILITY GROUP SID (S-1-5-32 + 8 sub-authorities: derived from a capability name, the same on every machine) the
		// desktop process' token is not a member: STATUS_SUCCESS, GrantedAccess 0, AccessStatus STATUS_ACCESS_DENIED (C0000022H),
		// PrivilegeSet = {PrivilegeCount 0, Control 0} (8 bytes written), PrivilegeSetLength unchanged; the same for S-1-5-32-581 (tm_nt
		// acc.2: the second check, ACE size 18H). The measured SIDs only.
		{
			static const char* const measured[] = {
				"S-1-5-32-1604681682-535129537-3273749797-3666938095-336295784-2177615760-2743807136-2867270584",   // tm_nt acc.1
				"S-1-5-32-581" };   // tm_nt acc.2 (BUILTIN alias RID 581, um\winnt.h DOMAIN_ALIAS_RID_DEFAULT_ACCOUNT)
			uint8_t h[ 40 ] = {}, ah[ 8 ] = {}, eh[ 8 ] = {}, gm[ 16 ] = {};
			static const uint8_t gm_want[ 16 ] = { 1, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 1, 0, 0x1F, 0 };
			std::string err;
			bool shape = c.stack_args && user_buffer( c.args[ 0 ], 40, UC_PROT_READ ) && c.args[ 1 ] == current_process_token && uint32_t( c.args[ 2 ] ) == 1 && psl == 0x38 &&
						 user_buffer( c.args[ 3 ], 16, UC_PROT_READ );
			uint64_t own = 0, grp = 0, sacl = 0, dacl = 0;
			if ( shape )
			{
				e_.read( c.args[ 0 ], h, 40, err );
				e_.read( c.args[ 3 ], gm, 16, err );
				std::memcpy( &own, h + 8, 8 );
				std::memcpy( &grp, h + 16, 8 );
				std::memcpy( &sacl, h + 24, 8 );
				std::memcpy( &dacl, h + 32, 8 );
				shape = h[ 0 ] == 1 && h[ 2 ] == 4 && h[ 3 ] == 0 && !sacl && std::memcmp( gm, gm_want, 16 ) == 0 && dacl && user_buffer( dacl, 0x3C, UC_PROT_READ );
			}
			if ( shape )
			{
				e_.read( dacl, ah, 8, err );
				e_.read( dacl + 8, eh, 8, err );
				uint32_t mask = 0;
				uint8_t subs = 0;
				std::memcpy( &mask, eh + 4, 4 );
				e_.read( dacl + 17, &subs, 1, err );   // the ACE's SID: SubAuthorityCount
				const std::string s = sid_text( own );
				shape = ah[ 0 ] == 2 && ah[ 4 ] == 1 && ah[ 5 ] == 0 && eh[ 0 ] == 0 && eh[ 1 ] == 0 && eh[ 2 ] == 16 + 4 * subs && eh[ 3 ] == 0 && mask == 1 && sid_text( grp ) == s &&
						sid_text( dacl + 16 ) == s;
				bool known = false;
				for ( auto m : measured ) known = known || s == m;
				if ( shape && known )
				{
					const uint64_t ps = c.args[ 4 ], pg = c.args[ 6 ], pa = c.args[ 7 ];
					if ( !user_buffer( ps, 0x38, UC_PROT_WRITE ) || !user_buffer( pg, 4, UC_PROT_WRITE ) || !user_buffer( pa, 4, UC_PROT_WRITE ) )
						return done_( status::access_violation, "NtAccessCheck: an OUT buffer is not writable" );
					reply r = done_( status::success, "NtAccessCheck( the membership check of " + s + " (the membership check), NtCurrentProcessToken(), 1 ): not a member - "
														  "STATUS_SUCCESS, GrantedAccess 0, AccessStatus STATUS_ACCESS_DENIED, PrivilegeSet {0, 0} (measured: tm_nt acc.*)" );
					const uint64_t zero8 = 0;
					const uint32_t zero = 0, denied = 0xC0000022;
					if ( !put( ps, &zero8, 8, r ) || !put( pg, &zero, 4, r ) || !put( pa, &denied, 4, r ) ) return stop_( "writing the result failed after the probe" );
					return r;
				}
			}
		}
		return stop_( "NtAccessCheck( SD " + hex( c.args[ 0 ] ) + sd + " [" + dump( c.args[ 0 ], 0x28 ) + "], ClientToken " + hex( c.args[ 1 ] ) + ", DesiredAccess " + hex( uint32_t( c.args[ 2 ] ) ) +
					  ", GenericMapping " + hex( c.args[ 3 ] ) + " [" + dump( c.args[ 3 ], 16 ) + "], PrivilegeSet " + hex( c.args[ 4 ] ) + ", PrivilegeSetLength " + hex( c.args[ 5 ] ) + " = " + hex( psl ) +
					  ", GrantedAccess " + hex( c.stack_args ? c.args[ 6 ] : 0 ) + ", AccessStatus " + hex( c.stack_args ? c.args[ 7 ] : 0 ) + " ) - not measured yet (P41)" );
	}

	// NtCreateMutant( *MutantHandle, DesiredAccess, ObjectAttributes, InitialOwner ) - phnt ntexapi.h. P41 (U2076): kernelbase's start
	// creates the WIL staging mutant "Local\\SM0:<the process id>:304:WilStaging_02" relative to the session's BaseNamedObjects directory,
	// OBJ_OPENIF, 1F0001H, InitialOwner FALSE. MEASURED (tm_nt mut.*): a name no object has = STATUS_SUCCESS + a handle (mut.fresh); the
	// same name while the first one exists = STATUS_OBJECT_NAME_EXISTS (40000000H) + a handle (mut.loader: the process' own kernelbase
	// created it during its start - so kernelbase's own first call was the creation). The emulated environment holds no other process,
	// so the first call creates. Other names / roots / access: stop.
	reply kernel::svc_create_mutant( const call& c )
	{
		object_attributes oa;
		std::string why;
		if ( c.args[ 2 ] && !guest_object_attributes( c.args[ 2 ], oa, why ) ) return stop_( "NtCreateMutant: " + why );
		const auto it = handles_.find( oa.root );
		const std::string what = "NtCreateMutant( " + hex( c.args[ 0 ] ) + ", access " + hex( uint32_t( c.args[ 1 ] ) ) + ", OA " + hex( c.args[ 2 ] ) + " {root " + hex( oa.root ) +
								 ( it != handles_.end() ? " (" + it->second.kind + " " + it->second.name + ")" : std::string() ) + ", \"" + oa.name + "\", attributes " + hex( oa.attributes ) +
								 "}, InitialOwner " + hex( uint8_t( c.args[ 3 ] ) ) + " )";
		uint32_t sid = 0;
		std::string err;
		e_.read( f_.peb + 0x2C0, &sid, 4, err );   // PEB SessionId (2C0H, ntdll.pdb _PEB)
		const std::string bno = "\\Sessions\\" + std::to_string( sid ) + "\\BaseNamedObjects";
		const std::string want = "Local\\SM0:" + std::to_string( f_.process_id ) + ":304:WilStaging_02";
		if ( !c.args[ 2 ] || it == handles_.end() || it->second.kind != "directory" || it->second.name != bno || oa.name != want || oa.attributes != 0x80 ||
			 uint32_t( c.args[ 1 ] ) != 0x1F0001 || uint8_t( c.args[ 3 ] ) != 0 )
			return stop_( what + " - not measured (tm_nt mut.*: the session's BaseNamedObjects, \"Local\\SM0:<the process id>:304:WilStaging_02\", OBJ_OPENIF, 1F0001H, FALSE)" );
		if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": MutantHandle not writable" );
		const std::string full = bno + "\\" + oa.name;
		const bool exists = named_objects_.count( full ) && named_objects_[ full ] > 0;
		object o;
		o.kind = "mutant";
		o.name = full;
		const uint64_t hnd = open_handle( o );
		named_objects_[ full ]++;
		if ( !exists ) mutant_state_[ full ] = 1;   // InitialOwner FALSE: free (signal state 1)
		reply r = done_( exists ? 0x40000000u : status::success, what + ": mutant handle " + hex( hnd ) + ( exists ? " - STATUS_OBJECT_NAME_EXISTS (measured: tm_nt mut.loader)" : " - created (measured: tm_nt mut.fresh)" ) );
		if ( !put( c.args[ 0 ], &hnd, 8, r ) ) return stop_( "writing the handle failed after the probe" );
		return r;
	}

	// P41 (U2076): the mutant's wait and release - MEASURED (tm_nt mut.wait* / mut.rel*, a fresh mutant of the loader's shape, one thread):
	// NtWaitForSingleObject( the mutant, FALSE, NULL ) = STATUS_SUCCESS (acquired; a second wait by the owner also = STATUS_SUCCESS);
	// NtReleaseMutant( h, &PreviousCount ) = STATUS_SUCCESS with PreviousCount = the signal state before the release (FFFFFFFFH after two
	// waits, then 0); a release while not owned = STATUS_MUTANT_NOT_OWNED (C0000046H), PreviousCount not written. The phnt prototypes:
	// ntexapi.h NtReleaseMutant, ntobapi.h NtWaitForSingleObject. One thread: the owner is always the waiting thread. Alertable / timeouts: stop.
	reply kernel::svc_mutant_wait_release( const call& c )
	{
		const auto it = handles_.find( c.args[ 0 ] );
		if ( it == handles_.end() || it->second.kind != "mutant" ) return stop_( c.name + ": handle " + hex( c.args[ 0 ] ) + " is not a mutant of this process - not measured" );
		int64_t& s = mutant_state_[ it->second.name ];
		if ( c.name == "NtWaitForSingleObject" )
		{
			if ( uint8_t( c.args[ 1 ] ) || c.args[ 2 ] ) return stop_( "NtWaitForSingleObject on the mutant " + hex( c.args[ 0 ] ) + " Alertable / with a timeout: not measured" );
			s--;
			return done_( status::success, "NtWaitForSingleObject( mutant " + hex( c.args[ 0 ] ) + " \"" + it->second.name + "\", FALSE, NULL ): acquired by the thread, signal state " +
											   std::to_string( s ) + " (measured: tm_nt mut.wait*)" );
		}
		if ( s >= 1 ) return done_( 0xC0000046, "NtReleaseMutant( " + hex( c.args[ 0 ] ) + " ): not owned - STATUS_MUTANT_NOT_OWNED, PreviousCount not written (measured: tm_nt mut.rel2)" );
		if ( c.args[ 1 ] && !user_buffer( c.args[ 1 ], 4, UC_PROT_WRITE ) ) return done_( status::access_violation, "NtReleaseMutant: PreviousCount not writable" );
		const int32_t prev = int32_t( s );
		s++;
		reply r = done_( status::success, "NtReleaseMutant( mutant " + hex( c.args[ 0 ] ) + " ): released, PreviousCount " + std::to_string( prev ) + ", signal state " + std::to_string( s ) +
											  " (measured: tm_nt mut.rel*)" );
		if ( c.args[ 1 ] && !put( c.args[ 1 ], &prev, 4, r ) ) return stop_( "writing the result failed after the probe" );
		return r;
	}

	// P41: the decoded stop of a named-object service whose OBJECT_ATTRIBUTES is argument 2 (shared/ntdef.h) - the measurement replays it
	reply kernel::svc_named_decode( const call& c )
	{
		object_attributes oa;
		std::string why;
		if ( c.args[ 2 ] && !guest_object_attributes( c.args[ 2 ], oa, why ) ) return stop_( c.name + ": " + why );
		const auto it = handles_.find( oa.root );
		std::string a;
		for ( int i = 3; i < 6; i++ ) a += ", " + hex( c.args[ size_t( i ) ] );
		return stop_( c.name + "( " + hex( c.args[ 0 ] ) + ", access " + hex( uint32_t( c.args[ 1 ] ) ) + ", OA " + hex( c.args[ 2 ] ) + " {root " + hex( oa.root ) +
					  ( it != handles_.end() ? " (" + it->second.kind + " " + it->second.name + ")" : std::string() ) + ", \"" + oa.name + "\", attributes " + hex( oa.attributes ) +
					  "}" + a + " ) - not measured yet (P41)" );
	}

	// P41 (U2077): the WIL staging names kernelbase's start uses in the session's BaseNamedObjects (traces c12 / c15): "Local\\SM0:<the
	// process id>:304:WilStaging_02" and names beginning "Local\\SM0:<the process id>:304:WilStaging_02_" relative to that directory
	bool kernel::wil_name( const object_attributes& oa, std::string& full, std::string& why )
	{
		uint32_t sid = 0;
		std::string err;
		e_.read( f_.peb + 0x2C0, &sid, 4, err );   // PEB SessionId (2C0H, ntdll.pdb _PEB)
		const std::string bno = "\\Sessions\\" + std::to_string( sid ) + "\\BaseNamedObjects";
		const auto it = handles_.find( oa.root );
		if ( it == handles_.end() || it->second.kind != "directory" || it->second.name != bno )
		{
			why = "the root is not the session's BaseNamedObjects directory";
			return false;
		}
		const std::string pre = "Local\\SM0:" + std::to_string( f_.process_id ) + ":304:WilStaging_02_";
		if ( oa.name.rfind( pre, 0 ) != 0 || oa.name.size() > pre.size() + 8 )
		{
			why = "not a WIL staging name of this process (\"" + pre + "...\")";
			return false;
		}
		full = bno + "\\" + oa.name;
		return true;
	}

	// NtOpenSemaphore( *SemaphoreHandle, DesiredAccess, ObjectAttributes ) / NtCreateSemaphore( ..., InitialCount, MaximumCount ) -
	// phnt ntexapi.h. MEASURED (tm_nt wil.open.*): 1F0003H, attributes 0, a WIL staging name: an existing semaphore = STATUS_SUCCESS + a
	// handle; a name no object has = STATUS_OBJECT_NAME_NOT_FOUND (C0000034H), the handle NOT written. NtCreateSemaphore: decoded stop until
	// measured.
	reply kernel::svc_wil_semaphore( const call& c )
	{
		object_attributes oa;
		std::string why, full;
		if ( !c.args[ 2 ] || !guest_object_attributes( c.args[ 2 ], oa, why ) ) return stop_( c.name + ": " + ( why.empty() ? std::string( "no ObjectAttributes" ) : why ) );
		std::string a;
		for ( int i = 3; i < 5; i++ ) a += ", " + hex( c.args[ size_t( i ) ] );
		const std::string what = c.name + "( " + hex( c.args[ 0 ] ) + ", access " + hex( uint32_t( c.args[ 1 ] ) ) + ", OA {root " + hex( oa.root ) + ", \"" + oa.name + "\", attributes " +
								 hex( oa.attributes ) + "}" + ( c.name == "NtCreateSemaphore" ? a : std::string() ) + " )";
		if ( !wil_name( oa, full, why ) ) return stop_( what + " - " + why + ": not measured" );
		if ( c.name == "NtCreateSemaphore" )
		{
			// MEASURED (tm_nt wil.create): 1F0003H, OBJ_OPENIF, InitialCount = MaximumCount = 28CF0H (the loader's; WIL encodes its data in
			// the counts): a name no object has = STATUS_SUCCESS + a handle; the same name while it exists = STATUS_OBJECT_NAME_EXISTS
			// (40000000H) + a handle. The counts are kept as given when 0 <= InitialCount <= MaximumCount and MaximumCount > 0 (the documented
			// rule of a semaphore's count and limit: WDK km\wdm.h KeInitializeSemaphore); other counts / access / attributes: stop.
			const int32_t init = int32_t( c.args[ 3 ] ), maxc = int32_t( c.args[ 4 ] );
			if ( uint32_t( c.args[ 1 ] ) != 0x1F0003 || oa.attributes != 0x80 || maxc <= 0 || init < 0 || init > maxc ) return stop_( what + " - not measured (1F0003H, OBJ_OPENIF, 0 <= InitialCount <= MaximumCount > 0)" );
			if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": SemaphoreHandle not writable" );
			const bool exists = named_objects_.count( full ) && named_objects_[ full ] > 0;
			object o;
			o.kind = "semaphore";
			o.name = full;
			o.value = ( uint64_t( uint32_t( maxc ) ) << 32 ) | uint32_t( init );
			const uint64_t hnd = open_handle( o );
			named_objects_[ full ]++;
			reply rc = done_( exists ? 0x40000000u : status::success, what + ": semaphore handle " + hex( hnd ) + ( exists ? " - STATUS_OBJECT_NAME_EXISTS (measured: tm_nt wil.create)" : " - created (measured: tm_nt wil.create)" ) );
			if ( !put( c.args[ 0 ], &hnd, 8, rc ) ) return stop_( "writing the handle failed after the probe" );
			return rc;
		}
		if ( uint32_t( c.args[ 1 ] ) != 0x1F0003 || oa.attributes != 0 ) return stop_( what + " - not measured (1F0003H, attributes 0 are)" );
		if ( !user_buffer( c.args[ 0 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": SemaphoreHandle not writable" );
		if ( !named_objects_.count( full ) || !named_objects_[ full ] ) return done_( status::object_name_not_found, what + ": no such object - STATUS_OBJECT_NAME_NOT_FOUND, handle not written (measured: tm_nt wil.open.fresh)" );
		object o;
		o.kind = "semaphore";
		o.name = full;
		const uint64_t hnd = open_handle( o );
		named_objects_[ full ]++;
		reply r = done_( status::success, what + ": semaphore handle " + hex( hnd ) + " (measured: tm_nt wil.open.loader)" );
		if ( !put( c.args[ 0 ], &hnd, 8, r ) ) return stop_( "writing the handle failed after the probe" );
		return r;
	}

	// NtQueryWnfStateData( StateName, TypeId, ExplicitScope, *ChangeStamp, Buffer, *BufferLength ) - phnt ntexapi.h:1902-1909
	// (WNF_STATE_NAME :1737-1752: a 64-bit encoded name). P41 (U2078): kernelbase's start queries the state 0280032EA3BC0875H with its
	// TypeId {4E8552D648F96463H, 4E26174EC1F2029CH} (constants of the system DLL), no scope, a 1000H buffer. MEASURED (tm_nt wnf.*):
	// STATUS_SUCCESS, 8 data bytes 01 00 00 00 00 00 00 00 (a configuration value of the guest), BufferLength 8, ChangeStamp written
	// non-zero (placeholder 1: the counter of the state's changes); a buffer below 8 = STATUS_BUFFER_TOO_SMALL with BufferLength 8 and the
	// ChangeStamp written, no data; TypeId NULL = STATUS_INVALID_PARAMETER, nothing written. Other states / scopes: stop.
	reply kernel::svc_query_wnf_state_data( const call& c )
	{
		uint64_t name = 0, tid[ 2 ] = {};
		uint32_t blen = 0;
		std::string err;
		if ( c.args[ 0 ] && user_buffer( c.args[ 0 ], 8, UC_PROT_READ ) ) e_.read( c.args[ 0 ], &name, 8, err );
		if ( c.args[ 1 ] && user_buffer( c.args[ 1 ], 16, UC_PROT_READ ) ) e_.read( c.args[ 1 ], tid, 16, err );
		if ( c.stack_args && c.args[ 5 ] && user_buffer( c.args[ 5 ], 4, UC_PROT_READ ) ) e_.read( c.args[ 5 ], &blen, 4, err );
		const std::string what = "NtQueryWnfStateData( StateName " + hex( c.args[ 0 ] ) + " = " + hex( name ) + ", TypeId " + hex( c.args[ 1 ] ) +
								 ( c.args[ 1 ] ? " = " + hex( tid[ 0 ] ) + " " + hex( tid[ 1 ] ) : std::string() ) + ", ExplicitScope " + hex( c.args[ 2 ] ) + ", ChangeStamp " + hex( c.args[ 3 ] ) +
								 ", Buffer " + hex( c.stack_args ? c.args[ 4 ] : 0 ) + ", BufferLength " + hex( c.stack_args ? c.args[ 5 ] : 0 ) + " = " + hex( blen ) + " )";
		if ( c.stack_args && ( name == 0x418A073AA3BC7C75ull || name == 0x418A073AA3BC88F5ull ) && !c.args[ 1 ] && !c.args[ 2 ] && c.args[ 3 ] && c.args[ 5 ] &&
			 ( blen == 0 || blen == 0xC8 || blen == 0x1000 ) )
		{
			// the second / third state of kernelbase's start (traces c21, x1) - MEASURED (tm_nt wnf2.* / wnf3.*): no TypeId, Buffer NULL /
			// BufferLength 0 (a size probe), C8H or 1000H = STATUS_SUCCESS, ChangeStamp 0, BufferLength 0, no data (the states hold none)
			if ( !user_buffer( c.args[ 3 ], 4, UC_PROT_WRITE ) || !user_buffer( c.args[ 5 ], 4, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": an OUT buffer is not writable" );
			const uint32_t zero = 0;
			reply r2 = done_( status::success, what + ": no data - STATUS_SUCCESS, ChangeStamp 0, BufferLength 0 (measured: tm_nt wnf2.* / wnf3.*)" );
			if ( !put( c.args[ 3 ], &zero, 4, r2 ) || !put( c.args[ 5 ], &zero, 4, r2 ) ) return stop_( "writing the result failed after the probe" );
			return r2;
		}
		if ( c.stack_args && name == 0x418A073AA3BC1C75ull && !c.args[ 1 ] && !c.args[ 2 ] && c.args[ 3 ] && c.args[ 5 ] && ( blen == 0 || blen == 0x1000 ) )
		{
			// P41 (U2078): the fourth state (ExitProcess - WIL's feature-usage code, trace c37) - MEASURED (tm_nt wnf4.1000 / wnf4.0): no TypeId,
			// a 1000H buffer = STATUS_SUCCESS, BufferLength = the state's size, its bytes, ChangeStamp written non-zero (placeholder 1);
			// BufferLength 0 = STATUS_BUFFER_TOO_SMALL with the size and the ChangeStamp written. The state CHANGES (a table the system extends
			// as features are used: 44CH at the measurement 2026-10-26 07:4x, 450H an hour later; 1EAH with the header count 1 after the guest's
			// restart, P43), so the native reference checks its SHAPE (U2104: size 1..1000H, written = the size, the header with its count as
			// zero / non-zero) and the environment answers the 44CH SNAPSHOT of the measurement below (WIL feature-usage data of the guest
			// system: no identity; tm_nt wnf6 log of P41)
			static const uint8_t state4[ 0x44C ] = {
			0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0x8A, 0x83, 0x85, 0x00, 0xA5, 0xAD, 0xCF, 0x00,
			0xCE, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x40, 0x27, 0x01, 0x02, 0x38, 0x8F, 0x02, 0x02, 0x99, 0x66, 0x00, 0x02, 0xE6, 0x38, 0x01, 0x03, 0xBB,
			0x92, 0x03, 0x04, 0x69, 0xC2, 0x03, 0x05, 0x33, 0xC0, 0x03, 0x05, 0xD4, 0x7F, 0x00, 0x07, 0x00, 0x45, 0x03, 0x08, 0x6A, 0x67, 0x03, 0x09, 0x92,
			0xF8, 0x00, 0x09, 0xA3, 0x36, 0x01, 0x0B, 0xAE, 0x92, 0x01, 0x0D, 0x30, 0x45, 0x03, 0x0D, 0xA1, 0x81, 0x00, 0x0D, 0xDB, 0x80, 0x01, 0x0F, 0x3A,
			0xB4, 0x03, 0x0F, 0x54, 0xBE, 0x02, 0x10, 0xAF, 0xCA, 0x03, 0x11, 0x8A, 0xE6, 0x02, 0x11, 0xDC, 0xE4, 0x02, 0x12, 0x1F, 0xB8, 0x03, 0x12, 0xE5,
			0xF8, 0x00, 0x13, 0xBE, 0xD0, 0x02, 0x17, 0x95, 0x35, 0x02, 0x17, 0xD9, 0xD4, 0x02, 0x1B, 0x42, 0x78, 0x00, 0x22, 0x57, 0x63, 0x03, 0x23, 0x11,
			0xAF, 0x02, 0x23, 0x83, 0xE7, 0x01, 0x24, 0x6B, 0x46, 0x01, 0x24, 0xAC, 0xC7, 0x00, 0x24, 0xFF, 0xC3, 0x03, 0x26, 0x8B, 0x0A, 0x03, 0x26, 0x9F,
			0x2D, 0x03, 0x28, 0x12, 0xEA, 0x02, 0x28, 0x58, 0xFF, 0x02, 0x29, 0x28, 0xC0, 0x03, 0x29, 0xB1, 0x60, 0x03, 0x2A, 0x02, 0x4A, 0x03, 0x2A, 0x0E,
			0x39, 0x01, 0x2B, 0x9F, 0x5B, 0x02, 0x2C, 0xC9, 0xEE, 0x02, 0x2D, 0x21, 0x52, 0x03, 0x2E, 0x80, 0x1D, 0x01, 0x2F, 0xB6, 0xF6, 0x02, 0x30, 0x50,
			0x25, 0x01, 0x31, 0x11, 0x60, 0x03, 0x31, 0x15, 0xE7, 0x02, 0x31, 0x48, 0x4F, 0x00, 0x31, 0xCF, 0x44, 0x02, 0x37, 0xE0, 0x9B, 0x03, 0x38, 0xAC,
			0xC3, 0x03, 0x39, 0xDF, 0x94, 0x02, 0x3D, 0x73, 0xEB, 0x02, 0x3D, 0x7F, 0xE6, 0x00, 0x40, 0x56, 0xF1, 0x00, 0x42, 0x7B, 0xBA, 0x03, 0x47, 0xB1,
			0x49, 0x03, 0x48, 0xD7, 0x70, 0x03, 0x48, 0xF9, 0xA6, 0x00, 0x4E, 0x12, 0x24, 0x01, 0x4E, 0xE7, 0xC1, 0x00, 0x4F, 0xC5, 0xB7, 0x03, 0x52, 0x32,
			0x30, 0x02, 0x55, 0x5F, 0x2A, 0x01, 0x58, 0xFD, 0xFC, 0x00, 0x5C, 0x74, 0x65, 0x01, 0x5F, 0x77, 0xC1, 0x03, 0x60, 0xBA, 0x32, 0x02, 0x63, 0x96,
			0x77, 0x00, 0x65, 0xC9, 0x76, 0x03, 0x66, 0xB3, 0xC1, 0x03, 0x67, 0x1C, 0x8A, 0x02, 0x6A, 0x49, 0xCB, 0x03, 0x6A, 0x74, 0x95, 0x03, 0x6A, 0xC5,
			0x62, 0x02, 0x6B, 0x10, 0x60, 0x03, 0x6C, 0x28, 0x85, 0x02, 0x6E, 0x69, 0xC9, 0x03, 0x6F, 0x00, 0xBC, 0x02, 0x70, 0x1D, 0x8E, 0x03, 0x71, 0x40,
			0xA3, 0x00, 0x71, 0x7B, 0xBA, 0x03, 0x71, 0x96, 0x58, 0x03, 0x72, 0xA2, 0x42, 0x01, 0x73, 0x55, 0xFD, 0x02, 0x76, 0x79, 0xE9, 0x02, 0x77, 0x41,
			0xC7, 0x03, 0x79, 0x9C, 0x39, 0x00, 0x79, 0xCB, 0xA1, 0x03, 0x7A, 0x8B, 0x88, 0x03, 0x7B, 0x83, 0xA5, 0x03, 0x7B, 0x86, 0x3D, 0x02, 0x7B, 0xA8,
			0xD1, 0x00, 0x7E, 0x10, 0x44, 0x01, 0x7F, 0xC1, 0xA8, 0x02, 0x80, 0x10, 0xB8, 0x03, 0x80, 0x4A, 0x71, 0x02, 0x83, 0x02, 0x3C, 0x03, 0x84, 0x50,
			0xEB, 0x00, 0x86, 0x49, 0x1C, 0x01, 0x87, 0x6A, 0x49, 0x01, 0x87, 0xD7, 0x21, 0x01, 0x89, 0x2F, 0x83, 0x02, 0x8A, 0x1F, 0xB8, 0x03, 0x8A, 0x83,
			0x85, 0x00, 0x8B, 0xEE, 0xF2, 0x00, 0x8C, 0xFF, 0xC0, 0x03, 0x8D, 0xBD, 0xA6, 0x03, 0x8E, 0xD6, 0xDC, 0x01, 0x8F, 0x92, 0xC4, 0x03, 0x90, 0xEE,
			0x37, 0x01, 0x91, 0x1A, 0x9F, 0x03, 0x93, 0x86, 0x61, 0x00, 0x95, 0x0B, 0xFE, 0x00, 0x95, 0x9F, 0x33, 0x01, 0x96, 0x85, 0xB7, 0x03, 0x97, 0x01,
			0xA8, 0x02, 0x97, 0xDE, 0x12, 0x03, 0x98, 0x7F, 0x5C, 0x03, 0x99, 0x50, 0xC5, 0x03, 0x9C, 0x1F, 0x4D, 0x03, 0x9C, 0x62, 0x3A, 0x01, 0x9C, 0xE0,
			0xA8, 0x00, 0x9D, 0x9D, 0x92, 0x00, 0xA1, 0x43, 0xD3, 0x03, 0xA2, 0x58, 0x9C, 0x02, 0xA2, 0xBB, 0x1A, 0x03, 0xA3, 0x56, 0x88, 0x03, 0xA5, 0x59,
			0xA7, 0x03, 0xA6, 0x1C, 0xA8, 0x02, 0xA7, 0x54, 0xA1, 0x02, 0xA9, 0x17, 0x06, 0x02, 0xAE, 0xC7, 0x8F, 0x02, 0xB0, 0x54, 0xA1, 0x02, 0xB0, 0xA0,
			0x54, 0x03, 0xB0, 0xD6, 0x28, 0x01, 0xB0, 0xDC, 0xC9, 0x03, 0xB0, 0xF6, 0x2E, 0x03, 0xB3, 0x40, 0xC6, 0x03, 0xB4, 0x57, 0xCC, 0x03, 0xB5, 0x54,
			0xA1, 0x02, 0xB5, 0x61, 0x0D, 0x01, 0xB8, 0x54, 0xA1, 0x02, 0xB8, 0x94, 0x76, 0x03, 0xB9, 0x71, 0xBC, 0x03, 0xB9, 0x9E, 0xC9, 0x01, 0xBA, 0x15,
			0xC7, 0x03, 0xBB, 0x6A, 0x85, 0x02, 0xBC, 0x54, 0xA1, 0x02, 0xBC, 0xD2, 0x2A, 0x01, 0xBD, 0x44, 0x74, 0x03, 0xBE, 0x72, 0xBA, 0x03, 0xBE, 0x82,
			0x72, 0x03, 0xBF, 0xA6, 0x6F, 0x03, 0xC0, 0x54, 0xA1, 0x02, 0xC0, 0xF9, 0xBA, 0x03, 0xC2, 0x0C, 0x5B, 0x02, 0xC3, 0x6D, 0x81, 0x00, 0xC3, 0x99,
			0xF3, 0x00, 0xC3, 0xAE, 0xF1, 0x02, 0xC4, 0x27, 0x4E, 0x03, 0xC6, 0x16, 0xAF, 0x03, 0xCC, 0x49, 0x56, 0x00, 0xCE, 0xD6, 0x85, 0x03, 0xCF, 0x67,
			0x69, 0x01, 0xD0, 0x17, 0x56, 0x00, 0xD1, 0x35, 0x9F, 0x03, 0xD4, 0x41, 0xBA, 0x03, 0xD4, 0xD1, 0x7A, 0x03, 0xD7, 0x3D, 0xEB, 0x02, 0xD7, 0x78,
			0xBE, 0x03, 0xD7, 0xE5, 0xBA, 0x03, 0xD9, 0x07, 0x24, 0x01, 0xD9, 0xA2, 0x80, 0x03, 0xDA, 0x16, 0x3E, 0x02, 0xDA, 0x38, 0xC8, 0x01, 0xDA, 0x43,
			0xC3, 0x03, 0xDD, 0x1B, 0x19, 0x01, 0xDF, 0xA8, 0xC6, 0x01, 0xE0, 0x61, 0xC3, 0x02, 0xE0, 0x94, 0x38, 0x02, 0xE1, 0xD9, 0xF5, 0x02, 0xE4, 0x69,
			0xC9, 0x00, 0xE5, 0x81, 0xC3, 0x03, 0xE5, 0xCE, 0x9A, 0x02, 0xE5, 0xD9, 0x59, 0x03, 0xE6, 0x6C, 0x81, 0x00, 0xE9, 0x1B, 0xB3, 0x02, 0xE9, 0x93,
			0xC4, 0x03, 0xEA, 0xC6, 0x6A, 0x03, 0xED, 0x3E, 0xDC, 0x02, 0xEE, 0x05, 0x17, 0x03, 0xEF, 0x26, 0x4D, 0x03, 0xEF, 0x79, 0x8B, 0x00, 0xF0, 0x1C,
			0x3F, 0x03, 0xF0, 0xE0, 0xB6, 0x00, 0xF3, 0x08, 0xDB, 0x00, 0xF3, 0x28, 0x21, 0x01, 0xF3, 0xD5, 0x28, 0x02, 0xF4, 0xCC, 0x3E, 0x01, 0xF7, 0x12,
			0x5E, 0x00, 0xF7, 0xED, 0x6A, 0x00, 0xF9, 0xAE, 0xBE, 0x03, 0xFD, 0x67, 0x64, 0x03, 0x01, 0x00, 0x02, 0x01, 0x00, 0x00, 0x8A, 0x83, 0x85, 0x00,
			0x01, 0x00, 0x03, 0x01, 0x00, 0x00, 0x8A, 0x83, 0x85, 0x00, 0x2F, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x47, 0xF1, 0x00, 0x01, 0x91, 0x40, 0x01,
			0x02, 0x1C, 0x41, 0x01, 0x02, 0x35, 0x4F, 0x01, 0x02, 0xBD, 0x7E, 0x00, 0x04, 0x92, 0x1E, 0x01, 0x0A, 0xEF, 0x59, 0x02, 0x12, 0xB9, 0x5D, 0x03,
			0x18, 0x1F, 0x1B, 0x01, 0x19, 0xC3, 0x98, 0x00, 0x1B, 0x77, 0x98, 0x01, 0x1E, 0x51, 0x3D, 0x03, 0x22, 0x7A, 0x4C, 0x03, 0x29, 0x00, 0xD8, 0x00,
			0x29, 0x55, 0x63, 0x03, 0x2E, 0x53, 0x4C, 0x01, 0x42, 0x93, 0x80, 0x00, 0x4D, 0x86, 0x3E, 0x03, 0x4F, 0x6B, 0x68, 0x03, 0x60, 0xAA, 0x56, 0x01,
			0x62, 0x8E, 0x74, 0x03, 0x63, 0x3E, 0x99, 0x00, 0x64, 0x47, 0xEF, 0x00, 0x65, 0x7A, 0x4C, 0x03, 0x70, 0x2C, 0x48, 0x03, 0x74, 0x85, 0xAD, 0x03,
			0x75, 0x9F, 0xBE, 0x02, 0x79, 0xA8, 0x79, 0x03, 0x82, 0xE6, 0xF4, 0x00, 0x87, 0x27, 0x9C, 0x02, 0x8B, 0x52, 0xCB, 0x00, 0x9B, 0x56, 0xA4, 0x00,
			0x9C, 0xA5, 0x53, 0x03, 0x9F, 0x60, 0xC3, 0x00, 0xA3, 0xB0, 0x6D, 0x03, 0xA7, 0xC2, 0x33, 0x01, 0xAA, 0x9F, 0xCE, 0x02, 0xAC, 0x84, 0x0E, 0x01,
			0xAF, 0xEF, 0xC9, 0x00, 0xB4, 0x89, 0x22, 0x01, 0xB5, 0x7A, 0x48, 0x01, 0xB6, 0x51, 0x5D, 0x00, 0xCC, 0xE1, 0x17, 0x03, 0xDA, 0x19, 0xD7, 0x00,
			0xE6, 0x19, 0x9B, 0x00, 0xE8, 0xA4, 0xC6, 0x01, 0xFC, 0xA5, 0x3B, 0x03, 0x04, 0x00, 0x40, 0x01, 0x00, 0x00, 0x4B, 0x11, 0xB4, 0x00, 0x7B, 0xA8,
			0xD1, 0x00, 0x8A, 0x83, 0x85, 0x00, 0x9F, 0x27, 0xFF, 0x00, 0x01, 0x00, 0x43, 0x01, 0x00, 0x00, 0xC0, 0xEC, 0x7C, 0x00,
			};
			if ( !user_buffer( c.args[ 3 ], 4, UC_PROT_WRITE ) || !user_buffer( c.args[ 5 ], 4, UC_PROT_WRITE ) || ( blen && !user_buffer( c.args[ 4 ], sizeof( state4 ), UC_PROT_WRITE ) ) )
				return done_( status::access_violation, what + ": an OUT buffer is not writable" );
			const uint32_t one = 1, n4 = sizeof( state4 );
			if ( !blen )
			{
				reply r0 = done_( status::buffer_too_small, what + ": STATUS_BUFFER_TOO_SMALL, BufferLength 44CH, ChangeStamp written (measured: tm_nt wnf4.0)" );
				if ( !put( c.args[ 3 ], &one, 4, r0 ) || !put( c.args[ 5 ], &n4, 4, r0 ) ) return stop_( "writing the result failed after the probe" );
				return r0;
			}
			reply r4 = done_( status::success, what + ": 44CH data bytes (the measured WIL feature configuration), BufferLength 44CH, ChangeStamp 1 (placeholder: non-zero) (measured: tm_nt wnf4.1000)" );
			if ( !put( c.args[ 4 ], state4, sizeof( state4 ), r4 ) || !put( c.args[ 3 ], &one, 4, r4 ) || !put( c.args[ 5 ], &n4, 4, r4 ) ) return stop_( "writing the result failed after the probe" );
			return r4;
		}
		if ( !c.stack_args || name != 0x0280032EA3BC0875ull || c.args[ 2 ] || !c.args[ 3 ] || !c.args[ 5 ] ) return stop_( what + " - not measured (tm_nt wnf*: the measured states, no scope)" );
		if ( !c.args[ 1 ] ) return done_( status::invalid_parameter, what + ": TypeId NULL - STATUS_INVALID_PARAMETER (measured: tm_nt wnf.notype)" );
		if ( tid[ 0 ] != 0x4E8552D648F96463ull || tid[ 1 ] != 0x4E26174EC1F2029Cull ) return stop_( what + " - another TypeId: not measured" );
		if ( !user_buffer( c.args[ 3 ], 4, UC_PROT_WRITE ) || !user_buffer( c.args[ 5 ], 4, UC_PROT_WRITE ) || ( blen >= 8 && !user_buffer( c.args[ 4 ], 8, UC_PROT_WRITE ) ) )
			return done_( status::access_violation, what + ": an OUT buffer is not writable" );
		const uint32_t stamp = 1, eight = 8;
		const uint8_t data[ 8 ] = { 1, 0, 0, 0, 0, 0, 0, 0 };
		if ( blen < 8 )
		{
			reply rs = done_( status::buffer_too_small, what + ": STATUS_BUFFER_TOO_SMALL, BufferLength 8, ChangeStamp written (measured: tm_nt wnf.0)" );
			if ( !put( c.args[ 3 ], &stamp, 4, rs ) || !put( c.args[ 5 ], &eight, 4, rs ) ) return stop_( "writing the result failed after the probe" );
			return rs;
		}
		reply r = done_( status::success, what + ": 8 data bytes 01 00 00 00 00 00 00 00, BufferLength 8, ChangeStamp 1 (placeholder: non-zero) (measured: tm_nt wnf.1000)" );
		if ( !put( c.args[ 4 ], data, 8, r ) || !put( c.args[ 3 ], &stamp, 4, r ) || !put( c.args[ 5 ], &eight, 4, r ) ) return stop_( "writing the result failed after the probe" );
		return r;
	}

	// P41 (U2078): NtSetWnfProcessNotificationEvent( NotificationEvent ) - phnt ntexapi.h:2005-2007. MEASURED (tm_nt wnfe.*): in a process
	// whose own start already set one, a new synchronization event = STATUS_ALREADY_REGISTERED (C0000718H), again the same; NULL =
	// STATUS_INVALID_HANDLE. So the process' FIRST call (ntdll's, during kernelbase's start) succeeds - inferred, as for ETW 1BH. The event
	// is never signaled here (no WNF state changes in the emulated environment).
	// NtSubscribeWnfStateChange( StateName, ChangeStamp, EventMask, *SubscriptionId ) - phnt ntexapi.h:1946-1951 (below).
	reply kernel::svc_wnf_more( const call& c )
	{
		if ( c.name == "NtSetWnfProcessNotificationEvent" )
		{
			const auto it = handles_.find( c.args[ 0 ] );
			const std::string what = "NtSetWnfProcessNotificationEvent( " + hex( c.args[ 0 ] ) + ( it != handles_.end() ? " (" + it->second.kind + ")" : std::string() ) + " )";
			if ( !c.args[ 0 ] ) return done_( status::invalid_handle, what + ": NULL - STATUS_INVALID_HANDLE (measured: tm_nt wnfe.null)" );
			if ( it == handles_.end() || it->second.kind != "event (synchronization)" ) return stop_( what + " - not a synchronization event: not measured" );
			if ( wnf_event_ ) return done_( 0xC0000718, what + ": already set (event " + hex( wnf_event_ ) + ") - STATUS_ALREADY_REGISTERED (measured: tm_nt wnfe.set / .again)" );
			wnf_event_ = c.args[ 0 ];
			return done_( status::success, what + ": the process' WNF notification event (first call: STATUS_SUCCESS - inferred from tm_nt wnfe.set: a later one is STATUS_ALREADY_REGISTERED); never signaled here" );
		}
		// NtSubscribeWnfStateChange - MEASURED (tm_nt wnfs.*): the state 0280032EA3BC0875H, EventMask 11H, the ChangeStamp of the query (or
		// another) = STATUS_SUCCESS, SubscriptionId written non-zero, the SAME id for a second subscription of that state in the process.
		// The id: placeholder 1 (the shape: non-zero, stable per state). Other states / masks: stop.
		uint64_t name = 0;
		std::string err;
		if ( c.args[ 0 ] && user_buffer( c.args[ 0 ], 8, UC_PROT_READ ) ) e_.read( c.args[ 0 ], &name, 8, err );
		const std::string what = "NtSubscribeWnfStateChange( StateName " + hex( c.args[ 0 ] ) + " = " + hex( name ) + ", ChangeStamp " + hex( uint32_t( c.args[ 1 ] ) ) + ", EventMask " +
								 hex( uint32_t( c.args[ 2 ] ) ) + ", SubscriptionId " + hex( c.args[ 3 ] ) + " )";
		// tm_nt wnfs.two / wnfs.three: the second / third state - STATUS_SUCCESS, an id of its own each
		const uint64_t id = name == 0x0280032EA3BC0875ull ? 1 : name == 0x418A073AA3BC7C75ull ? 2 : name == 0x418A073AA3BC88F5ull ? 3 : 0;
		if ( !id || uint32_t( c.args[ 2 ] ) != 0x11 || !c.args[ 3 ] )
			return stop_( what + " - not measured (tm_nt wnfs: the states 0280032EA3BC0875H / 418A073AA3BC7C75H / 418A073AA3BC88F5H, 11H, an id pointer)" );
		if ( !user_buffer( c.args[ 3 ], 8, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": SubscriptionId not writable" );
		reply r = done_( status::success, what + ": subscribed, SubscriptionId " + std::to_string( id ) + " (placeholder: non-zero, stable per state, one per state; measured: tm_nt wnfs.*); "
										  "no change is ever delivered here" );
		if ( !put( c.args[ 3 ], &id, 8, r ) ) return stop_( "writing the result failed after the probe" );
		return r;
	}

	// NtSetTimer2( TimerHandle, DueTime, Period, Parameters ) - phnt ntexapi.h:1470-1475 (T2_SET_PARAMETERS_V0 :1430-1450).
	reply kernel::svc_set_timer2( const call& c )
	{
		const auto it = handles_.find( c.args[ 0 ] );
		int64_t due = 0, period = 0;
		uint8_t prm[ 16 ] = {};
		std::string err, ph;
		if ( c.args[ 1 ] && user_buffer( c.args[ 1 ], 8, UC_PROT_READ ) ) e_.read( c.args[ 1 ], &due, 8, err );
		if ( c.args[ 2 ] && user_buffer( c.args[ 2 ], 8, UC_PROT_READ ) ) e_.read( c.args[ 2 ], &period, 8, err );
		if ( c.args[ 3 ] && user_buffer( c.args[ 3 ], 16, UC_PROT_READ ) ) e_.read( c.args[ 3 ], prm, 16, err );
		for ( int i = 0; i < 16; i++ ) { char b[ 4 ]; std::snprintf( b, sizeof( b ), "%02X", prm[ i ] ); ph += ( i ? " " : "" ) + std::string( b ); }
		const std::string what = "NtSetTimer2( " + hex( c.args[ 0 ] ) + ( it != handles_.end() ? " (" + it->second.kind + ")" : std::string() ) + ", DueTime " + hex( c.args[ 1 ] ) + " = " +
								 std::to_string( due ) + ", Period " + hex( c.args[ 2 ] ) + " = " + std::to_string( period ) + ", Parameters " + hex( c.args[ 3 ] ) + " [" + ph + "] )";
		// P41 (U2079): MEASURED (tm_nt t2s.*): a timer2 of NtCreateTimer2 (Attributes 8), a RELATIVE DueTime (negative; the loader's
		// 300 s relative, in 100-ns units), Period NULL, Parameters {Version 0, NoWakeTolerance 75 s} = STATUS_SUCCESS (again: STATUS_SUCCESS), the
		// DueTime not written. The timer is armed but never expires here: expiry needs the virtual time base (milestone M6) and the emulated
		// run waits on nothing that it would signal. Other shapes: stop.
		uint32_t ver = 0;
		std::memcpy( &ver, prm, 4 );
		if ( it == handles_.end() || it->second.kind != "timer2" || !c.args[ 1 ] || due >= 0 || c.args[ 2 ] || !c.args[ 3 ] || ver != 0 )
			return stop_( what + " - not measured (tm_nt t2s: a timer2, a relative DueTime, no Period, Parameters Version 0)" );
		it->second.value = uint64_t( due );
		return done_( status::success, what + ": armed (relative " + std::to_string( -due / 10000000 ) + " s; never expires in the emulated run - virtual time is M6) (measured: tm_nt t2s.set)" );
	}

	// P45 (U2137): the atom table - NtAddAtomEx( AtomName, Length, Atom, Flags ) phnt ntexapi.h:10140-10146, NtFindAtom( AtomName, Length, Atom )
	// :10163, NtDeleteAtom( Atom ) :10183, NtQueryInformationAtom( Atom, AtomInformationClass, AtomInformation, Length, ReturnLength )
	// (AtomBasicInformation 0 {USHORT UsageCount, USHORT Flags, USHORT NameLength, WCHAR Name[]}). Sample exploration x3 system call 599:
	// dxgi's DllMain -> kernel32 GlobalAddAtomW -> NtAddAtomEx( a 27-character name, 36H, &atom, Flags 0 ). MEASURED (tm_nt atom.*, a name
	// of its own): in a process whose user32 is NOT loaded = STATUS_ACCESS_DENIED, the atom not written (atom.nouser32); once user32 is
	// loaded (its start connects the process to its window station - here: the USERSRV connection of user32's DllMain was made, U2119):
	// add = STATUS_SUCCESS, an atom in C000H..FFFFH; the same name again = the same atom (also with Atom NULL); the query = STATUS_SUCCESS,
	// ReturnLength 6 + NameLength + 2, UsageCount = the adds, Flags 0, NameLength 2 x characters, the name and a NUL; NtFindAtom = the same
	// atom; each NtDeleteAtom = STATUS_SUCCESS (one per add), then NtFindAtom = STATUS_OBJECT_NAME_NOT_FOUND, the atom not written. The model
	// is the table of the emulated window station holding only the atoms this process added (the session's other atoms are not measured);
	// the atom value is a placeholder of the measured range (C100H + n; Windows' value depends on the atoms the session holds). Not measured -
	// stop: Flags other than 0, an integer atom ("#..."), a name differing only in case from a held one, another class / length, an atom
	// this process did not add.
	reply kernel::svc_add_atom( const call& c )
	{
		const bool add = c.name == "NtAddAtomEx", find = c.name == "NtFindAtom";
		auto read_name = [ & ]( uint64_t pn, uint64_t len, std::string& name, std::vector<uint16_t>& w ) {
			if ( !len || len > 0x1FE || ( len & 1 ) || !user_buffer( pn, len, UC_PROT_READ ) ) return false;
			w.assign( size_t( len / 2 ), 0 );
			std::string err;
			e_.read( pn, w.data(), len, err );
			name.clear();
			for ( uint16_t ch : w ) name += ( ch >= 0x20 && ch < 0x7F ) ? char( ch ) : '?';
			return true;
		};
		if ( add || find )
		{
			if ( !c.stack_args && add ) return stop_( c.name + ": the stack arguments are not readable" );
			const uint64_t pn = c.args[ 0 ], len = uint32_t( c.args[ 1 ] ), pa = c.args[ 2 ];
			const uint32_t flags = add ? uint32_t( c.args[ 3 ] ) : 0;
			std::string name;
			std::vector<uint16_t> w;
			const bool ok = read_name( pn, len, name, w );
			const std::string what = c.name + "( \"" + name + "\" (" + hex( len ) + " bytes), Atom " + hex( pa ) + ( add ? ", Flags " + hex( flags ) : std::string() ) + " )";
			if ( !ok ) return stop_( what + " - a name that is empty, longer than 255 characters or not readable: not measured" );
			if ( flags ) return stop_( what + " - Flags other than 0: not measured (tm_nt atom: 0)" );
			if ( name[ 0 ] == '#' ) return stop_( what + " - an integer atom: not measured" );
			if ( name.find( '?' ) != std::string::npos ) return stop_( what + " - a character outside printable ASCII (or '?'): not modelled" );
			if ( pa && !user_buffer( pa, 2, UC_PROT_WRITE ) ) return stop_( what + " - the Atom pointer is not writable: not measured" );
			if ( !user_connected_ && find ) return stop_( what + " - before user32's start: not measured (only NtAddAtomEx is: atom.nouser32)" );
			if ( !user_connected_ ) return done_( 0xC0000022, what + ": the process is not connected to a window station (user32 not started) - STATUS_ACCESS_DENIED, the atom not written (measured: tm_nt atom.nouser32)" );
			auto it = atoms_.begin();
			for ( ; it != atoms_.end(); ++it )
				if ( lower( it->second.name ) == lower( name ) ) break;
			if ( it != atoms_.end() && it->second.name != name ) return stop_( what + " - a name differing only in case from a held atom: not measured" );
			if ( find )
			{
				if ( it == atoms_.end() ) return done_( status::object_name_not_found, what + ": no such atom - STATUS_OBJECT_NAME_NOT_FOUND, the atom not written (measured: tm_nt atom.gone)" );
				reply rf = done_( status::success, what + ": atom " + hex( it->first ) + " (measured: tm_nt atom.find)" );
				const uint16_t av = uint16_t( it->first );
				if ( pa && !put( pa, &av, 2, rf ) ) return stop_( "writing the atom failed after the probe" );
				return rf;
			}
			uint32_t av = 0;
			if ( it != atoms_.end() )
			{
				it->second.usage++;
				av = it->first;
			}
			else
			{
				av = 0xC100 + uint32_t( atoms_next_++ );
				if ( av > 0xFFFF ) return stop_( what + " - more atoms than the placeholder range holds" );
				atoms_[ av ] = { name, 1 };
			}
			reply ra = done_( status::success, what + ": atom " + hex( av ) + " (a placeholder of the measured range C000H..FFFFH), usage " + std::to_string( atoms_[ av ].usage ) +
												   " (measured: tm_nt atom.add1 / add2 / add3)" );
			const uint16_t a16 = uint16_t( av );
			if ( pa && !put( pa, &a16, 2, ra ) ) return stop_( "writing the atom failed after the probe" );
			return ra;
		}
		const uint32_t av = uint16_t( c.args[ 0 ] );
		const auto it = atoms_.find( av );
		const std::string what = c.name + "( " + hex( av ) + ( it != atoms_.end() ? " \"" + it->second.name + "\"" : std::string() ) + " )";
		if ( it == atoms_.end() ) return stop_( what + " - an atom this process did not add: not measured" );
		if ( c.name == "NtDeleteAtom" )
		{
			if ( --it->second.usage == 0 ) atoms_.erase( it );
			return done_( status::success, what + ": STATUS_SUCCESS (one reference less; measured: tm_nt atom.del*)" );
		}
		// NtQueryInformationAtom
		if ( !c.stack_args ) return stop_( what + ": the stack argument ReturnLength is not readable" );
		const uint32_t cls = uint32_t( c.args[ 1 ] ), len = uint32_t( c.args[ 3 ] );
		const uint64_t out = c.args[ 2 ], pret = c.args[ 4 ];
		const uint32_t nl = uint32_t( it->second.name.size() * 2 );
		if ( cls != 0 || len < 6 + nl + 2 || len > 0x1000 ) return stop_( what + " - class " + std::to_string( cls ) + ", Length " + hex( len ) + ": not measured (tm_nt atom.query: 0, 100H)" );
		if ( !user_buffer( out, 6 + nl + 2, UC_PROT_WRITE ) || ( pret && !user_buffer( pret, 4, UC_PROT_WRITE ) ) ) return stop_( what + " - an output pointer is not writable: not measured" );
		std::vector<uint8_t> b( 6 + nl + 2, 0 );
		const uint16_t hdr[ 3 ] = { uint16_t( it->second.usage ), 0, uint16_t( nl ) };
		std::memcpy( b.data(), hdr, 6 );
		for ( size_t i = 0; i < it->second.name.size(); i++ ) b[ 6 + 2 * i ] = uint8_t( it->second.name[ i ] );
		const uint32_t rl = 6 + nl + 2;
		reply rq = done_( status::success, what + ": UsageCount " + std::to_string( it->second.usage ) + ", Flags 0, NameLength " + hex( nl ) + ", the name and a NUL, ReturnLength " + hex( rl ) +
											  " (measured: tm_nt atom.query)" );
		if ( !put( out, b.data(), b.size(), rq ) || ( pret && !put( pret, &rl, 4, rq ) ) ) return stop_( "writing the result failed after the probe" );
		return rq;
	}

	// P45 (U2135): NtReadVirtualMemory( ProcessHandle, BaseAddress, Buffer, NumberOfBytesToRead, NumberOfBytesRead ) - phnt ntmmapi.h:840-846
	// (the service of the documented ReadProcessMemory: "Copies the data in the specified address range from the address space of the
	// specified process into the specified buffer"). Sample exploration x1 system call 573: NtCurrentProcess(), PEB + 18H, a stack buffer, 8,
	// &read. MEASURED (tm_nt rvm.*, this process' own memory): a readable source into a writable destination = STATUS_SUCCESS, the bytes
	// copied, *NumberOfBytesRead = the size (ldr; NumberOfBytesRead NULL: the same, nothing else written - ldr.nocount); 0 bytes =
	// STATUS_SUCCESS, count 0 (zero); a source that is reserved only / PAGE_NOACCESS / readable for its first 4 bytes only, or a read-only
	// destination = STATUS_PARTIAL_COPY (8000000DH), count 0, NOTHING copied (reserved / noaccess / edge / rodst). Another process handle, a
	// destination writable in part, an unwritable count pointer, more than 16 MiB: not measured - stop.
	reply kernel::svc_read_virtual_memory( const call& c )
	{
		if ( !c.stack_args ) return stop_( "NtReadVirtualMemory: the stack argument NumberOfBytesRead is not readable" );
		const uint64_t src = c.args[ 1 ], dst = c.args[ 2 ], n = c.args[ 3 ], pcount = c.args[ 4 ];
		const std::string what = "NtReadVirtualMemory( " + hex( c.args[ 0 ] ) + ", " + hex( src ) + ", " + hex( dst ) + ", " + hex( n ) + ", " + hex( pcount ) + " )";
		if ( c.args[ 0 ] != current_process ) return stop_( what + " - another process: not measured (tm_nt rvm: NtCurrentProcess())" );
		if ( pcount && !user_buffer( pcount, 8, UC_PROT_WRITE ) ) return stop_( what + " - NumberOfBytesRead is not writable: not measured" );
		if ( n > ( 16ull << 20 ) ) return stop_( what + " - more than 16 MiB: not measured" );
		const uint64_t zero = 0;
		if ( !n )
		{
			reply rz = done_( status::success, what + ": 0 bytes - STATUS_SUCCESS, count 0 (measured: tm_nt rvm.zero)" );
			if ( pcount && !put( pcount, &zero, 8, rz ) ) return stop_( "writing the count failed after the probe" );
			return rz;
		}
		const bool src_ok = user_buffer( src, n, UC_PROT_READ ), dst_ok = user_buffer( dst, n, UC_PROT_WRITE );
		if ( !dst_ok && dst <= UINT64_MAX - n )
		{
			// a destination writable in part only was not measured (rodst: wholly read-only)
			for ( uint64_t pg = dst & ~0xFFFull; pg < dst + n; pg += 0x1000 )
				if ( user_buffer( std::max( pg, dst ), std::min( pg + 0x1000, dst + n ) - std::max( pg, dst ), UC_PROT_WRITE ) )
					return stop_( what + " - a destination writable in part: not measured (tm_nt rvm.rodst: wholly read-only)" );
		}
		if ( !src_ok || !dst_ok )
		{
			reply rp = done_( 0x8000000D, what + ": the " + std::string( !src_ok ? "source is not readable (in whole or in part)" : "destination is not writable" ) +
											  " - STATUS_PARTIAL_COPY, count 0, nothing copied (measured: tm_nt rvm." + ( !src_ok ? "reserved / noaccess / edge" : "rodst" ) + ")" );
			if ( pcount && !put( pcount, &zero, 8, rp ) ) return stop_( "writing the count failed after the probe" );
			return rp;
		}
		std::vector<uint8_t> b( size_t( n ), 0 );
		std::string err;
		if ( !e_.read( src, b.data(), n, err ) ) return stop_( what + " - reading the source failed after the probe: " + err );
		reply rr = done_( status::success, what + ": " + std::to_string( n ) + " bytes copied, STATUS_SUCCESS (measured: tm_nt rvm.ldr" + ( pcount ? "" : ".nocount" ) + ")" );
		if ( !put( dst, b.data(), n, rr ) ) return stop_( "writing the destination failed after the probe" );
		if ( pcount && !put( pcount, &n, 8, rr ) ) return stop_( "writing the count failed after the probe" );
		return rr;
	}
}
