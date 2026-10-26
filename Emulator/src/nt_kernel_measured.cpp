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
		if ( c.name == "NtQuerySystemInformationEx" )
		{
			r = svc_query_system_information_ex( c );
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
