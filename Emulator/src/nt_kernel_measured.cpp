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
		if ( c.name == "NtCreateFile" ) return svc_create_file_measured( c, r );
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
		if ( c.args[ 4 ] != current_process || uint32_t( c.args[ 7 ] ) != 0x200 || c.args[ 8 ] != 0x100000 || c.args[ 9 ] != 0x1000 )
			return stop_( "NtCreateWorkerFactory( " + a + " ) - not measured (tm_nt wkf: NtCurrentProcess(), 200H, 100000H, 1000H)" );
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
			{ 5, 0, 0x200, status::info_length_mismatch, "WorkerFactoryThreadMaximum" } };
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
				{ { 0xFA, 0x2A, 0x1D, 0x90, 0xF6, 0x4F, 0xD7, 0x46, 0x8D, 0x0E, 0x53, 0x64, 0x5E, 0x1A, 0x47, 0xF5 }, 3, true, "tm_nt etw.reg3" } };
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
			if ( !hit ) return stop_( what + " - EtwRegisterGuidsCode for provider bytes " + guid + " / another input: not measured (the provider's enable state is the environment's)" );
			if ( c.args[ 5 ] && !user_buffer( c.args[ 5 ], 4, UC_PROT_WRITE ) ) return done_( status::access_violation, what + ": ReturnLength not writable" );
			object o;
			o.kind = "etw registration";
			o.name = guid;
			const uint64_t h = open_handle( o );
			reply rr = done_( status::success, what + ": provider " + guid + " registered, ReplyHandle " + hex( h ) + ", not enabled (measured: " + hit->tag + ")" );
			const uint32_t a0 = 0xA0;
			if ( !put( c.args[ 1 ] + 0x18, &h, 8, rr ) || !put( c.args[ 1 ] + 0x2C, &a0, 4, rr ) || ( c.args[ 5 ] && !put( c.args[ 5 ], &a0, 4, rr ) ) )
				return stop_( "writing the result failed after the probe" );
			return rr;
		}
		if ( fn != 0x1B || c.args[ 3 ] || olen ) return stop_( what + " - not measured" );
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
			if ( lower( full ) != "\\knowndlls" || access != 3 ) return stop_( what + " - not measured (tm_nt kd.open: \"\\KnownDlls\", 3)" );
			o.kind = "directory";
			o.name = "\\KnownDlls";
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
	// (emu::pe::map, relocated) at the environment's fixed slot of that DLL (kernel32 7FFB10000000H, kernelbase 7FFB20000000H - the
	// --HLE layout; Windows: the boot-time ASLR base), the header's ImageBase set to that base, registered as a MEM_IMAGE view. Other
	// sections / arguments / DLLs without a slot or capture file: stop.
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
		uint64_t slot = 0;
		if ( file == "kernel32.dll" ) slot = 0x7FFB10000000ull;
		else if ( file == "kernelbase.dll" ) slot = 0x7FFB20000000ull;
		if ( !slot || !f_.files || !f_.files->count( file ) ) return stop_( what + " - " + file + " has no slot / is not in the environment's capture (capture_sysdlls.py --dll)" );
		for ( const auto& [ vb, v ] : views_ )
			if ( v.base == slot ) return stop_( what + " - a second view of " + file + " (measured: STATUS_IMAGE_AT_DIFFERENT_BASE) - not modelled" );
		const std::vector<uint8_t>& bytes = f_.files->at( file );
		pe::image img;
		pe::mapping mp;
		std::string err;
		if ( !pe::parse( bytes, img, err ) || !pe::map( e_, bytes, img, slot, file, mp, err ) ) return stop_( what + " - mapping the capture's " + file + " failed: " + err );
		// the view's header ImageBase = the base (IMAGE_OPTIONAL_HEADER64.ImageBase at e_lfanew + 18H + 18H, um\winnt.h)
		if ( !e_.write( slot + img.e_lfanew + 0x18 + 0x18, &slot, 8, err ) ) return stop_( what + " - writing the header ImageBase failed: " + err );
		if ( !add_image_view( slot, mp.size, file, err ) ) return stop_( what + " - " + err );
		reply rr = done_( status::success, what + ": " + file + " (the capture's file) mapped at " + hex( slot ) + ", ViewSize " + hex( img.size_of_image ) +
											   ( mp.relocated ? ", relocated (" + std::to_string( mp.relocations ) + " fixups)" : "" ) + ", header ImageBase = the base (measured: tm_nt kd.map)" );
		const uint64_t vs = img.size_of_image;
		if ( !put( pbase, &slot, 8, rr ) || !put( psize, &vs, 8, rr ) ) return stop_( "writing the result failed after the probe" );
		return rr;
	}

	// NtOpenFile( *FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions ) - phnt ntioapi.h:2294-2301 (the
	// WDK's ZwOpenFile documents the call). The emulated file system holds exactly what the environment presents: the process image's
	// directory (the CurrentDirectory of its parameters) - every other path is a loud stop until the environment holds it. MEASURED
	// (tm_nt of.*): the loader's call - access 100020H, "\??\<current directory>\" OBJ_CASE_INSENSITIVE, ShareAccess 3, OpenOptions 21H -
	// = STATUS_SUCCESS, a handle (non-zero, multiple of 4), IoStatus {STATUS_SUCCESS, Information 1 = FILE_OPENED}; NtClose =
	// STATUS_SUCCESS; a directory that does not exist = STATUS_OBJECT_NAME_NOT_FOUND, nothing written (not used: no path outside the
	// environment is answered "absent" - it stops).
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
		if ( !curdir ) return stop_( what + " - the path is not in the emulated file system (it holds the image's directory only) - not measured" );
		if ( access != 0x100020 || share != 3 || options != 0x21 || !( oa.attributes & 0x40 ) ) return stop_( what + " - the image directory with other arguments: not measured (tm_nt of.curdir)" );
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
		// modules in the guest). Other names (an entry may exist for them in the environment's database): stop until measured; other classes
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
		if ( ln != "kernelbase.dll" && ln != "kernel32.dll" && ln != "ntdll.dll" ) return stop_( what + " - this module name is not measured (tm_nt ahc: kernelbase.dll, kernel32.dll, ntdll.dll)" );
		if ( !user_buffer( pout, 4, UC_PROT_WRITE ) ) return stop_( what + " - ParamsOut not writable: not measured" );
		reply rr = done_( status::success, what + ": no compatibility-database entry - ParamsOut = 0 (measured: tm_nt ahc)" );
		const uint32_t zero = 0;
		if ( !put( pout, &zero, 4, rr ) ) return stop_( "writing ParamsOut failed after the probe" );
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
		if ( cls != 0xC5 && cls != 0x37 && cls != 0xC0 && cls != 0x32 ) return false;
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
}
