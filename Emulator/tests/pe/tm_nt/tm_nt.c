/* tm_nt.c - test PE "tm_nt" (plan 3.M5 --Sys, P39; ledger U2050): the NATIVE MEASUREMENT of the undocumented system services the
 * real ntdll loader calls under emu-oep --Sys --loader (USER DECISION 2026-10-25, plan line 3.M5: prototypes from System Informer's
 * MIT-licensed phnt headers, behaviour measured by running OUR OWN self-written test PEs natively in the VMware guest - decision
 * 3.0.7). Self-written, never derived from the sample. The emulator never runs or links this program; the harness runs it natively
 * as a labelled reference and compares its output with Emulator/data/nt_measured_vmware_guest.txt.
 *
 * Every case calls ONE service with the arguments the loader used in the emulated trace (named per case) and prints one line per
 * call: the status, the ReturnLength, how many output bytes were written (the buffer is pre-filled with CCH) and the output's
 * SHAPE (sizes, counts, types, flags, buffer-relative offsets). Real identifiers are never printed: a value that could identify
 * the machine, the user or the token (SIDs, names, paths, LUIDs, handle values, addresses, attribute values) is replaced by a
 * placeholder of the same shape (e.g. "<str:16>" = a 16-byte string, "<sid>", "<ptr:+18H>" = a pointer to buffer offset 18H) -
 * each case names what it placeholders.
 *
 * Declarations: our own, from the prototypes in phnt (E:\sources\oss\phnt-4.0.26241.138\src\systeminformer-4.0.26241.138\phnt\
 * include, MIT; reference only, cited per declaration); types from the Windows SDK (um\winnt.h, shared\ntdef.h). Linked against the
 * SDK's ntdll.lib (the import library only).
 *
 * usage: tm_nt.exe [CASE ...]   (no argument = every case, in table order); exit 0 = every case ran, 2 = unknown case
 *        tm_nt.exe run.detached IMAGE   (P41: our own test PE IMAGE started detached; prints its exit code)
 *        tm_nt.exe run.normal IMAGE     (P44: our own test PE IMAGE started normally, no creation flags; prints its exit code) */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

typedef LONG TM_NTSTATUS;
typedef struct { USHORT Length; USHORT MaximumLength; PWSTR Buffer; } TM_USTR;   /* shared\ntdef.h UNICODE_STRING */

/* phnt ntseapi.h:947-954 */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQuerySecurityAttributesToken( HANDLE TokenHandle, const TM_USTR* Attributes, ULONG NumberOfAttributes,
																		  PVOID Buffer, ULONG Length, PULONG ReturnLength );

/* phnt ntexapi.h:8339-8344 */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQuerySystemInformation( ULONG SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength,
																	PULONG ReturnLength );

#define TM_CURRENT_PROCESS_TOKEN ( (HANDLE)(LONG_PTR)-4 ) /* phnt ntpsapi.h:2489 NtCurrentProcessToken() */

static unsigned char g_buf[ 0x10000 ];

static void tm_ustr( TM_USTR* u, const wchar_t* s )
{
	u->Buffer = (PWSTR)s;
	u->Length = (USHORT)( wcslen( s ) * 2 );
	u->MaximumLength = (USHORT)( u->Length + 2 );
}

static size_t tm_written( size_t len )
{
	size_t n = 0, i;
	for ( i = 0; i < len; i++ )
		if ( g_buf[ i ] != 0xCC ) n = i + 1; /* the highest byte that changed */
	return n;
}

/* the shape of a pointer into the buffer: "<ptr:+XH>", NULL, or "<ptr:outside>" (a value outside the buffer is never printed) */
static void tm_ptr( char* out, size_t cap, const void* p )
{
	const unsigned char* q = (const unsigned char*)p;
	if ( !p ) snprintf( out, cap, "NULL" );
	else if ( q >= g_buf && q < g_buf + sizeof( g_buf ) ) snprintf( out, cap, "<ptr:+%XH>", (unsigned)( q - g_buf ) );
	else snprintf( out, cap, "<ptr:outside>" );
}

/* ------------------------------------------------------------------------------------------------ NtQuerySecurityAttributesToken
 * TOKEN_SECURITY_ATTRIBUTES_INFORMATION {USHORT Version, USHORT Reserved, ULONG AttributeCount, PTOKEN_SECURITY_ATTRIBUTE_V1} (phnt
 * ntseapi.h:416-424), TOKEN_SECURITY_ATTRIBUTE_V1 {UNICODE_STRING Name, USHORT ValueType, USHORT Reserved, ULONG Flags, ULONG
 * ValueCount, PVOID Values} (ntseapi.h:375-391). Placeholders: attribute VALUES (any type) as "<value:TYPE>" - an attribute name is a
 * fixed system string (e.g. WIN://SYSAPPID) and printed; no attribute value is printed. */
static void tm_sat( const char* tag, const wchar_t* const* names, ULONG n, ULONG len )
{
	TM_USTR u[ 8 ];
	ULONG i, ret = 0xCCCCCCCC;
	TM_NTSTATUS st;
	char line[ 2048 ];
	size_t w;
	for ( i = 0; i < n; i++ ) tm_ustr( &u[ i ], names[ i ] );
	memset( g_buf, 0xCC, sizeof( g_buf ) );
	st = NtQuerySecurityAttributesToken( TM_CURRENT_PROCESS_TOKEN, n ? u : NULL, n, len ? g_buf : NULL, len, &ret );
	w = tm_written( sizeof( g_buf ) );
	snprintf( line, sizeof( line ), "tm_nt %s: NtQuerySecurityAttributesToken(NtCurrentProcessToken(), %lu name(s), Length %lXH) -> status %08lXH, ReturnLength %lXH, written %zXH", tag,
			  (unsigned long)n, (unsigned long)len, (unsigned long)st, (unsigned long)ret, w );
	if ( st >= 0 && len >= 16 )
	{
		const USHORT ver = *(USHORT*)( g_buf + 0 ), res = *(USHORT*)( g_buf + 2 );
		const ULONG cnt = *(ULONG*)( g_buf + 4 );
		char p[ 64 ];
		tm_ptr( p, sizeof( p ), *(void**)( g_buf + 8 ) );
		snprintf( line + strlen( line ), sizeof( line ) - strlen( line ), "; Version %u, Reserved %u, AttributeCount %lu, AttributeV1 %s", ver, res, (unsigned long)cnt, p );
		if ( cnt && *(unsigned char**)( g_buf + 8 ) >= g_buf && *(unsigned char**)( g_buf + 8 ) < g_buf + sizeof( g_buf ) - 40 * 8 )
		{
			const unsigned char* a = *(unsigned char**)( g_buf + 8 );
			for ( i = 0; i < cnt && i < 8; i++, a += 40 )
			{
				char nm[ 128 ], pv[ 64 ];
				const TM_USTR* an = (const TM_USTR*)a;
				int k;
				for ( k = 0; k < an->Length / 2 && k < 120; k++ ) nm[ k ] = ( an->Buffer[ k ] >= 0x20 && an->Buffer[ k ] < 0x7F ) ? (char)an->Buffer[ k ] : '?';
				nm[ k ] = 0;
				tm_ptr( pv, sizeof( pv ), *(void**)( a + 32 ) );
				snprintf( line + strlen( line ), sizeof( line ) - strlen( line ), "; [%lu] \"%s\" ValueType %u Flags %lXH ValueCount %lu Values %s <value:%u>", (unsigned long)i, nm,
						  *(USHORT*)( a + 16 ), (unsigned long)*(ULONG*)( a + 20 ), (unsigned long)*(ULONG*)( a + 24 ), pv, *(USHORT*)( a + 16 ) );
			}
		}
	}
	printf( "%s\n", line );
}

/* the loader's call (emulated trace of T2 / T3 under --Sys --loader, P39 iteration i1, system call 19): TokenHandle NtCurrentProcessToken(),
 * the 2 names in this order, a 330H-byte buffer on the stack, ReturnLength */
static const wchar_t* const sat_loader[] = { L"WIN://SYSAPPID", L"WIN://PKG" };

static void case_sat( void )
{
	static const ULONG lens[] = { 0x330, 0, 8, 0x10, 0x18, 0x1000 };
	size_t i;
	for ( i = 0; i < sizeof( lens ) / sizeof( lens[ 0 ] ); i++ )
	{
		char tag[ 32 ];
		snprintf( tag, sizeof( tag ), "sat.%zu", i );
		tm_sat( tag, sat_loader, 2, lens[ i ] );
	}
	tm_sat( "sat.name0", sat_loader, 1, 0x100 );
	tm_sat( "sat.name1", sat_loader + 1, 1, 0x100 );
	tm_sat( "sat.none", NULL, 0, 0x100 );
}

/* ------------------------------------------------------------------------------------------------ NtQuerySystemInformation
 * Classes the loader asks that no documentation describes (the class names: phnt ntexapi.h SYSTEM_INFORMATION_CLASS, e.g. :2426
 * SystemNumaProcessorMap 37H, :2568 SystemHypervisorSharedPageInformation C5H). Printed per call: status, ReturnLength (pre-set
 * CCCCCCCCH), bytes written (buffer pre-filled CCH) and the class shape:
 *   C5H SYSTEM_HYPERVISOR_SHARED_PAGE_INFORMATION {PVOID HypervisorSharedUserVa} (ntexapi.h:7190-7197): NULL or "<va>" (an address of
 *       this process - placeholder, never printed)
 *   37H SYSTEM_NUMA_INFORMATION {ULONG HighestNodeNumber, ULONG Reserved, GROUP_AFFINITY ActiveProcessorsGroupAffinity[40H]}
 *       (ntexapi.h:3859-3869, MAXIMUM_NODE_COUNT 40H on x64 :3853): HighestNodeNumber, Reserved, and per node 0 .. Highest the
 *       GROUP_AFFINITY {KAFFINITY Mask, WORD Group, WORD Reserved[3]} (um\winnt.h) - processor masks describe the configuration, no identity
 *   otherwise: the first 64 bytes written as hex */
static void tm_qsi( const char* tag, ULONG cls, ULONG len, int retnull )
{
	ULONG ret = 0xCCCCCCCC;
	TM_NTSTATUS st;
	char line[ 4096 ];
	size_t w, k;
	memset( g_buf, 0xCC, sizeof( g_buf ) );
	st = NtQuerySystemInformation( cls, len ? g_buf : NULL, len, retnull ? NULL : &ret );
	w = tm_written( sizeof( g_buf ) );
	snprintf( line, sizeof( line ), "tm_nt %s: NtQuerySystemInformation(%lXH, Length %lXH, ReturnLength %s) -> status %08lXH, ReturnLength %lXH, written %zXH", tag,
			  (unsigned long)cls, (unsigned long)len, retnull ? "NULL" : "set", (unsigned long)st, (unsigned long)ret, w );
	if ( st >= 0 && cls == 0xC5 && len >= 8 )
		snprintf( line + strlen( line ), sizeof( line ) - strlen( line ), "; HypervisorSharedUserVa %s", *(void**)g_buf ? "<va>" : "NULL" );
	else if ( st >= 0 && cls == 0x37 && len >= 8 )
	{
		const ULONG hi = *(ULONG*)g_buf;
		snprintf( line + strlen( line ), sizeof( line ) - strlen( line ), "; HighestNodeNumber %lu, Reserved %lXH", (unsigned long)hi, (unsigned long)*(ULONG*)( g_buf + 4 ) );
		for ( k = 0; k <= hi && k < 0x40 && 8 + 16 * ( k + 1 ) <= len; k++ )
			snprintf( line + strlen( line ), sizeof( line ) - strlen( line ), "; node %zu Mask %llXH Group %u Reserved %u %u %u", k, (unsigned long long)*(ULONGLONG*)( g_buf + 8 + 16 * k ),
					  *(USHORT*)( g_buf + 16 + 16 * k ), *(USHORT*)( g_buf + 18 + 16 * k ), *(USHORT*)( g_buf + 20 + 16 * k ), *(USHORT*)( g_buf + 22 + 16 * k ) );
		{
			/* the bytes after the last listed node up to the written end: all zero? */
			size_t z = 8 + 16 * ( (size_t)hi + 1 ), nz = 0;
			for ( ; z < w && z < len; z++ ) nz += g_buf[ z ] != 0;
			snprintf( line + strlen( line ), sizeof( line ) - strlen( line ), "; non-zero bytes after node %lu: %zu", (unsigned long)hi, nz );
		}
	}
	else if ( st >= 0 )
	{
		strcat( line, "; bytes" );
		for ( k = 0; k < w && k < 64; k++ ) snprintf( line + strlen( line ), sizeof( line ) - strlen( line ), " %02X", g_buf[ k ] );
	}
	printf( "%s\n", line );
}

static void case_qsi( void )
{
	/* the loader's calls (emulated trace P39 i4, system call 34: class C5H, Length 8, ReturnLength NULL; P36 exploration: class 37H,
	 * Length 408H) and the neighbouring lengths */
	tm_qsi( "qsi.c5", 0xC5, 8, 1 );
	tm_qsi( "qsi.c5r", 0xC5, 8, 0 );
	tm_qsi( "qsi.c5.0", 0xC5, 0, 0 );
	tm_qsi( "qsi.c5.4", 0xC5, 4, 0 );
	tm_qsi( "qsi.c5.10", 0xC5, 0x10, 0 );
	tm_qsi( "qsi.37", 0x37, 0x408, 1 );
	tm_qsi( "qsi.37r", 0x37, 0x408, 0 );
	tm_qsi( "qsi.37.0", 0x37, 0, 0 );
	tm_qsi( "qsi.37.8", 0x37, 8, 0 );
	tm_qsi( "qsi.37.18", 0x37, 0x18, 0 );
	tm_qsi( "qsi.37.410", 0x37, 0x410, 0 );
	tm_qsi( "qsi.37.4", 0x37, 4, 0 );
	tm_qsi( "qsi.37.c", 0x37, 0xC, 0 );
	tm_qsi( "qsi.37.10", 0x37, 0x10, 0 );
	tm_qsi( "qsi.37.17", 0x37, 0x17, 0 );
	/* class C0H (SystemFlushInformation, phnt ntexapi.h SYSTEM_INFORMATION_CLASS): the loader's call (P39 trace, system call 7: Length 20H,
	 * ReturnLength NULL) - the emulator answers STATUS_INVALID_INFO_CLASS since P36 (no layout source); measured here */
	tm_qsi( "qsi.c0", 0xC0, 0x20, 1 );
	tm_qsi( "qsi.c0r", 0xC0, 0x20, 0 );
	tm_qsi( "qsi.c0.0", 0xC0, 0, 0 );
	tm_qsi( "qsi.c0.10", 0xC0, 0x10, 0 );
	tm_qsi( "qsi.c0.1f", 0xC0, 0x1F, 0 );
	tm_qsi( "qsi.c0.28", 0xC0, 0x28, 0 );
	/* class 32H SystemRangeStartInformation (phnt ntexapi.h SYSTEM_INFORMATION_CLASS; q: ULONG_PTR): kernelbase's call (P39 trace i33,
	 * system call 94: Length 8, ReturnLength NULL) - the start of the kernel's address range (a system constant, printed raw) */
	/* P45 (U2140): class 1 SystemProcessorInformation (phnt ntexapi.h SYSTEM_INFORMATION_CLASS; SYSTEM_PROCESSOR_INFORMATION {USHORT
	 * ProcessorArchitecture, ProcessorLevel, ProcessorRevision, MaximumProcessors, ULONG ProcessorFeatureBits}): d3d11's DllMain (sample
	 * exploration x7 system call 632: Length CH, ReturnLength NULL) - the processor's architecture / family / model-stepping (no identity) */
	tm_qsi( "qsi.1", 1, 0xC, 1 );
	tm_qsi( "qsi.1r", 1, 0xC, 0 );
	tm_qsi( "qsi.1.0", 1, 0, 0 );
	tm_qsi( "qsi.1.8", 1, 8, 0 );
	tm_qsi( "qsi.1.10", 1, 0x10, 0 );
	tm_qsi( "qsi.32", 0x32, 8, 1 );
	tm_qsi( "qsi.32r", 0x32, 8, 0 );
	tm_qsi( "qsi.32.4", 0x32, 4, 0 );
	tm_qsi( "qsi.32.10", 0x32, 0x10, 0 );
}

/* ------------------------------------------------------------------------------------------------ the hypervisor shared page (class C5H)
 * The page HypervisorSharedUserVa points to: its MEMORY_BASIC_INFORMATION (um\winnt.h; NtQueryVirtualMemory phnt ntmmapi.h:977-984,
 * class 0) - State / Protect / Type / AllocationProtect / RegionSize (the addresses as "<va>") - and its contents' SHAPE per
 * SYSTEM_HYPERVISOR_USER_SHARED_DATA {ULONG TimeUpdateLock, ULONG Reserved0?, ULONGLONG QpcMultiplier, ULONGLONG QpcBias} (phnt
 * ntexapi.h:7139-7160): each field zero / non-zero only (timing values of the host's hypervisor are not printed), the count of non-zero
 * bytes in the rest of the page; and this process' KUSER_SHARED_DATA QpcData (offset 3C6H, phnt ntexapi.h:9639; flags :8846-8852). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryVirtualMemory( HANDLE ProcessHandle, PVOID BaseAddress, ULONG MemoryInformationClass, PVOID MemoryInformation,
																SIZE_T MemoryInformationLength, PSIZE_T ReturnLength );
static void case_hvp( void )
{
	unsigned char* va = NULL;
	MEMORY_BASIC_INFORMATION mbi;
	SIZE_T rl = 0;
	TM_NTSTATUS st;
	size_t k, nz = 0;
	ULONG qsr = 0;
	st = NtQuerySystemInformation( 0xC5, &va, 8, &qsr );
	if ( st < 0 || !va )
	{
		printf( "tm_nt hvp: class C5H status %08lXH, HypervisorSharedUserVa %s - nothing to inspect\n", (unsigned long)st, va ? "<va>" : "NULL" );
		return;
	}
	memset( &mbi, 0, sizeof( mbi ) );
	st = NtQueryVirtualMemory( (HANDLE)(LONG_PTR)-1, va, 0, &mbi, sizeof( mbi ), &rl );
	printf( "tm_nt hvp.mbi: NtQueryVirtualMemory(<va>, MemoryBasicInformation) -> status %08lXH, ReturnLength %zXH; BaseAddress %s, AllocationBase %s, AllocationProtect %lXH, "
			"RegionSize %zXH, State %lXH, Protect %lXH, Type %lXH; page offset of the VA %zXH\n",
			(unsigned long)st, rl, mbi.BaseAddress == (PVOID)va ? "= the VA" : "<other>", mbi.AllocationBase == (PVOID)va ? "= the VA" : "<other>", (unsigned long)mbi.AllocationProtect,
			mbi.RegionSize, (unsigned long)mbi.State, (unsigned long)mbi.Protect, (unsigned long)mbi.Type, (size_t)( (ULONG_PTR)va & 0xFFF ) );
	for ( k = 0x18; k < 0x1000 - ( (ULONG_PTR)va & 0xFFF ); k++ ) nz += va[ k ] != 0;
	printf( "tm_nt hvp.data: TimeUpdateLock %s, +4 %s, QpcMultiplier %s, QpcBias %s, non-zero bytes in [+18H, page end) %zu; KUSER QpcData %04XH\n",
			*(ULONG*)va ? "non-zero" : "0", *(ULONG*)( va + 4 ) ? "non-zero" : "0", *(ULONGLONG*)( va + 8 ) ? "non-zero" : "0", *(ULONGLONG*)( va + 16 ) ? "non-zero" : "0", nz,
			*(volatile USHORT*)(ULONG_PTR)0x7FFE03C6 );
}

/* ------------------------------------------------------------------------------------------------ NtQueryInformationProcess
 * The undocumented classes the loader asks (P39 trace: 22H ProcessExecuteFlags, 24H ProcessCookie, Length 4; class names phnt ntpsapi.h
 * PROCESSINFOCLASS); prototype phnt ntpsapi.h NtQueryInformationProcess (winternl.h documents the call). Printed: status, ReturnLength,
 * the 22H value (DEP flags: a configuration, no identity); for 24H only zero / non-zero (a per-process secret - never printed). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryInformationProcess( HANDLE ProcessHandle, ULONG ProcessInformationClass, PVOID ProcessInformation,
																	 ULONG ProcessInformationLength, PULONG ReturnLength );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtSetInformationProcess( HANDLE ProcessHandle, ULONG ProcessInformationClass, PVOID ProcessInformation,
																   ULONG ProcessInformationLength );
static void tm_qip( const char* tag, ULONG cls, ULONG len, int retnull )
{
	ULONG ret = 0xCCCCCCCC;
	TM_NTSTATUS st;
	size_t w;
	memset( g_buf, 0xCC, sizeof( g_buf ) );
	st = NtQueryInformationProcess( (HANDLE)(LONG_PTR)-1, cls, len ? g_buf : NULL, len, retnull ? NULL : &ret );
	w = tm_written( sizeof( g_buf ) );
	printf( "tm_nt %s: NtQueryInformationProcess(NtCurrentProcess(), %lXH, Length %lXH, ReturnLength %s) -> status %08lXH, ReturnLength %lXH, written %zXH", tag,
			(unsigned long)cls, (unsigned long)len, retnull ? "NULL" : "set", (unsigned long)st, (unsigned long)ret, w );
	if ( st >= 0 && cls == 0x22 && w >= 4 ) printf( "; ExecuteFlags %lXH", (unsigned long)*(ULONG*)g_buf );
	if ( st >= 0 && cls == 0x24 && w >= 4 ) printf( "; Cookie %s", *(ULONG*)g_buf ? "<non-zero>" : "0" );
	printf( "\n" );
}

static void case_qip( void )
{
	tm_qip( "qip.22", 0x22, 4, 1 );
	tm_qip( "qip.22r", 0x22, 4, 0 );
	tm_qip( "qip.22.8", 0x22, 8, 0 );
	tm_qip( "qip.24", 0x24, 4, 1 );
	tm_qip( "qip.24r", 0x24, 4, 0 );
	tm_qip( "qip.24.8", 0x24, 8, 0 );
	{
		/* the cookie is the same on a second query in the same process? */
		ULONG a = 0, b = 0;
		NtQueryInformationProcess( (HANDLE)(LONG_PTR)-1, 0x24, &a, 4, NULL );
		NtQueryInformationProcess( (HANDLE)(LONG_PTR)-1, 0x24, &b, 4, NULL );
		printf( "tm_nt qip.24.same: two queries %s\n", a == b ? "equal" : "differ" );
	}
}

/* ------------------------------------------------------------------------------------------------ object-creating services
 * A handle value is printed only as its shape: "<handle>" plus whether it is non-zero and a multiple of 4 (the low two bits are
 * reserved for kernel handles); the value itself is never printed. NtClose: um\winternl.h / WDK ZwClose. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtClose( HANDLE Handle );
/* phnt ntioapi.h:3512-3516 */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtCreateWaitCompletionPacket( PHANDLE WaitCompletionPacketHandle, ACCESS_MASK DesiredAccess, PVOID ObjectAttributes );

static const char* tm_hshape( HANDLE h )
{
	const ULONG_PTR v = (ULONG_PTR)h;
	if ( v == (ULONG_PTR)0xCCCCCCCCCCCCCCCCull ) return "not written";
	if ( !v ) return "NULL";
	return ( v & 3 ) ? "<handle> (low bits set)" : "<handle> (non-zero, multiple of 4)";
}

/* phnt ntioapi.h:3327-3332 */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtCreateIoCompletion( PHANDLE IoCompletionHandle, ACCESS_MASK DesiredAccess, PVOID ObjectAttributes, ULONG NumberOfConcurrentThreads );

static void case_iocp( void )
{
	/* the loader's call (P39 trace i8, system call 39): IoCompletionHandle, DesiredAccess 1F0003H, ObjectAttributes NULL, Count 9 */
	static const ULONG counts[] = { 9, 0, 1, 8 };
	size_t i;
	for ( i = 0; i < sizeof( counts ) / sizeof( counts[ 0 ] ); i++ )
	{
		HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		TM_NTSTATUS st = NtCreateIoCompletion( &h, 0x1F0003, NULL, counts[ i ] );
		printf( "tm_nt iocp.%lu: NtCreateIoCompletion(&h, DesiredAccess 1F0003H, NULL, Count %lu) -> status %08lXH, handle %s", (unsigned long)counts[ i ], (unsigned long)counts[ i ],
				(unsigned long)st, tm_hshape( h ) );
		if ( st >= 0 ) printf( "; NtClose -> %08lXH", (unsigned long)NtClose( h ) );
		printf( "\n" );
	}
}

/* phnt ntexapi.h:2093-2104 */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtCreateWorkerFactory( PHANDLE WorkerFactoryHandleReturn, ACCESS_MASK DesiredAccess, PVOID ObjectAttributes, HANDLE CompletionPortHandle,
																 HANDLE WorkerProcessHandle, PVOID StartRoutine, PVOID StartParameter, ULONG MaxThreadCount, SIZE_T StackReserve,
																 SIZE_T StackCommit );
static volatile LONG g_worker_runs;
static void NTAPI tm_worker( PVOID p ) /* our own start routine: counts a run and returns (no thread is expected: no work is posted) */
{
	(void)p;
	InterlockedIncrement( &g_worker_runs );
}

static void case_wkf( void )
{
	/* the loader's call (P39 trace i10, system call 40): DesiredAccess F00FFH, ObjectAttributes NULL, the I/O completion port just created
	 * (Count 9), WorkerProcessHandle NtCurrentProcess(), StartRoutine (ntdll's; here our own), StartParameter (a heap block; here our own),
	 * MaxThreadCount 200H, StackReserve 100000H, StackCommit 1000H */
	HANDLE io = NULL, h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull, h2;
	static ULONG param[ 16 ];
	TM_NTSTATUS st = NtCreateIoCompletion( &io, 0x1F0003, NULL, 9 );
	printf( "tm_nt wkf.io: NtCreateIoCompletion(Count 9) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( io ) );
	st = NtCreateWorkerFactory( &h, 0xF00FF, NULL, io, (HANDLE)(LONG_PTR)-1, (PVOID)tm_worker, param, 0x200, 0x100000, 0x1000 );
	printf( "tm_nt wkf: NtCreateWorkerFactory(&h, F00FFH, NULL, the port, NtCurrentProcess(), start, param, 200H, 100000H, 1000H) -> status %08lXH, handle %s, start routine runs %ld\n",
			(unsigned long)st, tm_hshape( h ), (long)g_worker_runs );
	h2 = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	st = NtCreateWorkerFactory( &h2, 0xF00FF, NULL, (HANDLE)(LONG_PTR)-1, (HANDLE)(LONG_PTR)-1, (PVOID)tm_worker, param, 0x200, 0x100000, 0x1000 );
	printf( "tm_nt wkf.badport: NtCreateWorkerFactory(..., CompletionPortHandle NtCurrentProcess(), ...) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( h2 ) );
	if ( (ULONG_PTR)h2 != (ULONG_PTR)0xCCCCCCCCCCCCCCCCull && h2 ) NtClose( h2 );
	{
		/* P42 (step 3, run 7): the same call with the sample's own SizeOfStackReserve 800000H (P41 run 6 system call 40: the loader passes the
		 * image's stack reserve) on a fresh port of its own (Count 9, as the loader's) */
		HANDLE io8 = NULL, h8 = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		TM_NTSTATUS s8 = NtCreateIoCompletion( &io8, 0x1F0003, NULL, 9 );
		if ( s8 >= 0 ) s8 = NtCreateWorkerFactory( &h8, 0xF00FF, NULL, io8, (HANDLE)(LONG_PTR)-1, (PVOID)tm_worker, param, 0x200, 0x800000, 0x1000 );
		printf( "tm_nt wkf.800: NtCreateWorkerFactory(&h, F00FFH, NULL, a fresh port, NtCurrentProcess(), start, param, 200H, 800000H, 1000H) -> status %08lXH, handle %s\n",
				(unsigned long)s8, tm_hshape( h8 ) );
		if ( (ULONG_PTR)h8 != (ULONG_PTR)0xCCCCCCCCCCCCCCCCull && h8 ) NtClose( h8 );
		if ( io8 ) NtClose( io8 );
	}
	Sleep( 200 );
	printf( "tm_nt wkf.runs: start routine runs after 200 ms %ld\n", (long)g_worker_runs );
	if ( (ULONG_PTR)h != (ULONG_PTR)0xCCCCCCCCCCCCCCCCull && h ) printf( "tm_nt wkf.close: NtClose(the factory) -> %08lXH\n", (unsigned long)NtClose( h ) );
	NtClose( io );
}

/* phnt ntexapi.h:1410-1416 (attributes :1356-1361) */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtCreateTimer2( PHANDLE TimerHandle, PULONG TimerId, PVOID ObjectAttributes, ULONG Attributes, ACCESS_MASK DesiredAccess );
static void case_tmr2( void )
{
	/* the loader's call (P39 trace i11, system call 41): TimerHandle, TimerId NULL, ObjectAttributes NULL, Attributes 8
	 * (TIMER2_ATTRIBUTE_NO_WAKE), DesiredAccess 100002H (SYNCHRONIZE | TIMER_MODIFY_STATE; the upper half of the 64-bit argument is not
	 * part of the ULONG) */
	HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st = NtCreateTimer2( &h, NULL, NULL, 8, 0x100002 );
	printf( "tm_nt tmr2: NtCreateTimer2(&h, NULL, NULL, Attributes 8, DesiredAccess 100002H) -> status %08lXH, handle %s", (unsigned long)st, tm_hshape( h ) );
	if ( st >= 0 ) printf( "; NtClose -> %08lXH", (unsigned long)NtClose( h ) );
	printf( "\n" );
}

/* phnt ntioapi.h:3552-3561; events: um\winternl.h / WDK ZwCreateEvent; phnt NtCreateEvent */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtAssociateWaitCompletionPacket( HANDLE WaitCompletionPacketHandle, HANDLE IoCompletionHandle, HANDLE TargetObjectHandle,
																		   PVOID KeyContext, PVOID ApcContext, TM_NTSTATUS IoStatus, ULONG_PTR IoStatusInformation,
																		   PBOOLEAN AlreadySignaled );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtCreateEvent( PHANDLE EventHandle, ACCESS_MASK DesiredAccess, PVOID ObjectAttributes, ULONG EventType, BOOLEAN InitialState );
static void tm_awcp( const char* tag, const char* what, HANDLE target, int with_flag, ULONG_PTR info )
{
	HANDLE io = NULL, pk = NULL;
	BOOLEAN sig = 0xCC;
	static ULONG key[ 4 ], apc[ 4 ];
	TM_NTSTATUS st;
	NtCreateIoCompletion( &io, 0x1F0003, NULL, 9 );
	NtCreateWaitCompletionPacket( &pk, 1, NULL );
	st = NtAssociateWaitCompletionPacket( pk, io, target, key, apc, 0, info, with_flag ? &sig : NULL );
	printf( "tm_nt %s: NtAssociateWaitCompletionPacket(packet, port, %s, key, apc, 0, %lu, %s) -> status %08lXH, AlreadySignaled %s\n", tag, what, (unsigned long)info,
			with_flag ? "&b" : "NULL",
			(unsigned long)st, !with_flag ? "-" : sig == 0xCC ? "not written" : sig ? "TRUE" : "FALSE" );
	NtClose( pk );
	NtClose( io );
}

static void case_awcp( void )
{
	/* the loader's call (P39 trace i12, system call 43): the packet, the I/O completion port (Count 9), the NO_WAKE timer2 just created
	 * (not set), KeyContext / ApcContext (heap blocks; here our own), IoStatus 0, IoStatusInformation and AlreadySignaled from the stack */
	HANDLE t = NULL, e1 = NULL, e2 = NULL;
	NtCreateTimer2( &t, NULL, NULL, 8, 0x100002 );
	tm_awcp( "awcp.timer", "a timer2 (not set)", t, 1, 0 );
	tm_awcp( "awcp.timer.null", "a timer2 (not set)", t, 0, 0 );
	tm_awcp( "awcp.timer.info1", "a timer2 (not set)", t, 1, 1 );
	NtCreateEvent( &e1, 0x1F0003, NULL, 1, FALSE );
	tm_awcp( "awcp.event0", "a synchronization event (non-signaled)", e1, 1, 0 );
	NtCreateEvent( &e2, 0x1F0003, NULL, 1, TRUE );
	tm_awcp( "awcp.event1", "a synchronization event (signaled)", e2, 1, 0 );
	tm_awcp( "awcp.badtarget", "NtCurrentProcess() (a process, non-signaled)", (HANDLE)(LONG_PTR)-1, 1, 0 );
	NtClose( t );
	NtClose( e1 );
	NtClose( e2 );
}

/* phnt ntexapi.h:2122-2127 (classes WORKERFACTORYINFOCLASS :2038-2057) */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtSetInformationWorkerFactory( HANDLE WorkerFactoryHandle, ULONG WorkerFactoryInformationClass, PVOID WorkerFactoryInformation,
																		 ULONG WorkerFactoryInformationLength );
static void case_swf( void )
{
	/* a factory as the loader creates it (wkf), then each class with the loader's value (P39 trace) and neighbours; the start routine
	 * runs (our own, it returns at once) are counted after each call */
	static const struct { ULONG cls, len; LONGLONG v; } sets[] = {
		{ 5, 4, 0x300 }, { 5, 4, 0x200 }, { 5, 4, 1 }, { 5, 4, 0x10 }, { 4, 4, 0 }, { 4, 4, 1 }, { 3, 4, 1 }, { 6, 4, 0 }, { 9, 4, 0 }, { 10, 4, 0x100000 }, { 11, 4, 8 }, { 12, 4, 0 }, { 13, 4, 0 },
		{ 14, 4, 0x180 }, { 14, 4, 0x200 }, { 0, 8, -10000000LL }, { 1, 8, -10000000LL }, { 2, 8, -600000000LL }, { 5, 8, 0x200 }, { 5, 0, 0x200 },
		/* P41 exploration x5 (system calls 183 / 190 / 191): the loader's second factory - class 13 = 1, class 2 = -300000000 (8 bytes),
		 * class 5 = 3 */
		{ 13, 4, 1 }, { 2, 8, -300000000LL }, { 5, 4, 3 } };
	HANDLE io = NULL, h = NULL;
	static ULONG param[ 16 ];
	size_t i;
	NtCreateIoCompletion( &io, 0x1F0003, NULL, 9 );
	NtCreateWorkerFactory( &h, 0xF00FF, NULL, io, (HANDLE)(LONG_PTR)-1, (PVOID)tm_worker, param, 0x200, 0x100000, 0x1000 );
	for ( i = 0; i < sizeof( sets ) / sizeof( sets[ 0 ] ); i++ )
	{
		LONGLONG v = sets[ i ].v;
		TM_NTSTATUS st = NtSetInformationWorkerFactory( h, sets[ i ].cls, &v, sets[ i ].len );
		int k;
		/* stable output: "ran yes / no" (not a count); after class 4 = 1 wait up to 2 s for the worker the kernel starts */
		for ( k = 0; k < ( sets[ i ].cls == 4 && sets[ i ].v == 1 ? 40 : 1 ); k++ )
		{
			Sleep( 50 );
			if ( g_worker_runs ) break;
		}
		printf( "tm_nt swf.%lu.%lu.%llx: NtSetInformationWorkerFactory(factory, class %lu, value %llXH, Length %lu) -> status %08lXH; the start routine has run: %s\n", (unsigned long)sets[ i ].cls,
				(unsigned long)sets[ i ].len, (unsigned long long)sets[ i ].v, (unsigned long)sets[ i ].cls, (unsigned long long)sets[ i ].v, (unsigned long)sets[ i ].len, (unsigned long)st,
				g_worker_runs ? "yes" : "no" );
	}
	NtClose( h );
	NtClose( io );
	{
		/* the loader's exact sequence on a fresh factory (P39 trace: class 5 = 300H, class 14 = 180H, class 3 = 1) - does any of them start a
		 * worker thread? (the start routine counter, waited 2 s after the last call) */
		static const struct { ULONG cls; LONGLONG v; } seq[] = { { 5, 0x300 }, { 14, 0x180 }, { 3, 1 } };
		const LONG before = g_worker_runs;
		HANDLE io2 = NULL, h2 = NULL;
		int k;
		NtCreateIoCompletion( &io2, 0x1F0003, NULL, 9 );
		NtCreateWorkerFactory( &h2, 0xF00FF, NULL, io2, (HANDLE)(LONG_PTR)-1, (PVOID)tm_worker, param, 0x200, 0x100000, 0x1000 );
		for ( i = 0; i < 3; i++ )
		{
			LONGLONG v = seq[ i ].v;
			TM_NTSTATUS st = NtSetInformationWorkerFactory( h2, seq[ i ].cls, &v, 4 );
			printf( "tm_nt swf.loader.%zu: class %lu value %llXH -> status %08lXH\n", i, (unsigned long)seq[ i ].cls, (unsigned long long)seq[ i ].v, (unsigned long)st );
		}
		for ( k = 0; k < 40 && g_worker_runs == before; k++ ) Sleep( 50 );
		printf( "tm_nt swf.loader.runs: after the loader's three settings (2 s): the start routine ran %s\n", g_worker_runs == before ? "no" : "yes" );
		NtClose( h2 );
		NtClose( io2 );
	}
}

/* phnt ntwmi.h:6557-6564; function code 27 EtwAddNotificationEvent (:6374), input ETW_NOTIFICATION_EVENT_INFORMATION {HANDLE RegHandle}
 * (:6516-6519, "rev"; the loader passes 4 bytes) */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtTraceControl( ULONG FunctionCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength,
														  PULONG ReturnLength );
static ULONG NTAPI tm_etw_cb( PVOID a, PVOID b, PVOID c, PVOID d ) /* our own stand-in for the loader's ReplyCallback: does nothing */
{
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	return 0;
}

static void case_etw( void )
{
	/* the loader's call (P39 trace i17, system call 51): FunctionCode 1BH, InputBuffer (4 bytes: a synchronization event handle of the
	 * process, not signaled), 4, OutputBuffer NULL, 0, ReturnLength */
	HANDLE e = NULL;
	ULONG in, ret;
	TM_NTSTATUS st;
	NtCreateEvent( &e, 0x1F0003, NULL, 1, FALSE );
	in = (ULONG)(ULONG_PTR)e;
	ret = 0xCCCCCCCC;
	st = NtTraceControl( 0x1B, &in, 4, NULL, 0, &ret );
	printf( "tm_nt etw.event: NtTraceControl(1BH, &(ULONG)event handle, 4, NULL, 0, &r) -> status %08lXH, ReturnLength %lXH\n", (unsigned long)st, (unsigned long)ret );
	in = 0;
	ret = 0xCCCCCCCC;
	st = NtTraceControl( 0x1B, &in, 4, NULL, 0, &ret );
	printf( "tm_nt etw.zero: NtTraceControl(1BH, &(ULONG)0, 4, NULL, 0, &r) -> status %08lXH, ReturnLength %lXH\n", (unsigned long)st, (unsigned long)ret );
	ret = 0xCCCCCCCC;
	st = NtTraceControl( 0x1B, &e, 8, NULL, 0, &ret );
	printf( "tm_nt etw.event8: NtTraceControl(1BH, &event handle, 8, NULL, 0, &r) -> status %08lXH, ReturnLength %lXH\n", (unsigned long)st, (unsigned long)ret );
	NtClose( e );
	{
		/* function 0FH EtwRegisterGuidsCode (:6362), in ETW_UM_REGISTRATION_INFORMATION / out ETW_UM_REGISTRATION_REPLY (:6439-6473, "rev",
		 * A0H bytes): the loader's input (P39 trace i20, system call 52) byte for byte - ProviderGuid 3595AB5C-042A-4C8E-B942-2D059BFEB1B1
		 * (a provider GUID of the system's ntdll: a constant, no identity), RegType 3, LoggerId 1, the rest 0 - in place (in = out, A0H).
		 * Printed: status, ReturnLength, and per output field whether it changed and its new value's shape (a handle as <handle>) */
		/* P39 trace x1 (system calls 53 / 54): two more providers of the loader - 305FC87B-002A-5E26-D297-60223012CA9C (RegType 3,
		 * LoggerId 2, ReplyCallback = an ntdll function) and 901D2AFA-4FF6-46D7-8D0E-53645E1A47F5 (RegType 3, LoggerId 3, ReplyCallback = an
		 * ntdll function); here the ReplyCallback is our own function tm_etw_cb (it returns 0; the kernel never calls user code) */
		/* P41 (trace c3, system call 105; a process started without a console): kernelbase's provider B9AF743C-828D-E344-B52C-365DBF48382A
		 * (as bytes; RegType 3, LoggerId 4, ReplyCallback = a kernelbase function) - tag etw.reg4 */
		/* P41 exploration x4 (system calls 137 / 139 / 143): three more providers of the start in a process without a console -
		 * 0A27EFF1-..., FE5EF905-... (kernelbase) and 58446CE3-... (kernel32), RegType 3, LoggerId 5 / 6 / 7, ReplyCallback set */
		static const unsigned char guids[ 12 ][ 16 ] = {
			{ 0x5C, 0xAB, 0x95, 0x35, 0x2A, 0x04, 0x8E, 0x4C, 0xB9, 0x42, 0x2D, 0x05, 0x9B, 0xFE, 0xB1, 0xB1 },
			{ 0x7B, 0xC8, 0x5F, 0x30, 0x2A, 0x00, 0x26, 0x5E, 0xD2, 0x97, 0x60, 0x22, 0x30, 0x12, 0xCA, 0x9C },
			{ 0xFA, 0x2A, 0x1D, 0x90, 0xF6, 0x4F, 0xD7, 0x46, 0x8D, 0x0E, 0x53, 0x64, 0x5E, 0x1A, 0x47, 0xF5 },
			{ 0xB9, 0xAF, 0x74, 0x3C, 0x82, 0x8D, 0xE3, 0x44, 0xB5, 0x2C, 0x36, 0x5D, 0xBF, 0x48, 0x38, 0x2A },
			{ 0x0A, 0x27, 0xEF, 0xF1, 0x32, 0x0D, 0x52, 0x43, 0xBA, 0x52, 0xDB, 0xAB, 0x41, 0xE1, 0xD8, 0x59 },
			{ 0xFE, 0x5E, 0xF9, 0x05, 0x75, 0x7F, 0xC7, 0x49, 0xA9, 0x94, 0x60, 0xA5, 0x5C, 0xC0, 0x95, 0x71 },
			{ 0x58, 0x44, 0x6C, 0xE3, 0x80, 0xED, 0xD7, 0x4A, 0xA8, 0xBE, 0x52, 0xDD, 0xA1, 0xEB, 0x5F, 0x1C },
			/* P44 (U2115; exploration x2, T6 system calls 284 / 286 / 288): user32's providers 2ECD5BF2-... (Microsoft.Windows.User32),
			 * AC5EB75F-... (InputCore), 250E91E0-... (Microsoft.Windows.OneCoreTransforms) - RegType 3, LoggerId 8 / 9 / 10, ReplyCallback set */
			{ 0x2E, 0xCD, 0x5B, 0xF2, 0x90, 0x26, 0xDC, 0x55, 0x3B, 0xC4, 0x07, 0xB6, 0x5B, 0x1B, 0x41, 0xC9 },
			{ 0xAC, 0x5E, 0xB7, 0x5F, 0x0B, 0x9F, 0x0C, 0x55, 0x33, 0x9F, 0xFC, 0x21, 0xFD, 0xE9, 0x66, 0xCD },
			{ 0x25, 0x0E, 0x91, 0xE0, 0xCC, 0x6F, 0x32, 0x4F, 0x8F, 0x5E, 0xA4, 0x2C, 0xF7, 0xAC, 0xC5, 0xC3 },
			/* P45 (U2139; sample exploration x4 system call 601): dxgi's provider 03BBE5B8-C788-4D0B-B47E-5B5731398A89 (as bytes) - RegType 3,
			 * LoggerId 11, ReplyCallback set */
			{ 0xB8, 0xE5, 0xBB, 0x03, 0x88, 0xC7, 0x0B, 0x4D, 0xB4, 0x7E, 0x5B, 0x57, 0x31, 0x39, 0x8A, 0x89 },
			/* P45 (U2139; sample exploration x6 system call 603): dxgi's second provider CA11C036-0102-4A2D-A6AD-F03CFED5D3C9 (as bytes) - RegType 3,
			 * LoggerId 12, ReplyCallback set (no traits follow it) */
			{ 0x36, 0xC0, 0x11, 0xCA, 0x02, 0x01, 0x2D, 0x4A, 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 } };
		static const USHORT logger[ 12 ] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
		static const int with_cb[ 12 ] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
		int g;
		for ( g = 0; g < 12; g++ )
		{
			unsigned char io[ 0xA0 ], in0[ 0xA0 ];
			size_t k;
			char tag[ 32 ];
			memset( io, 0, sizeof( io ) );
			memcpy( io, guids[ g ], 16 );
			*(ULONG*)( io + 0x10 ) = 3;
			*(USHORT*)( io + 0x14 ) = logger[ g ];
			if ( with_cb[ g ] ) *(void**)( io + 0x20 ) = (void*)tm_etw_cb;
			memcpy( in0, io, sizeof( io ) );
			snprintf( tag, sizeof( tag ), g ? "etw.reg%d" : "etw.reg", g + 1 );
			ret = 0xCCCCCCCC;
			st = NtTraceControl( 0xF, io, 0xA0, io, 0xA0, &ret );
			printf( "tm_nt %s: NtTraceControl(0FH, the loader's A0H bytes (provider %d, LoggerId %u, ReplyCallback %s), in = out) -> status %08lXH, ReturnLength %lXH; changed bytes:", tag, g + 1,
					logger[ g ], with_cb[ g ] ? "set" : "NULL", (unsigned long)st, (unsigned long)ret );
			for ( k = 0; k < 0xA0; k++ )
				if ( io[ k ] != in0[ k ] ) printf( " +%02zX", k );
			printf( "; ReplyHandle (+18H) %s\n", tm_hshape( *(HANDLE*)( io + 0x18 ) ) );
			{
				/* the 64-bit enable fields as hex when below 10000000H, else as their 8 bytes (P41: no long digit runs in the data file) */
				char f[ 5 ][ 40 ];
				int q2;
				for ( q2 = 0; q2 < 5; q2++ )
				{
					const ULONGLONG v = *(ULONGLONG*)( io + 0x70 + 8 * q2 );
					if ( v < 0x10000000ull ) snprintf( f[ q2 ], sizeof( f[ q2 ] ), "%llXH", v );
					else snprintf( f[ q2 ], sizeof( f[ q2 ] ), "bytes %02X %02X %02X %02X %02X %02X %02X %02X", io[ 0x70 + 8 * q2 ], io[ 0x71 + 8 * q2 ], io[ 0x72 + 8 * q2 ],
								   io[ 0x73 + 8 * q2 ], io[ 0x74 + 8 * q2 ], io[ 0x75 + 8 * q2 ], io[ 0x76 + 8 * q2 ], io[ 0x77 + 8 * q2 ] );
				}
				printf( "tm_nt %s.fields: RegType %lu, LoggerId %u, Reserved0 %u, ReplyCallback %s, ReplyBufferSize %lXH, Reserved1 %lXH, EnableCallbackContext %s, EnableFilterDesc %s, "
						"EnableCallback %s, EnableCallbackReserved %s, ProviderTraits %s, TraitsFlags %lXH, HasPayload %lXH\n",
						tag, (unsigned long)*(ULONG*)( io + 0x10 ), *(USHORT*)( io + 0x14 ), *(USHORT*)( io + 0x16 ), *(void**)( io + 0x20 ) == *(void**)( in0 + 0x20 ) ? "unchanged" : "changed",
						(unsigned long)*(ULONG*)( io + 0x28 ), (unsigned long)*(ULONG*)( io + 0x2C ), f[ 0 ], f[ 1 ], f[ 2 ], f[ 3 ], f[ 4 ], (unsigned long)*(ULONG*)( io + 0x98 ),
						(unsigned long)*(ULONG*)( io + 0x9C ) );
			}
			{
				size_t nz = 0;
				for ( k = 0x30; k < 0x70; k++ ) nz += io[ k ] != 0;
				printf( "tm_nt %s.reserved2: non-zero bytes in Reserved2 [+30H, +70H): %zu\n", tag, nz );
			}
			if ( st >= 0 && *(HANDLE*)( io + 0x18 ) ) printf( "tm_nt %s.close: NtClose(ReplyHandle) -> %08lXH\n", tag, (unsigned long)NtClose( *(HANDLE*)( io + 0x18 ) ) );
		}
	}
}


/* P41 (U2072): function 1EH EtwSetProviderTraitsCode (phnt ntwmi.h:6377, "in ETW_SET_PROVIDER_TRAITS_INFORMATION, out
 * ETW_SET_PROVIDER_TRAITS_REPLY" - no layout in phnt) as kernelbase calls it right after registering its provider (etw.reg4) in a process
 * started without a console (trace c4, system call 106): input 18H bytes {the provider's ReplyHandle, a pointer to kernelbase's traits
 * blob (kernelbase+282EB6H in the capture's build), 33H, then the 6 bytes 06 00 FB 7F 00 00 the trace showed (uninitialised stack)},
 * output 78H bytes, ReturnLength. Replayed with a fresh registration of the same provider and OUR process' kernelbase at the same RVA
 * (valid for the capture's kernelbase build only, as csrc). Printed: status, ReturnLength, which output bytes changed and the reply's
 * non-zero qwords as shapes (a handle / pointer is never printed). */
static void tm_etwt( const char* pfx, const unsigned char* guid, USHORT loggerid, const wchar_t* module, ULONG rva, USHORT size, int trace_tail );
static void tm_etwt_bytes( const char* pfx, const unsigned char* guid, USHORT loggerid, const unsigned char* blob, USHORT size );
static void case_etwt( void )
{
	/* P41 exploration x4: the traits of providers 5 / 6 (kernelbase+282E80H 25H, +282DEFH 39H) and 7 (kernel32+8C95CH 39H); their inputs'
	 * 6 bytes after the size were not recorded (stack contents) - replayed as zero */
	static const unsigned char g5[ 16 ] = { 0x0A, 0x27, 0xEF, 0xF1, 0x32, 0x0D, 0x52, 0x43, 0xBA, 0x52, 0xDB, 0xAB, 0x41, 0xE1, 0xD8, 0x59 };
	static const unsigned char g6[ 16 ] = { 0xFE, 0x5E, 0xF9, 0x05, 0x75, 0x7F, 0xC7, 0x49, 0xA9, 0x94, 0x60, 0xA5, 0x5C, 0xC0, 0x95, 0x71 };
	static const unsigned char g7[ 16 ] = { 0x58, 0x44, 0x6C, 0xE3, 0x80, 0xED, 0xD7, 0x4A, 0xA8, 0xBE, 0x52, 0xDD, 0xA1, 0xEB, 0x5F, 0x1C };
	static const unsigned char g4[ 16 ] = { 0xB9, 0xAF, 0x74, 0x3C, 0x82, 0x8D, 0xE3, 0x44, 0xB5, 0x2C, 0x36, 0x5D, 0xBF, 0x48, 0x38, 0x2A };
	tm_etwt( "etwt", g4, 4, L"kernelbase.dll", 0x282EB6, 0x33, 1 );
	tm_etwt( "etwt5", g5, 5, L"kernelbase.dll", 0x282E80, 0x25, 0 );
	tm_etwt( "etwt6", g6, 6, L"kernelbase.dll", 0x282DEF, 0x39, 0 );
	tm_etwt( "etwt7", g7, 7, L"kernel32.dll", 0x8C95C, 0x39, 0 );
	{
		/* P44 (U2115; exploration x2, T6 system calls 285 / 287 / 289): user32's traits (user32+996FCH 2EH, +996DFH 0CH, +996A8H 26H in
		 * the capture's user32 build) - this process has no user32: the blobs are those bytes (the provider names and the group GUID of
		 * the trace), passed from a buffer of this program; the 6 bytes after the size: the trace's */
		static const unsigned char g8[ 16 ] = { 0x2E, 0xCD, 0x5B, 0xF2, 0x90, 0x26, 0xDC, 0x55, 0x3B, 0xC4, 0x07, 0xB6, 0x5B, 0x1B, 0x41, 0xC9 };
		static const unsigned char g9[ 16 ] = { 0xAC, 0x5E, 0xB7, 0x5F, 0x0B, 0x9F, 0x0C, 0x55, 0x33, 0x9F, 0xFC, 0x21, 0xFD, 0xE9, 0x66, 0xCD };
		static const unsigned char g10[ 16 ] = { 0x25, 0x0E, 0x91, 0xE0, 0xCC, 0x6F, 0x32, 0x4F, 0x8F, 0x5E, 0xA4, 0x2C, 0xF7, 0xAC, 0xC5, 0xC3 };
		static const char t8[] = "\x2E\x00Microsoft.Windows.User32\x00\x13\x00\x01\x1A\x73\x50\x4F\xCF\x89\x82\x47\xB3\xE0\xDC\xE8\xC9\x04\x76\xBA";
		static const char t9[] = "\x0C\x00InputCore";
		static const char t10[] = "\x26\x00Microsoft.Windows.OneCoreTransforms";
		tm_etwt_bytes( "etwt8", g8, 8, (const unsigned char*)t8, 0x2E );
		tm_etwt_bytes( "etwt9", g9, 9, (const unsigned char*)t9, 0x0C );
		tm_etwt_bytes( "etwt10", g10, 10, (const unsigned char*)t10, 0x26 );
	}
	{
		/* P45 (U2139; sample exploration x5 system call 602): dxgi's traits for its provider 03BBE5B8-... (etw.reg11): 35H bytes - the size,
		 * "Microsoft.Windows.Graphics.DXGI", the provider-group trait (13H, type 1, the group GUID of user32's provider) - the bytes of the
		 * capture's dxgi.dll (file offset AEA42H), passed from a buffer of this program */
		static const unsigned char g11[ 16 ] = { 0xB8, 0xE5, 0xBB, 0x03, 0x88, 0xC7, 0x0B, 0x4D, 0xB4, 0x7E, 0x5B, 0x57, 0x31, 0x39, 0x8A, 0x89 };
		static const unsigned char t11[ 0x35 ] = { 0x35, 0x00, 0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66, 0x74, 0x2E, 0x57, 0x69, 0x6E, 0x64, 0x6F, 0x77, 0x73, 0x2E,
												   0x47, 0x72, 0x61, 0x70, 0x68, 0x69, 0x63, 0x73, 0x2E, 0x44, 0x58, 0x47, 0x49, 0x00, 0x13, 0x00, 0x01, 0x1A, 0x73, 0x50,
												   0x4F, 0xCF, 0x89, 0x82, 0x47, 0xB3, 0xE0, 0xDC, 0xE8, 0xC9, 0x04, 0x76, 0xBA };
		tm_etwt_bytes( "etwt11", g11, 11, t11, 0x35 );
	}
}

static void tm_etwt( const char* pfx, const unsigned char* guid, USHORT loggerid, const wchar_t* module, ULONG rva, USHORT size, int trace_tail )
{
	unsigned char io[ 0xA0 ], in[ 0x18 ], out[ 0x78 ];
	unsigned char* kb = (unsigned char*)GetModuleHandleW( module );
	ULONG ret;
	TM_NTSTATUS st;
	size_t k, ch = 0;
	HANDLE h;
	memset( io, 0, sizeof( io ) );
	memcpy( io, guid, 16 );
	*(ULONG*)( io + 0x10 ) = 3;
	*(USHORT*)( io + 0x14 ) = loggerid;
	*(void**)( io + 0x20 ) = (void*)tm_etw_cb;
	st = NtTraceControl( 0xF, io, 0xA0, io, 0xA0, &ret );
	h = *(HANDLE*)( io + 0x18 );
	printf( "tm_nt %s.reg: NtTraceControl(0FH, provider %u as etw.reg%u) -> status %08lXH, ReplyHandle %s\n", pfx, loggerid, loggerid, (unsigned long)st, tm_hshape( h ) );
	if ( st < 0 || !kb ) return;
	{
		static const unsigned char tail[ 6 ] = { 0x06, 0x00, 0xFB, 0x7F, 0x00, 0x00 };
		unsigned char traits[ 0x40 ];
		size_t nz = 0;
		memcpy( traits, kb + rva, size );
		for ( k = 0; k < size; k++ ) nz += traits[ k ] != 0;
		printf( "tm_nt %s.blob: %S+%lXH, %XH bytes: first USHORT (the traits size) %XH, non-zero bytes %zu\n", pfx, module, (unsigned long)rva, size, *(USHORT*)traits, nz );
		memset( in, 0, sizeof( in ) );
		*(HANDLE*)in = h;
		*(void**)( in + 8 ) = kb + rva;
		*(USHORT*)( in + 0x10 ) = size;
		if ( trace_tail ) memcpy( in + 0x12, tail, 6 );
	}
	memset( out, 0xCC, sizeof( out ) );
	ret = 0xCCCCCCCC;
	st = NtTraceControl( 0x1E, in, 0x18, out, 0x78, &ret );
	printf( "tm_nt %s.set: NtTraceControl(1EH, {ReplyHandle, the traits blob, %XH, %s}, 18H, out, 78H, &r) -> status %08lXH, ReturnLength %lXH; output bytes changed:",
			pfx, size, trace_tail ? "the trace's 6 bytes" : "6 zero bytes", (unsigned long)st, (unsigned long)ret );
	for ( k = 0; k < sizeof( out ); k++ )
		if ( out[ k ] != 0xCC ) { printf( " +%02zX", k ); ch++; }
	printf( " (%zu)\n", ch );
	{
		/* the input unchanged? */
		printf( "tm_nt %s.in: input after the call: handle %s, blob pointer %s, size %XH\n", pfx, *(HANDLE*)in == h ? "unchanged" : "changed",
				*(unsigned char**)( in + 8 ) == kb + rva ? "unchanged" : "changed", *(USHORT*)( in + 0x10 ) );
	}
	printf( "tm_nt %s.close: NtClose(ReplyHandle) -> %08lXH\n", pfx, (unsigned long)NtClose( h ) );
}

/* P44 (U2115): the same with the traits blob given as bytes (user32's, this process has no user32); the trace's 6 bytes after the size */
static void tm_etwt_bytes( const char* pfx, const unsigned char* guid, USHORT loggerid, const unsigned char* blob, USHORT size )
{
	unsigned char io[ 0xA0 ], in[ 0x18 ], out[ 0x78 ];
	static const unsigned char tail[ 6 ] = { 0x06, 0x00, 0xFB, 0x7F, 0x00, 0x00 };
	ULONG ret;
	TM_NTSTATUS st;
	size_t k, ch = 0, nz = 0;
	HANDLE h;
	memset( io, 0, sizeof( io ) );
	memcpy( io, guid, 16 );
	*(ULONG*)( io + 0x10 ) = 3;
	*(USHORT*)( io + 0x14 ) = loggerid;
	*(void**)( io + 0x20 ) = (void*)tm_etw_cb;
	st = NtTraceControl( 0xF, io, 0xA0, io, 0xA0, &ret );
	h = *(HANDLE*)( io + 0x18 );
	printf( "tm_nt %s.reg: NtTraceControl(0FH, provider %u as etw.reg%u) -> status %08lXH, ReplyHandle %s\n", pfx, loggerid, loggerid, (unsigned long)st, tm_hshape( h ) );
	if ( st < 0 ) return;
	for ( k = 0; k < size; k++ ) nz += blob[ k ] != 0;
	printf( "tm_nt %s.blob: the traits (as bytes), %XH bytes: first USHORT (the traits size) %XH, non-zero bytes %zu\n", pfx, size, *(const USHORT*)blob, nz );
	memset( in, 0, sizeof( in ) );
	*(HANDLE*)in = h;
	*(const void**)( in + 8 ) = blob;
	*(USHORT*)( in + 0x10 ) = size;
	memcpy( in + 0x12, tail, 6 );
	memset( out, 0xCC, sizeof( out ) );
	ret = 0xCCCCCCCC;
	st = NtTraceControl( 0x1E, in, 0x18, out, 0x78, &ret );
	printf( "tm_nt %s.set: NtTraceControl(1EH, {ReplyHandle, the traits blob, %XH, the trace's 6 bytes}, 18H, out, 78H, &r) -> status %08lXH, ReturnLength %lXH; output bytes changed:",
			pfx, size, (unsigned long)st, (unsigned long)ret );
	for ( k = 0; k < sizeof( out ); k++ )
		if ( out[ k ] != 0xCC ) { printf( " +%02zX", k ); ch++; }
	printf( " (%zu)\n", ch );
	printf( "tm_nt %s.close: NtClose(ReplyHandle) -> %08lXH\n", pfx, (unsigned long)NtClose( h ) );
}

/* ------------------------------------------------------------------------------------------------ the KnownDlls object directory
 * phnt ntobapi.h:548-552 NtOpenDirectoryObject, :580-588 NtQueryDirectoryObject (OBJECT_DIRECTORY_INFORMATION :557-561), :751-755
 * NtOpenSymbolicLinkObject, :770-774 NtQuerySymbolicLinkObject; OBJECT_ATTRIBUTES / InitializeObjectAttributes: shared\ntdef.h.
 * Printed: statuses, handle shapes, every entry of \KnownDlls as "name:type" (system DLL names and object type names - no identity),
 * the KnownDllPath link target only as "<SystemRoot>\System32" when it equals this system's GetSystemDirectoryW() (else "<other>")
 * plus its lengths. */
typedef struct
{
	ULONG Length;
	HANDLE RootDirectory;
	TM_USTR* ObjectName;
	ULONG Attributes;
	PVOID SecurityDescriptor;
	PVOID SecurityQualityOfService;
} TM_OA;
__declspec( dllimport ) TM_NTSTATUS NTAPI NtOpenDirectoryObject( PHANDLE DirectoryHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryDirectoryObject( HANDLE DirectoryHandle, PVOID Buffer, ULONG Length, BOOLEAN ReturnSingleEntry, BOOLEAN RestartScan, PULONG Context,
																  PULONG ReturnLength );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtOpenSymbolicLinkObject( PHANDLE LinkHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQuerySymbolicLinkObject( HANDLE LinkHandle, TM_USTR* LinkTarget, PULONG ReturnedLength );
/* phnt ntmmapi.h:1449-1453 (the WDK's ZwOpenSection) */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtOpenSection( PHANDLE SectionHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes );
/* phnt ntmmapi.h:1475-1486, :1533-1536 (the WDK's ZwMapViewOfSection / ZwUnmapViewOfSection) */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtMapViewOfSection( HANDLE SectionHandle, HANDLE ProcessHandle, PVOID* BaseAddress, ULONG_PTR ZeroBits, SIZE_T CommitSize,
															  PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize, ULONG InheritDisposition, ULONG AllocationType, ULONG PageProtection );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtUnmapViewOfSection( HANDLE ProcessHandle, PVOID BaseAddress );

static void tm_oa( TM_OA* oa, HANDLE root, TM_USTR* name )
{
	memset( oa, 0, sizeof( *oa ) );
	oa->Length = sizeof( *oa );
	oa->RootDirectory = root;
	oa->ObjectName = name;
	oa->Attributes = 0x40; /* OBJ_CASE_INSENSITIVE (shared\ntdef.h) */
}

static void tm_wprint( const wchar_t* s, size_t n )
{
	size_t k;
	for ( k = 0; k < n; k++ ) putchar( ( s[ k ] >= 0x20 && s[ k ] < 0x7F ) ? (char)s[ k ] : '?' );
}

static void case_kd( void )
{
	/* the loader's call (P39 trace x1, system call 57): NtOpenDirectoryObject(&h, DesiredAccess 3, OA "\KnownDlls") */
	TM_USTR n, ln, tgt;
	TM_OA oa;
	HANDLE d = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull, l = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st;
	ULONG ctx = 0, rl = 0, count = 0;
	static wchar_t tbuf[ 512 ], sysdir[ 512 ];
	tm_ustr( &n, L"\\KnownDlls" );
	tm_oa( &oa, NULL, &n );
	st = NtOpenDirectoryObject( &d, 3, &oa );
	printf( "tm_nt kd.open: NtOpenDirectoryObject(&h, 3, \"\\KnownDlls\" OBJ_CASE_INSENSITIVE) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( d ) );
	if ( st < 0 ) return;
	printf( "tm_nt kd.list:" );
	for ( ;; )
	{
		const TM_USTR* e = (const TM_USTR*)g_buf;
		st = NtQueryDirectoryObject( d, g_buf, sizeof( g_buf ), TRUE, count == 0, &ctx, &rl );
		if ( st != 0 ) break;
		putchar( ' ' );
		tm_wprint( e[ 0 ].Buffer, e[ 0 ].Length / 2 );
		putchar( ':' );
		tm_wprint( e[ 1 ].Buffer, e[ 1 ].Length / 2 );
		count++;
	}
	printf( "; %lu entries, end status %08lXH\n", (unsigned long)count, (unsigned long)st );
	{
		/* the loader's next KnownDlls call (P39 trace i24, system call 72): NtOpenSection(&s, 0DH (SECTION_QUERY | SECTION_MAP_READ |
		 * SECTION_MAP_EXECUTE), OA root = the \KnownDlls handle, "KERNEL32.DLL", OBJ_CASE_INSENSITIVE); also a name that is not there */
		static const wchar_t* const sn[] = { L"KERNEL32.DLL", L"kernelbase.dll", L"TM_NT_NO_SUCH.DLL" };
		size_t q;
		for ( q = 0; q < 3; q++ )
		{
			TM_USTR sname;
			HANDLE s = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
			tm_ustr( &sname, sn[ q ] );
			tm_oa( &oa, d, &sname );
			st = NtOpenSection( &s, 0xD, &oa );
			printf( "tm_nt kd.section.%zu: NtOpenSection(&s, 0DH, root \\KnownDlls, \"", q );
			tm_wprint( sn[ q ], wcslen( sn[ q ] ) );
			printf( "\" OBJ_CASE_INSENSITIVE) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( s ) );
			if ( st >= 0 ) NtClose( s );
		}
	}
	{
		/* the loader's map (P39 trace i25, system call 73): NtMapViewOfSection(the KnownDlls section, NtCurrentProcess(), &base (0), ZeroBits 0,
		 * CommitSize 0, SectionOffset NULL, &ViewSize (0), ViewShare 1, AllocationType 800000H (MEM_DIFFERENT_IMAGE_BASE_OK), PAGE_EXECUTE_WRITECOPY
		 * 80H). Measured for a KnownDll this process has NOT loaded (advapi32.dll, WLDAP32.dll: the first view in a process, as the loader's
		 * kernel32 map is) and for KERNEL32.DLL (already loaded here: a second view). Printed: status, the base / size shapes, the view's
		 * header ImageBase relative to the base, the MEMORY_BASIC_INFORMATION of the base, the unmap status (addresses never printed). */
		static const wchar_t* const mn[] = { L"advapi32.dll", L"WLDAP32.dll", L"KERNEL32.DLL" };
		size_t q;
		for ( q = 0; q < 3; q++ )
		{
			TM_USTR sname;
			HANDLE s = NULL;
			PVOID base = NULL;
			SIZE_T vs = 0, rl2 = 0;
			MEMORY_BASIC_INFORMATION mbi;
			tm_ustr( &sname, mn[ q ] );
			tm_oa( &oa, d, &sname );
			st = NtOpenSection( &s, 0xD, &oa );
			if ( st < 0 ) { printf( "tm_nt kd.map.%zu: open failed %08lXH\n", q, (unsigned long)st ); continue; }
			st = NtMapViewOfSection( s, (HANDLE)(LONG_PTR)-1, &base, 0, 0, NULL, &vs, 1, 0x800000, 0x80 );
			printf( "tm_nt kd.map.%zu: NtMapViewOfSection(\"", q );
			tm_wprint( mn[ q ], wcslen( mn[ q ] ) );
			printf( "\", -1, &base (0), 0, 0, NULL, &size (0), ViewShare, 800000H, 80H) -> status %08lXH", (unsigned long)st );
			if ( st >= 0 && base )
			{
				const unsigned char* b = (const unsigned char*)base;
				const LONG lfanew = *(const LONG*)( b + 0x3C );
				const ULONGLONG hib = *(const ULONGLONG*)( b + lfanew + 0x18 + 0x18 );
				const ULONG soi = *(const ULONG*)( b + lfanew + 0x18 + 0x38 );
				printf( ", base <va> (64K-aligned %s), ViewSize %s SizeOfImage (%zXH vs %lXH), header ImageBase %s", ( (ULONG_PTR)base & 0xFFFF ) ? "no" : "yes", vs == soi ? "=" : "!=", vs,
						(unsigned long)soi, hib == (ULONGLONG)(ULONG_PTR)base ? "= the base" : "!= the base" );
				memset( &mbi, 0, sizeof( mbi ) );
				NtQueryVirtualMemory( (HANDLE)(LONG_PTR)-1, base, 0, &mbi, sizeof( mbi ), &rl2 );
				printf( "; MBI at the base: AllocationProtect %lXH, State %lXH, Protect %lXH, Type %lXH, RegionSize %zXH", (unsigned long)mbi.AllocationProtect, (unsigned long)mbi.State,
						(unsigned long)mbi.Protect, (unsigned long)mbi.Type, mbi.RegionSize );
				printf( "; NtUnmapViewOfSection -> %08lXH", (unsigned long)NtUnmapViewOfSection( (HANDLE)(LONG_PTR)-1, base ) );
			}
			printf( "\n" );
			NtClose( s );
		}
	}
	tm_ustr( &ln, L"KnownDllPath" );
	tm_oa( &oa, d, &ln );
	st = NtOpenSymbolicLinkObject( &l, 1, &oa );
	printf( "tm_nt kd.link: NtOpenSymbolicLinkObject(&h, 1, root \\KnownDlls, \"KnownDllPath\") -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( l ) );
	if ( st >= 0 )
	{
		UINT sl = GetSystemDirectoryW( sysdir, 512 );
		tgt.Buffer = tbuf;
		tgt.Length = 0;
		tgt.MaximumLength = sizeof( tbuf );
		rl = 0xCCCCCCCC;
		st = NtQuerySymbolicLinkObject( l, &tgt, &rl );
		printf( "tm_nt kd.target: NtQuerySymbolicLinkObject(link, MaximumLength %XH) -> status %08lXH, Length %XH, MaximumLength %XH, ReturnedLength %lXH, target %s\n", (unsigned)sizeof( tbuf ),
				(unsigned long)st, tgt.Length, tgt.MaximumLength, (unsigned long)rl,
				( sl && tgt.Length == sl * 2 && !_wcsnicmp( tbuf, sysdir, sl ) ) ? "<SystemRoot>\\System32 (= GetSystemDirectoryW)" : "<other>" );
		NtClose( l );
	}
	NtClose( d );
}

/* phnt ntioapi.h:2294-2301 NtOpenFile (the WDK's ZwOpenFile); IO_STATUS_BLOCK {Status/Pointer, Information} (WDK wdm.h) */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtOpenFile( PHANDLE FileHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes, PVOID IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions );
/* phnt ntioapi.h NtQueryVolumeInformationFile (the WDK's ZwQueryVolumeInformationFile) */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryVolumeInformationFile( HANDLE FileHandle, PVOID IoStatusBlock, PVOID FsInformation, ULONG Length, ULONG FsInformationClass );
static void case_of( void )
{
	/* the loader's call (P39 trace i22, system call 68): NtOpenFile(&h, 100020H (SYNCHRONIZE | FILE_TRAVERSE), OA = the NT path of the
	 * process' current directory ("\??\" + CurrentDirectory.DosPath, which ends in "\"), &iosb, ShareAccess 3, OpenOptions 21H
	 * (FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT)). Here: this process' own current directory (the path is not printed) */
	static wchar_t path[ 600 ];
	TM_USTR n;
	TM_OA oa;
	HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	ULONG_PTR iosb[ 2 ] = { (ULONG_PTR)0xCCCCCCCCCCCCCCCCull, (ULONG_PTR)0xCCCCCCCCCCCCCCCCull };
	TM_NTSTATUS st;
	DWORD k;
	wcscpy( path, L"\\??\\" );
	k = GetCurrentDirectoryW( 500, path + 4 );
	if ( !k || path[ 4 + k - 1 ] != L'\\' ) wcscat( path, L"\\" );
	tm_ustr( &n, path );
	tm_oa( &oa, NULL, &n );
	oa.Attributes = 0x40;
	st = NtOpenFile( &h, 0x100020, &oa, iosb, 3, 0x21 );
	printf( "tm_nt of.curdir: NtOpenFile(&h, 100020H, \"\\??\\<current directory>\\\" OBJ_CASE_INSENSITIVE, &iosb, 3, 21H) -> status %08lXH, handle %s, IoStatus.Status %08lXH, Information %llXH\n",
			(unsigned long)st, tm_hshape( h ), (unsigned long)(ULONG)iosb[ 0 ], (unsigned long long)iosb[ 1 ] );
	if ( st >= 0 )
	{
		/* the loader's next call (P39 trace i23, system call 69): NtQueryVolumeInformationFile(the directory handle, &iosb, buffer, 8, class 4
		 * FileFsDeviceInformation) - FILE_FS_DEVICE_INFORMATION {DEVICE_TYPE DeviceType, ULONG Characteristics} (WDK ntddk.h / wdm.h) */
		ULONG fsdev[ 4 ] = { 0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC };
		TM_NTSTATUS s2;
		iosb[ 0 ] = iosb[ 1 ] = (ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		s2 = NtQueryVolumeInformationFile( h, iosb, fsdev, 8, 4 );
		printf( "tm_nt of.fsdev: NtQueryVolumeInformationFile(the directory, &iosb, buf, 8, FileFsDeviceInformation 4) -> status %08lXH, IoStatus.Status %08lXH, Information %llXH; "
				"DeviceType %lXH, Characteristics %lXH, byte 8 %s\n",
				(unsigned long)s2, (unsigned long)(ULONG)iosb[ 0 ], (unsigned long long)iosb[ 1 ], (unsigned long)fsdev[ 0 ], (unsigned long)fsdev[ 1 ], fsdev[ 2 ] == 0xCCCCCCCC ? "untouched" : "written" );
		printf( "tm_nt of.close: NtClose -> %08lXH\n", (unsigned long)NtClose( h ) );
	}
	wcscat( path, L"tm_nt_no_such_dir\\" );
	tm_ustr( &n, path );
	h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	iosb[ 0 ] = iosb[ 1 ] = (ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	st = NtOpenFile( &h, 0x100020, &oa, iosb, 3, 0x21 );
	printf( "tm_nt of.missing: NtOpenFile(... a directory that does not exist ...) -> status %08lXH, handle %s, IoStatus %s\n", (unsigned long)st, tm_hshape( h ),
			iosb[ 0 ] == (ULONG_PTR)0xCCCCCCCCCCCCCCCCull ? "not written" : "written" );
}

/* ------------------------------------------------------------------------------------------------ ProcessTlsInformation (class 23H)
 * phnt ntpsapi.h:2557-2562 NtSetInformationProcess; class 35 = ProcessTlsInformation (PROCESSINFOCLASS), PROCESS_TLS_INFORMATION
 * :981-989 / THREAD_TLS_INFORMATION :960-966 / PROCESS_TLS_INFORMATION_TYPE :971-976. The loader's call (P39 trace i27, system call 90):
 * Length 28H - bytes {Flags 0, OperationType 1 (ProcessTlsReplaceVector), ThreadDataCount 1, 0} then ONE 18H-byte thread entry {Flags 0
 * (+10H), a heap pointer (the new TLS vector, +18H), an unset 8-byte field (+20H)} - i.e. this Windows build's entry is 18H bytes (phnt
 * lists 20H). Measured here in THIS process once it has exactly one thread (the loader's worker threads exit when idle - waited for,
 * up to 120 s, counted with a Toolhelp thread snapshot of the own process): the same bytes with the new vector = the thread's CURRENT
 * TEB ThreadLocalStoragePointer (gs:[58H]; nothing changes for the thread), the +20H field pre-set CCH. Printed: status, which fields
 * changed and how (the +18H field vs the old vector, the +20H field vs GetCurrentThreadId()), and the TEB pointer after the call. */
#include <tlhelp32.h>
#include <intrin.h>
static ULONG tm_own_threads( void )
{
	ULONG n = 0;
	THREADENTRY32 te;
	HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
	if ( snap == INVALID_HANDLE_VALUE ) return 0;
	te.dwSize = sizeof( te );
	if ( Thread32First( snap, &te ) )
		do
			if ( te.th32OwnerProcessID == GetCurrentProcessId() ) n++;
		while ( Thread32Next( snap, &te ) );
	CloseHandle( snap );
	return n;
}

static void case_tls( void )
{
	unsigned char buf[ 0x40 ];
	ULONG n = 0, w;
	ULONGLONG cur, after;
	TM_NTSTATUS st;
	for ( w = 0; w < 240; w++ )
	{
		n = tm_own_threads();
		if ( n == 1 ) break;
		Sleep( 500 );
	}
	printf( "tm_nt tls.threads: threads of this process before the call: %lu\n", (unsigned long)n );
	if ( n != 1 ) return;
	cur = __readgsqword( 0x58 );
	memset( buf, 0xCC, sizeof( buf ) );
	*(ULONG*)( buf + 0 ) = 0;
	*(ULONG*)( buf + 4 ) = 1;
	*(ULONG*)( buf + 8 ) = 1;
	*(ULONG*)( buf + 0xC ) = 0;
	*(ULONG*)( buf + 0x10 ) = 0;
	*(ULONGLONG*)( buf + 0x18 ) = cur;
	st = NtSetInformationProcess( (HANDLE)(LONG_PTR)-1, 0x23, buf, 0x28 );
	after = __readgsqword( 0x58 );
	printf( "tm_nt tls.set: NtSetInformationProcess(-1, 23H, {0, 1 ReplaceVector, 1, 0; entry {0, the current vector, CCH..}}, 28H) -> status %08lXH; header %lX %lX %lX %lX; "
			"entry Flags %lXH, +14H %s, +18H %s, +20H %s; TEB ThreadLocalStoragePointer after %s; bytes 28H.. %s\n",
			(unsigned long)st, (unsigned long)*(ULONG*)( buf + 0 ), (unsigned long)*(ULONG*)( buf + 4 ), (unsigned long)*(ULONG*)( buf + 8 ), (unsigned long)*(ULONG*)( buf + 0xC ),
			(unsigned long)*(ULONG*)( buf + 0x10 ), *(ULONG*)( buf + 0x14 ) == 0xCCCCCCCC ? "untouched" : "written",
			*(ULONGLONG*)( buf + 0x18 ) == cur ? "= the old vector (= the new one here)" : "<other>",
			*(ULONGLONG*)( buf + 0x20 ) == 0xCCCCCCCCCCCCCCCCull ? "untouched" : *(ULONGLONG*)( buf + 0x20 ) == (ULONGLONG)GetCurrentThreadId() ? "= GetCurrentThreadId()" : "<other value>",
			after == cur ? "unchanged (= the vector passed)" : "<other>", buf[ 0x28 ] == 0xCC ? "untouched" : "written" );
	{
		/* a DIFFERENT new vector: a heap copy of the current one (same contents - the thread's TLS keeps working), then the original
		 * restored the same way */
		/* (not HeapSize: the vector is not a block of the process heap - a first version stopped with STATUS_HEAP_CORRUPTION) the first
		 * 8 slots copied into a fresh page; this program's TLS uses slot 0 */
		const SIZE_T sz = 8 * sizeof( void* );
		void* copy = VirtualAlloc( NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
		if ( copy )
		{
			memcpy( copy, (const void*)(ULONG_PTR)cur, sz );
			memset( buf, 0xCC, sizeof( buf ) );
			*(ULONG*)( buf + 0 ) = 0;
			*(ULONG*)( buf + 4 ) = 1;
			*(ULONG*)( buf + 8 ) = 1;
			*(ULONG*)( buf + 0xC ) = 0;
			*(ULONG*)( buf + 0x10 ) = 0;
			*(ULONGLONG*)( buf + 0x18 ) = (ULONGLONG)(ULONG_PTR)copy;
			st = NtSetInformationProcess( (HANDLE)(LONG_PTR)-1, 0x23, buf, 0x28 );
			after = __readgsqword( 0x58 );
			printf( "tm_nt tls.swap: the same call with a different new vector (a copy) -> status %08lXH; entry Flags %lXH, +18H %s, +20H %s; TEB ThreadLocalStoragePointer after %s\n",
					(unsigned long)st, (unsigned long)*(ULONG*)( buf + 0x10 ),
					*(ULONGLONG*)( buf + 0x18 ) == cur ? "= the OLD vector" : *(ULONGLONG*)( buf + 0x18 ) == (ULONGLONG)(ULONG_PTR)copy ? "= the new vector (unchanged)" : "<other>",
					*(ULONGLONG*)( buf + 0x20 ) == (ULONGLONG)GetCurrentThreadId() ? "= GetCurrentThreadId()" : "<other>",
					after == (ULONGLONG)(ULONG_PTR)copy ? "= the new vector" : after == cur ? "= the old vector" : "<other>" );
			memset( buf, 0xCC, sizeof( buf ) );
			*(ULONG*)( buf + 0 ) = 0;
			*(ULONG*)( buf + 4 ) = 1;
			*(ULONG*)( buf + 8 ) = 1;
			*(ULONG*)( buf + 0xC ) = 0;
			*(ULONG*)( buf + 0x10 ) = 0;
			*(ULONGLONG*)( buf + 0x18 ) = cur;
			st = NtSetInformationProcess( (HANDLE)(LONG_PTR)-1, 0x23, buf, 0x28 );
			printf( "tm_nt tls.restore: back to the original vector -> status %08lXH, TEB pointer %s\n", (unsigned long)st, __readgsqword( 0x58 ) == cur ? "= the original" : "<other>" );
		}
	}
	{
		/* OperationType 0 ProcessTlsReplaceIndex (the T3 loader's call, P39 trace i28: {0, 0, 1, TlsIndex 1} + one entry, 28H): here with
		 * TlsIndex 0 (this program's slot; lines tls.idx0 .. tls.idx2): first the same block pointer, then a copy of the block's first 100H bytes in a fresh page, then
		 * the original restored */
		ULONGLONG* vec = (ULONGLONG*)(ULONG_PTR)__readgsqword( 0x58 );
		const ULONGLONG blk = vec[ 0 ];
		unsigned char* copy = (unsigned char*)VirtualAlloc( NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
		int round;
		for ( round = 0; round < 3 && copy; round++ )
		{
			const ULONGLONG nv = round == 1 ? (ULONGLONG)(ULONG_PTR)copy : blk;
			if ( round == 1 ) memcpy( copy, (const void*)(ULONG_PTR)blk, 0x100 );
			memset( buf, 0xCC, sizeof( buf ) );
			*(ULONG*)( buf + 0 ) = 0;
			*(ULONG*)( buf + 4 ) = 0;
			*(ULONG*)( buf + 8 ) = 1;
			*(ULONG*)( buf + 0xC ) = 0;
			*(ULONG*)( buf + 0x10 ) = 0;
			*(ULONGLONG*)( buf + 0x18 ) = nv;
			st = NtSetInformationProcess( (HANDLE)(LONG_PTR)-1, 0x23, buf, 0x28 );
			printf( "tm_nt tls.idx%d: NtSetInformationProcess(-1, 23H, {0, 0 ReplaceIndex, 1, TlsIndex 0; entry {0, %s, CCH..}}, 28H) -> status %08lXH; header %lX %lX %lX %lX; entry Flags %lXH, "
					"+18H %s, +20H %s; vector[0] after %s\n",
					round, round == 1 ? "a copy of the block" : "the current block", (unsigned long)st, (unsigned long)*(ULONG*)( buf + 0 ), (unsigned long)*(ULONG*)( buf + 4 ),
					(unsigned long)*(ULONG*)( buf + 8 ), (unsigned long)*(ULONG*)( buf + 0xC ), (unsigned long)*(ULONG*)( buf + 0x10 ),
					*(ULONGLONG*)( buf + 0x18 ) == blk ? ( round == 1 ? "= the OLD block" : "= the block (old = new)" ) : *(ULONGLONG*)( buf + 0x18 ) == nv ? "= the new value (unchanged)" : "<other>",
					*(ULONGLONG*)( buf + 0x20 ) == 0xCCCCCCCCCCCCCCCCull ? "untouched" : *(ULONGLONG*)( buf + 0x20 ) == (ULONGLONG)GetCurrentThreadId() ? "= GetCurrentThreadId()" : "<other>",
					vec[ 0 ] == nv ? "= the new value" : vec[ 0 ] == blk ? "= the old block" : "<other>" );
		}
	}
	memset( buf, 0xCC, sizeof( buf ) );
	*(ULONG*)( buf + 0 ) = 0;
	*(ULONG*)( buf + 4 ) = 1;
	*(ULONG*)( buf + 8 ) = 0;
	*(ULONG*)( buf + 0xC ) = 0;
	st = NtSetInformationProcess( (HANDLE)(LONG_PTR)-1, 0x23, buf, 0x10 );
	printf( "tm_nt tls.zero: NtSetInformationProcess(-1, 23H, {0, 1, 0 entries, 0}, 10H) -> status %08lXH\n", (unsigned long)st );
}

/* ------------------------------------------------------------------------------------------------ NtApphelpCacheControl
 * phnt ntmisc.h:257-262 (AHC_SERVICE_CLASS :82-97: 6 = ApphelpCacheServiceLookupCdb; AHC_SERVICE_DATA :234-246, AHC_SERVICE_LOOKUP_CDB
 * {UNICODE_STRING Name} :136-139). The loader's call (P39 trace i30, system call 91): class 6, a context of 178H bytes, all zero except
 * LookupCdb.Name at +E8H (the DLL name, e.g. "KERNELBASE.dll"), ParamsOut at +168H (a 4-byte stack variable) and ParamsOutSize 4 at
 * +170H. Here: the same layout for several DLL names; printed: status, the 4 output bytes (pre-set CCH), the DriverStatus (+160H) and
 * which other context bytes changed. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtApphelpCacheControl( ULONG ServiceClass, PVOID ServiceContext );
static void case_ahc( void )
{
	/* P41 (exploration x5, system call 196): the test PEs' own image names (the loader's LookupCdb for the process image) */
	/* P43 (U2107; sample run 8, exploration x2 system calls 449-472): the 24 names the loader asks after the sample's import processing, in
	 * its order and spelling (ahc.9 .. ahc.32; the last one is the sample's image NAME - only the string, the file is never touched) */
	static const wchar_t* const names[] = { L"KERNELBASE.dll", L"KERNEL32.DLL", L"kernel32.dll", L"ntdll.dll", L"tm_nt_no_such.dll", L"t2_import.exe", L"t3_tls.exe",
											L"t3_tls.vmware_guest.exe", L"t5_stdout.exe",
											L"win32u.dll", L"ucrtbase.dll", L"msvcp_win.dll", L"gdi32full.dll", L"GDI32.dll", L"USER32.dll", L"msvcrt.dll", L"dxgi.dll",
											L"d3d11.dll", L"RPCRT4.dll", L"combase.dll", L"kernel.appcore.dll", L"dcomp.dll", L"ole32.dll", L"OLEAUT32.dll", L"SHELL32.dll",
											L"dwmapi.dll", L"WS2_32.dll", L"bcrypt.dll", L"sechost.dll", L"ADVAPI32.dll", L"cfgmgr32.dll", L"SETUPAPI.dll",
											L"transparent_makimav2.exe",
											L"t6_user32.exe", L"IMM32.DLL",   /* P44 (U2116): ahc.34 - user32 loads the IMM file (its _InitializeImmEntryTable) */   /* P44 (U2115): ahc.33 - test PE T6's image name (its loader's LookupCdb) */
											L"t7_vfs.exe" };   /* P45 (U2125): ahc.35 - test PE T7's image name (its loader's LookupCdb) */
	size_t q, k;
	for ( q = 0; q < sizeof( names ) / sizeof( names[ 0 ] ); q++ )
	{
		static unsigned char ctx[ 0x200 ], ctx0[ 0x200 ];
		ULONG out = 0xCCCCCCCC;
		TM_USTR* n = (TM_USTR*)( ctx + 0xE8 );
		TM_NTSTATUS st;
		memset( ctx, 0, sizeof( ctx ) );
		tm_ustr( n, names[ q ] );
		*(PVOID*)( ctx + 0x168 ) = &out;
		*(ULONG*)( ctx + 0x170 ) = 4;
		memcpy( ctx0, ctx, sizeof( ctx ) );
		st = NtApphelpCacheControl( 6, ctx );
		printf( "tm_nt ahc.%zu: NtApphelpCacheControl(6 LookupCdb, Name \"", q );
		tm_wprint( names[ q ], wcslen( names[ q ] ) );
		printf( "\", ParamsOut 4 bytes) -> status %08lXH, out %lXH, DriverStatus %08lXH, changed context bytes:", (unsigned long)st, (unsigned long)out, (unsigned long)*(ULONG*)( ctx + 0x160 ) );
		for ( k = 0; k < sizeof( ctx ); k++ )
			if ( ctx[ k ] != ctx0[ k ] ) printf( " +%02zX", k );
		printf( "\n" );
	}
}

/* phnt ntmmapi.h:1186-1193 NtSetInformationVirtualMemory (class 4 VmImageHotPatchInformation :1040, ULONG; MEMORY_RANGE_ENTRY :1161) */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtSetInformationVirtualMemory( HANDLE ProcessHandle, ULONG VmInformationClass, ULONG_PTR NumberOfEntries, PVOID VirtualAddresses,
																		 PVOID VmInformation, ULONG VmInformationLength );
static void case_vmi( void )
{
	/* the loader's call (P39 trace i32, system call 93): class 4, 1 range = {the just-mapped kernelbase.dll view's base, 1000H}, value 1
	 * (4 bytes). Here: a fresh first view of a KnownDll this process has not loaded (advapi32.dll, mapped as the loader maps), then the
	 * same call on this process' own kernelbase.dll (already processed by its loader) */
	TM_USTR n, sn;
	TM_OA oa;
	HANDLE d = NULL, s = NULL;
	PVOID base = NULL;
	SIZE_T vs = 0;
	ULONG one = 1;
	ULONG_PTR range[ 2 ];
	TM_NTSTATUS st;
	tm_ustr( &n, L"\\KnownDlls" );
	tm_oa( &oa, NULL, &n );
	if ( NtOpenDirectoryObject( &d, 3, &oa ) < 0 ) return;
	tm_ustr( &sn, L"advapi32.dll" );
	tm_oa( &oa, d, &sn );
	if ( NtOpenSection( &s, 0xD, &oa ) >= 0 && NtMapViewOfSection( s, (HANDLE)(LONG_PTR)-1, &base, 0, 0, NULL, &vs, 1, 0x800000, 0x80 ) >= 0 )
	{
		range[ 0 ] = (ULONG_PTR)base;
		range[ 1 ] = 0x1000;
		st = NtSetInformationVirtualMemory( (HANDLE)(LONG_PTR)-1, 4, 1, range, &one, 4 );
		printf( "tm_nt vmi.fresh: NtSetInformationVirtualMemory(-1, 4, 1, {a fresh image view's base, 1000H}, &1, 4) -> status %08lXH\n", (unsigned long)st );
		st = NtSetInformationVirtualMemory( (HANDLE)(LONG_PTR)-1, 4, 1, range, &one, 4 );
		printf( "tm_nt vmi.again: the same call again -> status %08lXH\n", (unsigned long)st );
		NtUnmapViewOfSection( (HANDLE)(LONG_PTR)-1, base );
	}
	if ( s ) NtClose( s );
	NtClose( d );
	range[ 0 ] = (ULONG_PTR)GetModuleHandleW( L"kernelbase.dll" );
	range[ 1 ] = 0x1000;
	st = NtSetInformationVirtualMemory( (HANDLE)(LONG_PTR)-1, 4, 1, range, &one, 4 );
	printf( "tm_nt vmi.loaded: the same call on this process' kernelbase.dll base -> status %08lXH\n", (unsigned long)st );
}

/* ------------------------------------------------------------------------------------------------ the CSR shared section (shape only)
 * kernelbase's call (P39 trace i34, system call 96): NtOpenSection(&s, 4 (SECTION_MAP_READ), "\Sessions\<SessionId>\Windows\SharedSection"
 * OBJ_CASE_INSENSITIVE, no root). Measured: the open, and a read-only view of it (NtMapViewOfSection ViewUnmap 2, PAGE_READONLY) -
 * status, ViewSize, the base alignment, the MEMORY_BASIC_INFORMATION; the CONTENTS (the CSR server's shared heap) are NEVER read or
 * printed. The session number is taken from this process' PEB SessionId (2C0H) and printed only as "= the PEB SessionId". */
static void case_csr( void )
{
	static wchar_t name[ 128 ];
	TM_USTR n;
	TM_OA oa;
	HANDLE s = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st;
	const ULONG sid = *(ULONG*)( (ULONG_PTR)__readgsqword( 0x60 ) + 0x2C0 );
	_snwprintf( name, 127, L"\\Sessions\\%lu\\Windows\\SharedSection", (unsigned long)sid );
	tm_ustr( &n, name );
	tm_oa( &oa, NULL, &n );
	st = NtOpenSection( &s, 4, &oa );
	printf( "tm_nt csr.open: NtOpenSection(&s, 4 SECTION_MAP_READ, \"\\Sessions\\<the PEB SessionId>\\Windows\\SharedSection\" OBJ_CASE_INSENSITIVE) -> status %08lXH, handle %s\n",
			(unsigned long)st, tm_hshape( s ) );
	if ( st >= 0 )
	{
		PVOID base = NULL;
		SIZE_T vs = 0, rl = 0;
		MEMORY_BASIC_INFORMATION mbi;
		st = NtMapViewOfSection( s, (HANDLE)(LONG_PTR)-1, &base, 0, 0, NULL, &vs, 2, 0, 2 );
		printf( "tm_nt csr.map: NtMapViewOfSection(the section, -1, &base (0), 0, 0, NULL, &size (0), ViewUnmap 2, 0, PAGE_READONLY) -> status %08lXH, ViewSize %zXH, base 64K-aligned %s",
				(unsigned long)st, vs, ( (ULONG_PTR)base & 0xFFFF ) ? "no" : "yes" );
		if ( st >= 0 )
		{
			memset( &mbi, 0, sizeof( mbi ) );
			NtQueryVirtualMemory( (HANDLE)(LONG_PTR)-1, base, 0, &mbi, sizeof( mbi ), &rl );
			printf( "; MBI AllocationProtect %lXH, State %lXH, Protect %lXH, Type %lXH, RegionSize %zXH (contents not read); NtUnmapViewOfSection -> %08lXH", (unsigned long)mbi.AllocationProtect,
					(unsigned long)mbi.State, (unsigned long)mbi.Protect, (unsigned long)mbi.Type, mbi.RegionSize, (unsigned long)NtUnmapViewOfSection( (HANDLE)(LONG_PTR)-1, base ) );
		}
		printf( "\n" );
		NtClose( s );
	}
}

/* phnt ntmmapi.h:1391-1399 NtCreateSection (the WDK's ZwCreateSection documents it: a page-file-backed section when FileHandle is NULL) */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtCreateSection( PHANDLE SectionHandle, ACCESS_MASK DesiredAccess, PVOID ObjectAttributes, PLARGE_INTEGER MaximumSize,
														   ULONG SectionPageProtection, ULONG AllocationAttributes, HANDLE FileHandle );
static void case_sec( void )
{
	/* kernelbase's call (P39 trace i35, system call 97): NtCreateSection(&s, F001FH SECTION_ALL_ACCESS, NULL, &MaximumSize, PAGE_READWRITE 4,
	 * SEC_COMMIT 8000000H, FileHandle NULL) - here with MaximumSize 10000H / 20000H; printed: status, handle shape, MaximumSize after;
	 * then a view of it (ViewUnmap 2, PAGE_READWRITE): status, ViewSize, base alignment, MBI, the first and last byte's value (zero?) */
	static const LONGLONG sizes[] = { 0x10000, 0x20000 };
	size_t q;
	for ( q = 0; q < 2; q++ )
	{
		HANDLE s = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		LARGE_INTEGER ms;
		TM_NTSTATUS st;
		ms.QuadPart = sizes[ q ];
		st = NtCreateSection( &s, 0xF001F, NULL, &ms, 4, 0x8000000, NULL );
		printf( "tm_nt sec.%llx: NtCreateSection(&s, F001FH, NULL, &%llXH, 4, SEC_COMMIT, NULL) -> status %08lXH, handle %s, MaximumSize after %llXH", (unsigned long long)sizes[ q ],
				(unsigned long long)sizes[ q ], (unsigned long)st, tm_hshape( s ), (unsigned long long)ms.QuadPart );
		if ( st >= 0 )
		{
			PVOID base = NULL;
			SIZE_T vs = 0, rl = 0;
			MEMORY_BASIC_INFORMATION mbi;
			st = NtMapViewOfSection( s, (HANDLE)(LONG_PTR)-1, &base, 0, 0, NULL, &vs, 2, 0, 4 );
			printf( "; map (ViewUnmap, PAGE_READWRITE) -> status %08lXH, ViewSize %zXH, 64K-aligned %s", (unsigned long)st, vs, ( (ULONG_PTR)base & 0xFFFF ) ? "no" : "yes" );
			if ( st >= 0 )
			{
				memset( &mbi, 0, sizeof( mbi ) );
				NtQueryVirtualMemory( (HANDLE)(LONG_PTR)-1, base, 0, &mbi, sizeof( mbi ), &rl );
				printf( ", MBI AllocationProtect %lXH State %lXH Protect %lXH Type %lXH RegionSize %zXH, first byte %02X, last byte %02X; unmap -> %08lXH", (unsigned long)mbi.AllocationProtect,
						(unsigned long)mbi.State, (unsigned long)mbi.Protect, (unsigned long)mbi.Type, mbi.RegionSize, ( (unsigned char*)base )[ 0 ], ( (unsigned char*)base )[ vs - 1 ],
						(unsigned long)NtUnmapViewOfSection( (HANDLE)(LONG_PTR)-1, base ) );
			}
			NtClose( s );
		}
		printf( "\n" );
	}
}

static void case_wcp( void )
{
	/* the loader's call (P39 trace i5, system call 37): WaitCompletionPacketHandle, DesiredAccess 1, ObjectAttributes NULL */
	HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st = NtCreateWaitCompletionPacket( &h, 1, NULL );
	printf( "tm_nt wcp: NtCreateWaitCompletionPacket(&h, DesiredAccess 1, NULL) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( h ) );
	if ( st >= 0 ) printf( "tm_nt wcp.close: NtClose(the packet) -> status %08lXH\n", (unsigned long)NtClose( h ) );
	h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	st = NtCreateWaitCompletionPacket( &h, 0x1F0003, NULL );
	printf( "tm_nt wcp.all: NtCreateWaitCompletionPacket(&h, DesiredAccess 1F0003H, NULL) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( h ) );
	if ( st >= 0 ) NtClose( h );
}

/* ------------------------------------------------------------------------------------------------ NtQuerySystemInformationEx
 * phnt ntexapi.h:8362-8369; class 6BH SystemLogicalProcessorAndGroupInformation (ntexapi.h:2478; input LOGICAL_PROCESSOR_RELATIONSHIP)
 * - output SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX records (um\winnt.h, documented with GetLogicalProcessorInformationEx). The
 * processor topology of the environment (no identity): printed as the raw bytes written (hex), after the status / ReturnLength. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQuerySystemInformationEx( ULONG SystemInformationClass, PVOID InputBuffer, ULONG InputBufferLength, PVOID SystemInformation,
																	  ULONG SystemInformationLength, PULONG ReturnLength );
static void tm_qsix( const char* tag, ULONG cls, ULONG rel, ULONG len )
{
	ULONG ret = 0xCCCCCCCC, in = rel;
	TM_NTSTATUS st;
	size_t w, k;
	memset( g_buf, 0xCC, sizeof( g_buf ) );
	st = NtQuerySystemInformationEx( cls, &in, 4, len ? g_buf : NULL, len, &ret );
	w = tm_written( sizeof( g_buf ) );
	printf( "tm_nt %s: NtQuerySystemInformationEx(%lXH, input %lXH, Length %lXH) -> status %08lXH, ReturnLength %lXH, written %zXH", tag, (unsigned long)cls, (unsigned long)rel,
			(unsigned long)len, (unsigned long)st, (unsigned long)ret, w );
	if ( w )
	{
		printf( "; bytes" );
		for ( k = 0; k < w && k < 0x2000; k++ ) printf( " %02X", g_buf[ k ] );
	}
	printf( "\n" );
}

static void case_qsix( void )
{
	static const ULONG rels[] = { 0, 1, 2, 3, 4, 7, 0xFFFF };
	size_t i;
	for ( i = 0; i < sizeof( rels ) / sizeof( rels[ 0 ] ); i++ )
	{
		char tag[ 48 ];
		snprintf( tag, sizeof( tag ), "qsix.6b.%lx.50", (unsigned long)rels[ i ] );
		tm_qsix( tag, 0x6B, rels[ i ], 0x50 );
		snprintf( tag, sizeof( tag ), "qsix.6b.%lx.2000", (unsigned long)rels[ i ] );
		tm_qsix( tag, 0x6B, rels[ i ], 0x2000 );
		snprintf( tag, sizeof( tag ), "qsix.6b.%lx.0", (unsigned long)rels[ i ] );
		tm_qsix( tag, 0x6B, rels[ i ], 0 );
	}
}

/* ------------------------------------------------------------------------------------------------ what a CONNECTED CSR client sees (P40)
 * USER DECISION 2026-10-26 (plan 3.M5): measure what a process that its own loader connected to the session's CSR server SEES - this
 * program reads ONLY its own memory (its PEB, the read-only view of the CSR shared section its kernelbase mapped, its own ntdll's data,
 * its own handle table) after its own start-up; it NEVER sends a message to csrss, never opens csrss' process, never calls
 * NtConnectPort / NtRequestWaitReplyPort. Where the members are: the PEB (phnt ntpebteb.h:537 ReadOnlySharedMemoryBase 88H, :542
 * SharedData 90H, :547 ReadOnlyStaticServerData 98H, :882 CsrServerReadOnlySharedMemoryBase 380H); how ntdll / kernelbase use them -
 * the emulated trace of the real ntdll / kernelbase (P40 runs e2 / e3): ntdll CsrClientConnectToServer stores ConnectionInformation +0
 * in PEB 380H (the server's view base), maps the section anywhere (base 0, ViewUnmap, AllocationType 500000H, PAGE_READONLY) into PEB 88H,
 * sets PEB 98H = (+8) - (+0) + the client base, keeps ViewSize at ntdll+16CC38H, (+10H) at ntdll+16CC30H, the client view's
 * ViewRemoteBase - ViewBase at ntdll+16CC40H, the port handle at ntdll+16CC48H; kernelbase takes array[1] (BASESRV, ReactOS
 * csrmsg.h / base.h) - server pointers, converted by - PEB 380H + PEB 88H; the strings at +0 / +10H by + (array[1] - [BSSD+9E8H]).
 * The ntdll offsets hold only for the ntdll build of the capture (nt_measured.py runs the native reference only then).
 * Placeholders: every address as an offset from the shared view's base (the server's or the client's) or a relation; the server
 * process id and the port handle as shapes; the strings compared (equal / not) with this process' own GetWindowsDirectoryW(),
 * GetSystemDirectoryW() and "\Sessions\<the PEB SessionId>\BaseNamedObjects" - never printed. */
__declspec( dllimport ) HANDLE NTAPI CsrGetProcessId( void );   /* ntdll export; ReactOS subsystems/csr/csrlib/connect.c:505-510 */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryObject( HANDLE Handle, ULONG ObjectInformationClass, PVOID ObjectInformation, ULONG ObjectInformationLength,
														 PULONG ReturnLength );   /* phnt ntobapi.h:233 */

static void tm_rel( char* out, size_t cap, ULONG_PTR v, ULONG_PTR base, ULONG_PTR size )
{
	if ( !v ) snprintf( out, cap, "NULL" );
	else if ( v >= base && v - base < size ) snprintf( out, cap, "<base+%IXH>", v - base );
	else snprintf( out, cap, "<outside>" );
}

static int tm_ustr_eq( const TM_USTR* u, const WCHAR* client_buf, const wchar_t* want )
{
	size_t n = wcslen( want );
	return u->Length == n * 2 && client_buf && !_wcsnicmp( client_buf, want, n );
}

static void case_csrc( void )
{
	unsigned char* peb = (unsigned char*)(ULONG_PTR)__readgsqword( 0x60 );
	const ULONG_PTR base = *(ULONG_PTR*)( peb + 0x88 ), shared = *(ULONG_PTR*)( peb + 0x90 ), ssd = *(ULONG_PTR*)( peb + 0x98 ), srv = *(ULONG_PTR*)( peb + 0x380 );
	const ULONG sid = *(ULONG*)( peb + 0x2C0 );
	MEMORY_BASIC_INFORMATION mbi;
	SIZE_T rl = 0, vsize = 0;
	char a[ 64 ], b[ 64 ];
	unsigned char* nt = (unsigned char*)GetModuleHandleW( L"ntdll.dll" );
	int k;
	memset( &mbi, 0, sizeof( mbi ) );
	NtQueryVirtualMemory( (HANDLE)(LONG_PTR)-1, (PVOID)base, 0, &mbi, sizeof( mbi ), &rl );
	vsize = mbi.RegionSize;
	tm_rel( a, sizeof( a ), ssd, base, vsize );
	printf( "tm_nt csrc.peb: PEB ReadOnlySharedMemoryBase (88H) %s, 64K-aligned %s, MBI AllocationBase = it %s, AllocationProtect %lXH, State %lXH, Protect %lXH, Type %lXH, RegionSize %zXH; "
			"SharedData (90H) %s; ReadOnlyStaticServerData (98H) %s; CsrServerReadOnlySharedMemoryBase (380H) %s, 64K-aligned %s, = ReadOnlySharedMemoryBase %s\n",
			base ? "<va>" : "NULL", ( base & 0xFFFF ) ? "no" : "yes", mbi.AllocationBase == (PVOID)base ? "yes" : "no", (unsigned long)mbi.AllocationProtect, (unsigned long)mbi.State,
			(unsigned long)mbi.Protect, (unsigned long)mbi.Type, mbi.RegionSize, shared ? "<ptr>" : "NULL", a, srv ? "<va>" : "NULL", ( srv & 0xFFFF ) ? "no" : "yes",
			srv == base ? "yes" : "no" );
	if ( !base || !ssd || !srv ) return;
	/* the server-data pointer array: server addresses (relative to the server's view base) */
	printf( "tm_nt csrc.array: ReadOnlyStaticServerData[0..7] (server pointers):" );
	for ( k = 0; k < 8; k++ )
	{
		tm_rel( a, sizeof( a ), ( (ULONG_PTR*)ssd )[ k ], srv, vsize );
		printf( " [%d] %s", k, a );
	}
	printf( "\n" );
	{
		const ULONG_PTR a1 = ( (ULONG_PTR*)ssd )[ 1 ];
		const unsigned char* bssd = (const unsigned char*)( a1 - srv + base );
		const ULONG_PTR f9e8 = *(const ULONG_PTR*)( bssd + 0x9E8 );
		const ULONG_PTR adj = a1 - f9e8 - srv + base;   /* kernelbase's string conversion (P40 trace e3) */
		static const char* const names[ 3 ] = { "+0", "+10H", "+20H" };
		WCHAR wd[ MAX_PATH ], sd[ MAX_PATH ], bno[ 128 ];
		GetWindowsDirectoryW( wd, MAX_PATH );
		GetSystemDirectoryW( sd, MAX_PATH );
		_snwprintf( bno, 127, L"\\Sessions\\%lu\\BaseNamedObjects", (unsigned long)sid );
		tm_rel( a, sizeof( a ), a1, srv, vsize );
		tm_rel( b, sizeof( b ), f9e8, srv, vsize );
		printf( "tm_nt csrc.bssd: array[1] (BASE_STATIC_SERVER_DATA) %s; its +9E8H %s, = array[1] %s; within the view %s\n", a, b, f9e8 == a1 ? "yes" : "no",
				( a1 - srv + 0x1000 <= vsize ) ? "yes" : "no" );
		for ( k = 0; k < 3; k++ )
		{
			const TM_USTR* u = (const TM_USTR*)( bssd + 0x10 * k );
			const WCHAR* cb = u->Buffer ? (const WCHAR*)( (ULONG_PTR)u->Buffer + adj ) : NULL;
			const ULONG_PTR cbv = (ULONG_PTR)cb;
			const int in = cb && cbv >= base && cbv - base < vsize;
			tm_rel( a, sizeof( a ), (ULONG_PTR)u->Buffer, f9e8, 0x1000000 );
			printf( "tm_nt csrc.str%s: UNICODE_STRING at BSSD%s: Length %XH, MaximumLength %XH, Buffer = BSSD+9E8H-relative %s, converted inside the view %s; = GetWindowsDirectoryW() %s, "
					"= GetSystemDirectoryW() %s, = \"\\Sessions\\<the PEB SessionId>\\BaseNamedObjects\" %s\n",
					names[ k ], names[ k ], u->Length, u->MaximumLength, a, in ? "yes" : "no", in && tm_ustr_eq( u, cb, wd ) ? "yes" : "no", in && tm_ustr_eq( u, cb, sd ) ? "yes" : "no",
					in && tm_ustr_eq( u, cb, bno ) ? "yes" : "no" );
		}
		{
			/* the letter case of the parts after the Windows directory (the drive / directory name itself is the environment's
			 * SystemRoot, compared above): "\system32" / "\System32", "\Sessions\" / "\BaseNamedObjects" exactly (case-sensitive) */
			const TM_USTR* u0 = (const TM_USTR*)bssd;
			const TM_USTR* u1 = (const TM_USTR*)( bssd + 0x10 );
			const TM_USTR* u2 = (const TM_USTR*)( bssd + 0x20 );
			const WCHAR* c1 = (const WCHAR*)( (ULONG_PTR)u1->Buffer + adj );
			const WCHAR* c2 = (const WCHAR*)( (ULONG_PTR)u2->Buffer + adj );
			const int tail = u1->Length == u0->Length + 18;
			printf( "tm_nt csrc.case: BSSD+10H = BSSD+0 + 9 characters %s, tail \"\\system32\" (case-sensitive) %s, \"\\System32\" %s; BSSD+20H case-sensitive = \"\\Sessions\\<the PEB SessionId>\\BaseNamedObjects\" %s; "
					"the strings NUL-terminated (MaximumLength = Length + 2, a 0 character after) %s\n",
					tail ? "yes" : "no", tail && !wcsncmp( c1 + u0->Length / 2, L"\\system32", 9 ) ? "yes" : "no", tail && !wcsncmp( c1 + u0->Length / 2, L"\\System32", 9 ) ? "yes" : "no",
					u2->Length == wcslen( bno ) * 2 && !wcsncmp( c2, bno, wcslen( bno ) ) ? "yes" : "no",
					u0->MaximumLength == u0->Length + 2 && u1->MaximumLength == u1->Length + 2 && u2->MaximumLength == u2->Length + 2 &&
							!( (const WCHAR*)( (ULONG_PTR)u0->Buffer + adj ) )[ u0->Length / 2 ] && !c1[ u1->Length / 2 ] && !c2[ u2->Length / 2 ]
						? "yes"
						: "no" );
		}
		printf( "tm_nt csrc.words: BSSD +30H..+3EH (USHORT):" );
		for ( k = 0; k < 8; k++ ) printf( " %04X", *(const USHORT*)( bssd + 0x30 + 2 * k ) );
		printf( "\n" );
	}
	/* ntdll's own data (the connection's results, P40 trace e2 #1190648-#1190653) - the capture's ntdll build only */
	{
		const ULONG_PTR pid = *(ULONG_PTR*)( nt + 0x16CC30 ), vs = *(ULONG_PTR*)( nt + 0x16CC38 ), delta = *(ULONG_PTR*)( nt + 0x16CC40 );
		const HANDLE port = *(HANDLE*)( nt + 0x16CC48 );
		unsigned char ti[ 0x400 ];
		ULONG trl = 0;
		TM_NTSTATUS st;
		char tn[ 64 ] = "";
		memset( ti, 0, sizeof( ti ) );
		st = NtQueryObject( port, 2, ti, sizeof( ti ), &trl );   /* ObjectTypeInformation: OBJECT_TYPE_INFORMATION starts with TypeName (UNICODE_STRING) */
		if ( st >= 0 )
		{
			const TM_USTR* u = (const TM_USTR*)ti;
			for ( k = 0; k < u->Length / 2 && k < 60; k++ ) tn[ k ] = ( u->Buffer[ k ] >= 0x20 && u->Buffer[ k ] < 0x7F ) ? (char)u->Buffer[ k ] : '?';
			tn[ k ] = 0;
		}
		printf( "tm_nt csrc.ntdll: ntdll+16CC30H %s, = CsrGetProcessId() %s, multiple of 4 %s; ntdll+16CC38H %zXH (= the view's RegionSize %s); ntdll+16CC40H (client view remote delta) %s; "
				"ntdll+16CC48H (port handle) %s, NtQueryObject(ObjectTypeInformation) -> status %08lXH, type \"%s\"\n",
				pid ? "<non-zero>" : "0", pid == (ULONG_PTR)CsrGetProcessId() ? "yes" : "no", ( pid & 3 ) ? "no" : "yes", (size_t)vs, vs == vsize ? "yes" : "no", delta ? "<non-zero>" : "0",
				tm_hshape( port ), (unsigned long)st, tn );
	}
	/* the loader's map call itself on a second view of the section (P40 trace e1, system call 100): base 0, ViewUnmap 2, AllocationType
	 * 500000H, PAGE_READONLY; the second view's contents are not read */
	{
		static wchar_t name[ 128 ];
		TM_USTR n;
		TM_OA oa;
		HANDLE s = NULL;
		PVOID vb = NULL;
		SIZE_T sz = 0;
		TM_NTSTATUS st;
		_snwprintf( name, 127, L"\\Sessions\\%lu\\Windows\\SharedSection", (unsigned long)sid );
		tm_ustr( &n, name );
		tm_oa( &oa, NULL, &n );
		st = NtOpenSection( &s, 4, &oa );
		if ( st >= 0 )
		{
			st = NtMapViewOfSection( s, (HANDLE)(LONG_PTR)-1, &vb, 0, 0, NULL, &sz, 2, 0x500000, 2 );
			memset( &mbi, 0, sizeof( mbi ) );
			if ( st >= 0 ) NtQueryVirtualMemory( (HANDLE)(LONG_PTR)-1, vb, 0, &mbi, sizeof( mbi ), &rl );
			printf( "tm_nt csrc.map5: NtMapViewOfSection(the section, -1, &base (0), 0, 0, NULL, &size (0), ViewUnmap 2, 500000H, PAGE_READONLY) -> status %08lXH, ViewSize %zXH, "
					"64K-aligned %s, above the first view %s, MBI AllocationProtect %lXH State %lXH Protect %lXH Type %lXH RegionSize %zXH",
					(unsigned long)st, sz, ( (ULONG_PTR)vb & 0xFFFF ) ? "no" : "yes", (ULONG_PTR)vb > base ? "yes" : "no", (unsigned long)mbi.AllocationProtect, (unsigned long)mbi.State,
					(unsigned long)mbi.Protect, (unsigned long)mbi.Type, mbi.RegionSize );
			if ( st >= 0 ) printf( "; unmap -> %08lXH", (unsigned long)NtUnmapViewOfSection( (HANDLE)(LONG_PTR)-1, vb ) );
			printf( "\n" );
			NtClose( s );
		}
		else
			printf( "tm_nt csrc.map5: NtOpenSection -> status %08lXH\n", (unsigned long)st );
	}
}

/* ------------------------------------------------------------------------------------------------ NtInitializeNlsFiles (P40)
 * phnt ntexapi.h:9886-9891 NtInitializeNlsFiles( *BaseAddress, *DefaultLocaleId, *DefaultCasingTableSize (optional) ). kernelbase's call
 * (P40 trace e4, system call 103): &BaseAddress on the stack, DefaultLocaleId into its own data, DefaultCasingTableSize NULL. Measured
 * here with the same shape (twice): status, the view (alignment, MEMORY_BASIC_INFORMATION, size against the file
 * <SystemRoot>\System32\locale.nls, the bytes equal to the file's), the locale id as a relation only (= GetSystemDefaultLCID() /
 * GetUserDefaultLCID(): yes / no; the value is not printed). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtInitializeNlsFiles( PVOID* BaseAddress, PLCID DefaultLocaleId, PLARGE_INTEGER DefaultCasingTableSize );
static void case_nls( void )
{
	int q;
	WCHAR path[ MAX_PATH ];
	HANDLE f;
	DWORD fsz = 0, got = 0;
	static unsigned char file[ 0x200000 ];
	GetSystemDirectoryW( path, MAX_PATH );
	wcscat_s( path, MAX_PATH, L"\\locale.nls" );
	f = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
	if ( f != INVALID_HANDLE_VALUE )
	{
		fsz = GetFileSize( f, NULL );
		if ( fsz > sizeof( file ) || !ReadFile( f, file, fsz, &got, NULL ) ) got = 0;
		CloseHandle( f );
	}
	for ( q = 0; q < 2; q++ )
	{
		PVOID b = (PVOID)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		LCID lc = 0xCCCCCCCC;
		MEMORY_BASIC_INFORMATION mbi;
		SIZE_T rl = 0;
		TM_NTSTATUS st = NtInitializeNlsFiles( &b, &lc, NULL );
		memset( &mbi, 0, sizeof( mbi ) );
		if ( st >= 0 ) NtQueryVirtualMemory( (HANDLE)(LONG_PTR)-1, b, 0, &mbi, sizeof( mbi ), &rl );
		printf( "tm_nt nls.%d: NtInitializeNlsFiles(&base, &lcid, NULL) -> status %08lXH, base 64K-aligned %s, MBI AllocationBase = base %s, AllocationProtect %lXH, State %lXH, Protect %lXH, "
				"Type %lXH, RegionSize %zXH; locale.nls %lXH bytes (read %s), the view's first %lXH bytes = the file %s; lcid = GetSystemDefaultLCID() %s, = GetUserDefaultLCID() %s",
				q, (unsigned long)st, ( (ULONG_PTR)b & 0xFFFF ) ? "no" : "yes", mbi.AllocationBase == b ? "yes" : "no", (unsigned long)mbi.AllocationProtect, (unsigned long)mbi.State,
				(unsigned long)mbi.Protect, (unsigned long)mbi.Type, mbi.RegionSize, (unsigned long)fsz, got == fsz && fsz ? "yes" : "no", (unsigned long)fsz,
				st >= 0 && got == fsz && fsz && mbi.RegionSize >= fsz && !memcmp( b, file, fsz ) ? "yes" : "no", lc == GetSystemDefaultLCID() ? "yes" : "no",
				lc == GetUserDefaultLCID() ? "yes" : "no" );
		if ( st >= 0 ) printf( "; unmap -> %08lXH", (unsigned long)NtUnmapViewOfSection( (HANDLE)(LONG_PTR)-1, b ) );
		printf( "\n" );
	}
}

/* ------------------------------------------------------------------------------------------------ a process started WITHOUT a console
 * (P41, USER DECISION 2026-10-26 "started without a console"; ledger U2068): what a console-subsystem process sees when it is started
 * DETACHED - CreateProcessW( this image, ..., bInheritHandles FALSE, DETACHED_PROCESS (8H, um\WinBase.h "Process Creation Flags": "For
 * console processes, the new process does not inherit its parent's console (the default)"), environment / directory NULL, STARTUPINFOW
 * {cb only: dwFlags 0, no STARTF_USESTDHANDLES} ). The child measures ONLY ITSELF (its own PEB / process parameters, its own process
 * information, the console APIs on its own standard handles) and writes its lines into a file the parent names (the child has no
 * console and no standard handles to print to); the parent prints them. Never another process opened, never a console host started
 * (no AllocConsole / AttachConsole), nothing sent to csrss.
 * Layout: RTL_USER_PROCESS_PARAMETERS (phnt ntrtl.h:4438-4486; offsets of emu::win's checked table win_fields.hpp: ConsoleHandle 10H,
 * ConsoleFlags 18H, StandardInput 20H / StandardOutput 28H / StandardError 30H, WindowFlags A4H, ShowWindowFlags A8H); the console
 * handle constants phnt ntrtl.h:4433-4435 (RTL_USER_PROC_DETACHED_PROCESS -1, _CREATE_NEW_CONSOLE -2, _CREATE_NO_WINDOW -3);
 * ProcessConsoleHostProcess (31H) phnt ntpsapi.h:222 / :809-821 (PS_CONSOLE_HOST_PROCESS_MASK ~3, the low two bits flags).
 * Placeholders: handle values as their shape ("<handle> (non-zero, multiple of 4)"), a process id inside the 31H value as "<pid>" (only
 * its low two flag bits are printed); everything else printed is a constant of the start mode. */
/* phnt ntioapi.h:2911-2921 */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtWriteFile( HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, PVOID IoStatusBlock, PVOID Buffer, ULONG Length,
													   PLARGE_INTEGER ByteOffset, PULONG Key );
static char g_con[ 0x4000 ];
static size_t g_con_n;

static void tm_con_say( const char* fmt, ... )
{
	va_list ap;
	int n;
	va_start( ap, fmt );
	n = vsnprintf( g_con + g_con_n, sizeof( g_con ) - g_con_n - 2, fmt, ap );
	va_end( ap );
	if ( n > 0 && g_con_n + (size_t)n < sizeof( g_con ) - 2 )
	{
		g_con_n += (size_t)n;
		g_con[ g_con_n++ ] = '\n';
		g_con[ g_con_n ] = 0;
	}
}

static const char* tm_con_h( ULONG_PTR v )
{
	if ( v == 0 ) return "0";
	if ( v == (ULONG_PTR)-1 ) return "-1 (RTL_USER_PROC_DETACHED_PROCESS)";
	if ( v == (ULONG_PTR)-2 ) return "-2 (RTL_USER_PROC_CREATE_NEW_CONSOLE)";
	if ( v == (ULONG_PTR)-3 ) return "-3 (RTL_USER_PROC_CREATE_NO_WINDOW)";
	return ( v & 3 ) ? "<handle> (low bits set)" : "<handle> (non-zero, multiple of 4)";
}

static void tm_con_chp( const char* tag, ULONG len )
{
	ULONG_PTR v[ 3 ];
	ULONG ret = 0xCCCCCCCC;
	TM_NTSTATUS st;
	char shape[ 96 ];
	memset( v, 0xCC, sizeof( v ) );
	st = NtQueryInformationProcess( (HANDLE)(LONG_PTR)-1, 0x31, v, len, &ret );
	if ( st < 0 ) snprintf( shape, sizeof( shape ), "%s", v[ 0 ] == (ULONG_PTR)0xCCCCCCCCCCCCCCCCull ? "not written" : "written" );
	else if ( v[ 0 ] < 4 ) snprintf( shape, sizeof( shape ), "value %zXH", (size_t)v[ 0 ] );
	else snprintf( shape, sizeof( shape ), "value <pid> | %zXH (low two bits)", (size_t)( v[ 0 ] & 3 ) );
	tm_con_say( "tm_nt %s: NtQueryInformationProcess(NtCurrentProcess(), 31H ProcessConsoleHostProcess, Length %lXH) -> status %08lXH, ReturnLength %lXH, %s", tag,
				(unsigned long)len, (unsigned long)st, (unsigned long)ret, shape );
}

static void tm_con_child( const char* outfile, const char* parent_flags )
{
	unsigned char* peb = (unsigned char*)(ULONG_PTR)__readgsqword( 0x60 );
	unsigned char* pp = *(unsigned char**)( peb + 0x20 );
	HANDLE h[ 3 ], f;
	static const DWORD std_ids[ 3 ] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
	static const char* const std_names[ 3 ] = { "STD_INPUT_HANDLE", "STD_OUTPUT_HANDLE", "STD_ERROR_HANDLE" };
	DWORD e[ 3 ], n, mode, wr;
	BOOL ok;
	int i;
	/* ConsoleFlags is the STARTING process' (measured: 0 from a parent whose ConsoleFlags is 0, 1 from one whose is 1) - printed as that
	 * relation (the parent passes its own value) */
	tm_con_say( "tm_nt con.pp: RTL_USER_PROCESS_PARAMETERS ConsoleHandle %s, ConsoleFlags = the starting process' ConsoleFlags %s, StandardInput %s, StandardOutput %s, StandardError %s, "
				"WindowFlags %lXH, ShowWindowFlags %lXH",
				tm_con_h( *(ULONG_PTR*)( pp + 0x10 ) ), strtoul( parent_flags, NULL, 16 ) == *(ULONG*)( pp + 0x18 ) ? "yes" : "no", tm_con_h( *(ULONG_PTR*)( pp + 0x20 ) ), tm_con_h( *(ULONG_PTR*)( pp + 0x28 ) ),
				tm_con_h( *(ULONG_PTR*)( pp + 0x30 ) ), (unsigned long)*(ULONG*)( pp + 0xA4 ), (unsigned long)*(ULONG*)( pp + 0xA8 ) );
	tm_con_chp( "con.chp.8", 8 );
	tm_con_chp( "con.chp.4", 4 );
	tm_con_chp( "con.chp.10", 0x10 );
	{
		/* kernelbase's own call in a process started detached (the emulated trace of T2 / T3, P41 run c1, system call 104):
		 * NtSetInformationProcess( NtCurrentProcess(), 31H, &(ULONG_PTR)1, 8 ) - replayed here (kernelbase made it once before main);
		 * then the neighbouring lengths 4 / 10H and the value read back */
		ULONG_PTR one[ 2 ] = { 1, 0 };
		TM_NTSTATUS st = NtSetInformationProcess( (HANDLE)(LONG_PTR)-1, 0x31, one, 8 );
		tm_con_say( "tm_nt con.chps.8: NtSetInformationProcess(NtCurrentProcess(), 31H, value 1, Length 8H) -> status %08lXH", (unsigned long)st );
		st = NtSetInformationProcess( (HANDLE)(LONG_PTR)-1, 0x31, one, 4 );
		tm_con_say( "tm_nt con.chps.4: NtSetInformationProcess(NtCurrentProcess(), 31H, value 1, Length 4H) -> status %08lXH", (unsigned long)st );
		st = NtSetInformationProcess( (HANDLE)(LONG_PTR)-1, 0x31, one, 0x10 );
		tm_con_say( "tm_nt con.chps.10: NtSetInformationProcess(NtCurrentProcess(), 31H, value 1, Length 10H) -> status %08lXH", (unsigned long)st );
		tm_con_chp( "con.chp.after", 8 );
	}
	/* the documented console APIs without a console (each with LastError pre-set to 12345678H to see whether the call sets it) */
	for ( i = 0; i < 3; i++ )
	{
		SetLastError( 0x12345678 );
		h[ i ] = GetStdHandle( std_ids[ i ] );
		e[ i ] = GetLastError();
		tm_con_say( "tm_nt con.std.%d: GetStdHandle(%s) -> %s, LastError %lXH", i, std_names[ i ], h[ i ] == INVALID_HANDLE_VALUE ? "INVALID_HANDLE_VALUE" : tm_con_h( (ULONG_PTR)h[ i ] ),
					(unsigned long)e[ i ] );
	}
	SetLastError( 0x12345678 );
	tm_con_say( "tm_nt con.api.window: GetConsoleWindow() -> %s, LastError %lXH", GetConsoleWindow() ? "<hwnd>" : "NULL", (unsigned long)GetLastError() );
	SetLastError( 0x12345678 );
	n = GetConsoleCP();
	tm_con_say( "tm_nt con.api.cp: GetConsoleCP() -> %lXH, LastError %lXH", (unsigned long)n, (unsigned long)GetLastError() );
	SetLastError( 0x12345678 );
	n = GetConsoleOutputCP();
	tm_con_say( "tm_nt con.api.ocp: GetConsoleOutputCP() -> %lXH, LastError %lXH", (unsigned long)n, (unsigned long)GetLastError() );
	mode = 0xCCCCCCCC;
	SetLastError( 0x12345678 );
	ok = GetConsoleMode( h[ 1 ], &mode );
	tm_con_say( "tm_nt con.api.mode: GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) -> %d, LastError %lXH, mode %s", ok, (unsigned long)GetLastError(),
				mode == 0xCCCCCCCC ? "not written" : "written" );
	wr = 0xCCCCCCCC;
	SetLastError( 0x12345678 );
	ok = WriteConsoleW( h[ 1 ], L"tm", 2, &wr, NULL );
	tm_con_say( "tm_nt con.api.wcon: WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), L\"tm\", 2, &written, NULL) -> %d, LastError %lXH, written %s", ok, (unsigned long)GetLastError(),
				wr == 0xCCCCCCCC ? "not written" : ( wr ? "non-zero" : "0" ) );
	wr = 0xCCCCCCCC;
	SetLastError( 0x12345678 );
	ok = WriteFile( h[ 1 ], "tm", 2, &wr, NULL );
	tm_con_say( "tm_nt con.api.wfile: WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), \"tm\", 2, &written, NULL) -> %d, LastError %lXH, written %s", ok, (unsigned long)GetLastError(),
				wr == 0xCCCCCCCC ? "not written" : ( wr ? "non-zero" : "0" ) );
	SetLastError( 0x12345678 );
	n = GetFileType( h[ 1 ] );
	tm_con_say( "tm_nt con.api.ftype: GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) -> %lXH, LastError %lXH", (unsigned long)n, (unsigned long)GetLastError() );
	{
		/* the system service under WriteFile for the "no console" standard handle 0 (phnt ntioapi.h:2911-2921): IoStatusBlock pre-set CCH */
		unsigned char iosb[ 16 ];
		TM_NTSTATUS st;
		memset( iosb, 0xCC, sizeof( iosb ) );
		st = NtWriteFile( NULL, NULL, NULL, NULL, iosb, (PVOID) "tm", 2, NULL, NULL );
		tm_con_say( "tm_nt con.ntwf: NtWriteFile(NULL, NULL, NULL, NULL, &iosb, \"tm\", 2, NULL, NULL) -> status %08lXH, IoStatusBlock %s", (unsigned long)st,
					iosb[ 0 ] == 0xCC && iosb[ 8 ] == 0xCC ? "not written" : "written" );
	}
	tm_con_say( "tm_nt con.pp.after: ConsoleHandle %s, StandardOutput %s (after the API calls)", tm_con_h( *(ULONG_PTR*)( pp + 0x10 ) ), tm_con_h( *(ULONG_PTR*)( pp + 0x28 ) ) );
	f = CreateFileA( outfile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( f != INVALID_HANDLE_VALUE )
	{
		WriteFile( f, g_con, (DWORD)g_con_n, &wr, NULL );
		CloseHandle( f );
	}
}

/* the launcher: start an image DETACHED (see above) and wait; returns the exit code, or -1 if the start / wait failed */
/* P44 (U2115): the creation flags as a parameter - tm_detached = DETACHED_PROCESS (P41), tm_normal = none (an ordinary start) */
static long long tm_start( const wchar_t* image, wchar_t* cmdline, DWORD flags, DWORD* how )
{
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	DWORD code = 0xCCCCCCCC, w;
	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );
	memset( &pi, 0, sizeof( pi ) );
	if ( !CreateProcessW( image, cmdline, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi ) )
	{
		*how = GetLastError();
		return -1;
	}
	w = WaitForSingleObject( pi.hProcess, 60000 );
	if ( w != WAIT_OBJECT_0 || !GetExitCodeProcess( pi.hProcess, &code ) ) code = 0xCCCCCCCC;
	*how = w;
	CloseHandle( pi.hThread );
	CloseHandle( pi.hProcess );
	return code == 0xCCCCCCCC ? -1 : (long long)code;
}

static long long tm_detached( const wchar_t* image, wchar_t* cmdline, DWORD* how )
{
	return tm_start( image, cmdline, DETACHED_PROCESS, how );
}

static void case_con( void )
{
	wchar_t self[ MAX_PATH ], tmp[ MAX_PATH ], cmd[ 2 * MAX_PATH + 64 ];
	char outfile[ MAX_PATH ];
	DWORD how = 0, got = 0;
	long long code;
	HANDLE f;
	static char buf[ 0x4000 ];
	char* line;
	char* next;
	GetModuleFileNameW( NULL, self, MAX_PATH );
	GetTempPathW( MAX_PATH, tmp );
	if ( !GetTempFileNameW( tmp, L"tmc", 0, tmp ) )
	{
		printf( "tm_nt con.start: no temporary file (GetTempFileNameW failed %lXH)\n", (unsigned long)GetLastError() );
		return;
	}
	WideCharToMultiByte( CP_ACP, 0, tmp, -1, outfile, MAX_PATH, NULL, NULL );
	swprintf_s( cmd, sizeof( cmd ) / sizeof( cmd[ 0 ] ), L"\"%s\" con.child \"%s\" %lX", self, tmp,
				(unsigned long)*(ULONG*)( *(unsigned char**)( (unsigned char*)(ULONG_PTR)__readgsqword( 0x60 ) + 0x20 ) + 0x18 ) );   /* our own ConsoleFlags */
	code = tm_detached( self, cmd, &how );
	printf( "tm_nt con.start: CreateProcessW(this image, \"con.child <file>\", bInheritHandles FALSE, DETACHED_PROCESS 8H, STARTUPINFOW {cb only}) -> %s, wait %lXH, child exit code %s\n",
			code < 0 && how != WAIT_OBJECT_0 && how != WAIT_TIMEOUT ? "failed" : "ok", (unsigned long)how, code < 0 ? "none" : ( code == 0 ? "0" : "non-zero" ) );
	f = CreateFileW( tmp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
	if ( f != INVALID_HANDLE_VALUE )
	{
		if ( !ReadFile( f, buf, sizeof( buf ) - 1, &got, NULL ) ) got = 0;
		CloseHandle( f );
	}
	DeleteFileW( tmp );   /* our own temporary file, one file */
	buf[ got ] = 0;
	for ( line = buf; *line; line = next )
	{
		next = strchr( line, '\n' );
		if ( next ) *next++ = 0;
		else next = line + strlen( line );
		if ( *line ) printf( "%s\n", line );
	}
	if ( !got ) printf( "tm_nt con.child: no output\n" );
}

/* the detached NATIVE REFERENCE of the test PEs (P41): "tm_nt run.detached IMAGE" starts our own test PE IMAGE detached as above (no
 * command-line arguments) and prints its exit code - the harness passes only its own test PEs (t2_import.exe, t3_tls.vmware_guest.exe) */
static int tm_run_detached( const char* image )
{
	wchar_t w[ MAX_PATH ];
	const char* base = strrchr( image, '\\' );
	DWORD how = 0;
	long long code;
	MultiByteToWideChar( CP_ACP, 0, image, -1, w, MAX_PATH );
	code = tm_detached( w, NULL, &how );
	printf( "tm_nt run.detached: %s started with DETACHED_PROCESS -> %s, exit code %llXH\n", base ? base + 1 : image, code < 0 ? "failed" : "ok", code < 0 ? 0ull : (unsigned long long)code );
	return code < 0 ? 3 : 0;
}

/* ------------------------------------------------------------------------------------------------ user32's start (P44, U2115)
 * USER DECISION 2026-10-27 STEP 3: what an ordinary GUI program gets from user32's initialization - its CSR server connection and the
 * win32k start are made by Windows' own user32 code in our own GUI test PE tm_u32 (Emulator/tests/pe/tm_u32), started NORMALLY here
 * (CreateProcessW( tm_u32.exe beside this image, "tm_u32.exe <temporary file>", bInheritHandles FALSE, no creation flags, STARTUPINFOW
 * {cb only} )); it reads only its own memory and writes its lines to the temporary file, printed here (one file, deleted). "tm_nt
 * run.normal IMAGE" starts our own test PE IMAGE the same way (no arguments) and prints its exit code (the T6 native reference). */
static void case_u32( void )
{
	wchar_t self[ MAX_PATH ], img[ MAX_PATH ], tmp[ MAX_PATH ], cmd[ 2 * MAX_PATH + 64 ];
	wchar_t* slash;
	DWORD how = 0, got = 0;
	long long code;
	HANDLE f;
	static char buf[ 0x8000 ];
	char* line;
	char* next;
	GetModuleFileNameW( NULL, self, MAX_PATH );
	wcscpy_s( img, MAX_PATH, self );
	slash = wcsrchr( img, L'\\' );
	if ( !slash ) return;
	wcscpy_s( slash + 1, MAX_PATH - ( slash + 1 - img ), L"tm_u32.exe" );
	GetTempPathW( MAX_PATH, tmp );
	if ( !GetTempFileNameW( tmp, L"tmu", 0, tmp ) )
	{
		printf( "tm_nt u32.start: no temporary file (GetTempFileNameW failed %lXH)\n", (unsigned long)GetLastError() );
		return;
	}
	swprintf_s( cmd, sizeof( cmd ) / sizeof( cmd[ 0 ] ), L"\"%s\" %s", img, tmp );
	code = tm_start( img, cmd, 0, &how );
	printf( "tm_nt u32.start: CreateProcessW(tm_u32.exe, \"tm_u32.exe <file>\", bInheritHandles FALSE, creation flags 0, STARTUPINFOW {cb only}) -> %s, wait %lXH, exit code %s\n",
			code < 0 && how != WAIT_OBJECT_0 && how != WAIT_TIMEOUT ? "failed" : "ok", (unsigned long)how, code < 0 ? "none" : ( code == 0 ? "0" : "non-zero" ) );
	f = CreateFileW( tmp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
	if ( f != INVALID_HANDLE_VALUE )
	{
		if ( !ReadFile( f, buf, sizeof( buf ) - 1, &got, NULL ) ) got = 0;
		CloseHandle( f );
	}
	DeleteFileW( tmp );   /* our own temporary file, one file */
	buf[ got ] = 0;
	for ( line = buf; *line; line = next )
	{
		next = strchr( line, '\n' );
		if ( next ) *next++ = 0;
		else next = line + strlen( line );
		if ( *line && line[ strlen( line ) - 1 ] == '\r' ) line[ strlen( line ) - 1 ] = 0;   /* the child wrote its file in text mode (CR LF) */
		if ( *line ) printf( "%s\n", line );
	}
	if ( !got ) printf( "tm_nt u32.child: no output\n" );
}

static int tm_run_normal( const char* image )
{
	wchar_t w[ MAX_PATH ];
	const char* base = strrchr( image, '\\' );
	DWORD how = 0;
	long long code;
	MultiByteToWideChar( CP_ACP, 0, image, -1, w, MAX_PATH );
	code = tm_start( w, NULL, 0, &how );
	printf( "tm_nt run.normal: %s started normally (no creation flags) -> %s, exit code %llXH\n", base ? base + 1 : image, code < 0 ? "failed" : "ok", code < 0 ? 0ull : (unsigned long long)code );
	return code < 0 ? 3 : 0;
}

/* ------------------------------------------------------------------------------------------------ the process token (P41, U2073)
 * kernelbase's start in a process without a console asks its token (the emulated trace, P41 run c5 and the P40 exploration e10):
 * NtQueryInformationToken( NtCurrentProcessToken(), 1DH TokenIsAppContainer, 4 ), NtOpenProcessToken( NtCurrentProcess(), 8
 * TOKEN_QUERY ), then on that handle 0CH TokenSessionId / 1DH / 2AH TokenPrivateNameSpace, each with 4 bytes and a ReturnLength.
 * Prototypes phnt ntseapi.h:609-613 NtOpenProcessToken, :721-727 NtQueryInformationToken; classes ntseapi.h TOKEN_INFORMATION_CLASS
 * (TokenSessionId 12, TokenIsAppContainer 29, TokenPrivateNameSpace 42, TokenBnoIsolation 44). Printed: status, ReturnLength, bytes written; the session id
 * only as "= the PEB SessionId" yes / no (placeholder <the PEB SessionId>); the two flags (0 / 1: properties of a desktop process, no
 * identity) as values. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtOpenProcessToken( HANDLE ProcessHandle, ACCESS_MASK DesiredAccess, PHANDLE TokenHandle );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryInformationToken( HANDLE TokenHandle, ULONG TokenInformationClass, PVOID TokenInformation, ULONG TokenInformationLength,
																   PULONG ReturnLength );
static void tm_qit( const char* tag, HANDLE h, const char* hname, ULONG cls, ULONG len )
{
	ULONG ret = 0xCCCCCCCC;
	TM_NTSTATUS st;
	size_t w;
	const ULONG sid = *(ULONG*)( (unsigned char*)(ULONG_PTR)__readgsqword( 0x60 ) + 0x2C0 );
	memset( g_buf, 0xCC, sizeof( g_buf ) );
	st = NtQueryInformationToken( h, cls, len ? g_buf : NULL, len, &ret );
	w = tm_written( 0x200 );
	printf( "tm_nt %s: NtQueryInformationToken(%s, %lXH, Length %lXH) -> status %08lXH, ReturnLength %lXH, written %zXH", tag, hname, (unsigned long)cls, (unsigned long)len,
			(unsigned long)st, (unsigned long)ret, w );
	if ( st >= 0 && w >= 4 )
	{
		if ( cls == 0xC ) printf( "; value = the PEB SessionId %s", *(ULONG*)g_buf == sid ? "yes" : "no" );
		else if ( cls == 1 )   /* TOKEN_USER {SID_AND_ATTRIBUTES {PSID Sid, ULONG Attributes}} + the SID (um\winnt.h): the SID as its shape only */
		{
			const unsigned char* s = *(const unsigned char**)g_buf;
			char pv[ 64 ];
			tm_ptr( pv, sizeof( pv ), s );
			if ( s >= g_buf && s < g_buf + 0x100 )
			{
				const ULONG* sa = (const ULONG*)( s + 8 );
				const int n = s[ 1 ];
				printf( "; Sid %s, Attributes %lXH, SID Revision %u, SubAuthorityCount %d, IdentifierAuthority %u, SubAuthority[0] %lu, the RID (last) >= 1000 %s, "
						"the S-1-5-21 + domain + RID shape %s", pv, (unsigned long)*(ULONG*)( g_buf + 8 ), s[ 0 ], n, s[ 7 ], (unsigned long)( n ? sa[ 0 ] : 0 ), n && sa[ n - 1 ] >= 1000 ? "yes" : "no",
						n == 5 && s[ 7 ] == 5 && sa[ 0 ] == 21 ? "yes" : "no" );
			}
			else printf( "; Sid %s", pv );
		}
		else if ( cls == 0x2C )   /* TOKEN_BNO_ISOLATION_INFORMATION {PWSTR IsolationPrefix, BOOLEAN IsolationEnabled} (um\winnt.h) */
		{
			char pv[ 64 ];
			tm_ptr( pv, sizeof( pv ), *(void**)g_buf );
			printf( "; IsolationPrefix %s, IsolationEnabled %u, bytes +9..+FH %s", pv, g_buf[ 8 ], ( g_buf[ 9 ] == 0xCC && g_buf[ 15 ] == 0xCC ) ? "not written" : "written" );
		}
		else printf( "; value %lXH", (unsigned long)*(ULONG*)g_buf );
	}
	printf( "\n" );
}

static void case_tok( void )
{
	HANDLE t = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st;
	tm_qit( "tok.pt.1d", TM_CURRENT_PROCESS_TOKEN, "NtCurrentProcessToken()", 0x1D, 4 );
	tm_qit( "tok.pt.1d.0", TM_CURRENT_PROCESS_TOKEN, "NtCurrentProcessToken()", 0x1D, 0 );
	tm_qit( "tok.pt.1d.8", TM_CURRENT_PROCESS_TOKEN, "NtCurrentProcessToken()", 0x1D, 8 );
	st = NtOpenProcessToken( (HANDLE)(LONG_PTR)-1, 8, &t );
	printf( "tm_nt tok.open: NtOpenProcessToken(NtCurrentProcess(), 8 TOKEN_QUERY, &h) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( t ) );
	if ( st < 0 ) return;
	tm_qit( "tok.h.c", t, "the token handle", 0xC, 4 );
	tm_qit( "tok.h.1d", t, "the token handle", 0x1D, 4 );
	tm_qit( "tok.h.2a", t, "the token handle", 0x2A, 4 );
	tm_qit( "tok.h.c.0", t, "the token handle", 0xC, 0 );
	tm_qit( "tok.h.2a.0", t, "the token handle", 0x2A, 0 );
	tm_qit( "tok.h.2a.8", t, "the token handle", 0x2A, 8 );
	tm_qit( "tok.pt.c", TM_CURRENT_PROCESS_TOKEN, "NtCurrentProcessToken()", 0xC, 4 );
	/* trace c30, system call 172: TokenUser on NtCurrentThreadEffectiveToken() (-6, phnt ntpsapi.h:2491) with 58H; and 0 */
	tm_qit( "tok.eff.1", (HANDLE)(LONG_PTR)-6, "NtCurrentThreadEffectiveToken()", 1, 0x58 );
	tm_qit( "tok.eff.1.0", (HANDLE)(LONG_PTR)-6, "NtCurrentThreadEffectiveToken()", 1, 0 );
	tm_qit( "tok.pt.2a", TM_CURRENT_PROCESS_TOKEN, "NtCurrentProcessToken()", 0x2A, 4 );
	/* P41 (trace c10, system call 115): 2CH TokenBnoIsolation with a 120H buffer on the opened handle; and the neighbouring lengths */
	tm_qit( "tok.h.2c", t, "the token handle", 0x2C, 0x120 );
	tm_qit( "tok.h.2c.10", t, "the token handle", 0x2C, 0x10 );
	tm_qit( "tok.h.2c.0", t, "the token handle", 0x2C, 0 );
	tm_qit( "tok.h.2c.8", t, "the token handle", 0x2C, 8 );
	printf( "tm_nt tok.close: NtClose(the token handle) -> %08lXH\n", (unsigned long)NtClose( t ) );
	{
		HANDLE t2 = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		st = NtOpenProcessToken( (HANDLE)(LONG_PTR)-1, 0x20008, &t2 );
		printf( "tm_nt tok.open.read: NtOpenProcessToken(NtCurrentProcess(), 20008H, &h) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( t2 ) );
		if ( st >= 0 ) NtClose( t2 );
	}
}

/* ------------------------------------------------------------------------------------------------ NtAccessCheck (P41, U2074)
 * kernelbase's start asks whether the process token is a member of a group (the emulated trace, P41 run c7, system call 108: ntdll's
 * membership check): NtAccessCheck( an ABSOLUTE security descriptor {Revision 1, Control 4 SE_DACL_PRESENT, Owner = Group = the SID, no
 * SACL, a DACL {AclRevision 2, AclSize ECH, 1 ACE: ACCESS_ALLOWED type 0, flags 0, size 34H, mask 1, the SID}}, NtCurrentProcessToken(),
 * DesiredAccess 1, GenericMapping {20001H, 20000H, 20000H, 1F0001H} (ntdll's), PrivilegeSet (38H bytes), &PrivilegeSetLength = 38H,
 * &GrantedAccess, &AccessStatus ). The SID is S-1-5-32 followed by 8 sub-authorities - the shape of a capability GROUP SID derived from
 * a capability name (a hash; the same on every machine - a constant of the system, no identity). Prototype phnt ntseapi.h:977-986;
 * SECURITY_DESCRIPTOR / ACL / ACCESS_ALLOWED_ACE / SID: um\winnt.h. Replayed byte for byte (the SID's sub-authorities in tm_acc_sids);
 * printed: status, GrantedAccess, AccessStatus, PrivilegeSetLength after the call, bytes of PrivilegeSet written (all pre-set CCH). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtAccessCheck( PVOID SecurityDescriptor, HANDLE ClientToken, ACCESS_MASK DesiredAccess, PVOID GenericMapping, PVOID PrivilegeSet,
														 PULONG PrivilegeSetLength, PACCESS_MASK GrantedAccess, TM_NTSTATUS* AccessStatus );
/* the second check (system call 109): S-1-5-32-581 (BUILTIN, alias RID 581, um\winnt.h DOMAIN_ALIAS_RID_DEFAULT_ACCOUNT: a well-known group) */
static const ULONG tm_acc_sids[ 2 ][ 9 ] = { { 32, 1604681682u, 535129537u, 3273749797u, 3666938095u, 336295784u, 2177615760u, 2743807136u, 2867270584u }, { 32, 581 } };
static const unsigned char tm_acc_counts[ 2 ] = { 9, 2 };

static void tm_acc( const char* tag, int which, HANDLE token, const char* tname )
{
	static unsigned char sid[ 8 + 4 * 9 ], acl[ 0xEC ], ps[ 0x38 ];
	struct { unsigned char Revision, Sbz1; USHORT Control; PVOID Owner, Group, Sacl, Dacl; } sd;   /* um\winnt.h SECURITY_DESCRIPTOR (absolute) */
	static const ULONG gm[ 4 ] = { 0x20001, 0x20000, 0x20000, 0x1F0001 };
	ULONG psl = 0x38, granted = 0xCCCCCCCC, n = 0;
	TM_NTSTATUS ast = (TM_NTSTATUS)0xCCCCCCCC, st;
	int i;
	const ULONG* subs = tm_acc_sids[ which ];
	const unsigned char cnt = tm_acc_counts[ which ];
	memset( sid, 0, sizeof( sid ) );
	sid[ 0 ] = 1;
	sid[ 1 ] = cnt;
	memset( sid + 2, 0, 6 );
	sid[ 7 ] = 5;   /* SECURITY_NT_AUTHORITY {0,0,0,0,0,5} */
	memcpy( sid + 8, subs, 4 * cnt );
	memset( acl, 0, sizeof( acl ) );
	acl[ 0 ] = 2;
	*(USHORT*)( acl + 2 ) = 0xEC;
	*(USHORT*)( acl + 4 ) = 1;
	acl[ 8 ] = 0;      /* ACCESS_ALLOWED_ACE_TYPE */
	acl[ 9 ] = 0;
	*(USHORT*)( acl + 10 ) = (USHORT)( 8 + 8 + 4 * cnt );   /* 34H for 9 sub-authorities, 18H for 2 (the trace) */
	*(ULONG*)( acl + 12 ) = 1;
	memcpy( acl + 16, sid, 8 + 4 * cnt );
	memset( &sd, 0, sizeof( sd ) );
	sd.Revision = 1;
	sd.Control = 4;
	sd.Owner = sid;
	sd.Group = sid;
	sd.Dacl = acl;
	memset( ps, 0xCC, sizeof( ps ) );
	st = NtAccessCheck( &sd, token, 1, (PVOID)gm, ps, &psl, &granted, &ast );
	for ( i = 0; i < (int)sizeof( ps ); i++ )
		if ( ps[ i ] != 0xCC ) n = i + 1;
	printf( "tm_nt %s: NtAccessCheck(the loader's absolute SD (SID %d), %s, 1, ntdll's mapping, PrivilegeSet 38H) -> status %08lXH, GrantedAccess %lXH, "
			"AccessStatus %08lXH, PrivilegeSetLength %lXH, PrivilegeSet bytes written %lXH", tag, which + 1, tname, (unsigned long)st, (unsigned long)granted,
			(unsigned long)ast, (unsigned long)psl, (unsigned long)n );
	if ( n >= 8 ) printf( "; PrivilegeCount %lu, Control %lXH", (unsigned long)*(ULONG*)ps, (unsigned long)*(ULONG*)( ps + 4 ) );
	printf( "\n" );
}

static void case_acc( void )
{
	tm_acc( "acc.1", 0, TM_CURRENT_PROCESS_TOKEN, "NtCurrentProcessToken()" );
	tm_acc( "acc.2", 1, TM_CURRENT_PROCESS_TOKEN, "NtCurrentProcessToken()" );
}

/* ------------------------------------------------------------------------------------------------ the session's named-object directory
 * (P41, U2075) kernelbase's start opens the session's BaseNamedObjects directory (the emulated trace, P41 run c9, system call 114):
 * NtOpenDirectoryObject( &h, FH (DIRECTORY_QUERY | TRAVERSE | CREATE_OBJECT | CREATE_SUBDIRECTORY), {root NULL, "\Sessions\<the PEB
 * SessionId>\BaseNamedObjects", OBJ_CASE_INSENSITIVE 40H} ). Prototype phnt ntobapi.h:548-552. The name is printed with the session
 * number as the placeholder <the PEB SessionId> (the same string BASE_STATIC_SERVER_DATA +20H names, tm_nt csrc.str); printed: status,
 * the handle's shape, its object type (NtQueryObject ObjectTypeInformation, a system type name). */
static void tm_bno( const char* tag, ULONG access, int other_session )
{
	const ULONG sid = *(ULONG*)( (unsigned char*)(ULONG_PTR)__readgsqword( 0x60 ) + 0x2C0 );
	wchar_t name[ 96 ];
	TM_USTR u;
	TM_OA oa;
	HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st;
	swprintf_s( name, 96, L"\\Sessions\\%lu\\BaseNamedObjects", (unsigned long)( other_session ? sid + 7 : sid ) );
	tm_ustr( &u, name );
	tm_oa( &oa, NULL, &u );
	st = NtOpenDirectoryObject( &h, access, &oa );
	printf( "tm_nt %s: NtOpenDirectoryObject(&h, %lXH, \"\\Sessions\\%s\\BaseNamedObjects\" OBJ_CASE_INSENSITIVE) -> status %08lXH, handle %s", tag, (unsigned long)access,
			other_session ? "<another session number>" : "<the PEB SessionId>", (unsigned long)st, tm_hshape( h ) );
	if ( st >= 0 )
	{
		ULONG rl = 0;
		memset( g_buf, 0, 0x200 );
		if ( NtQueryObject( h, 2, g_buf, 0x200, &rl ) >= 0 )
		{
			const TM_USTR* tn = (const TM_USTR*)g_buf;
			char tnm[ 64 ];
			int k;
			for ( k = 0; k < tn->Length / 2 && k < 60; k++ ) tnm[ k ] = (char)tn->Buffer[ k ];
			tnm[ k ] = 0;
			printf( ", object type \"%s\"", tnm );
		}
		printf( "; NtClose -> %08lXH", (unsigned long)NtClose( h ) );
	}
	printf( "\n" );
}

static void case_bno( void )
{
	tm_bno( "bno.f", 0xF, 0 );
	tm_bno( "bno.3", 3, 0 );
	tm_bno( "bno.other", 0xF, 1 );
}

/* ------------------------------------------------------------------------------------------------ the WIL staging mutant (P41, U2076)
 * kernelbase's start creates a named mutant in the session's named-object directory (the emulated trace, P41 run c12, system call 117):
 * NtCreateMutant( &h, 1F0001H (MUTANT_ALL_ACCESS), {root = the "\Sessions\<the PEB SessionId>\BaseNamedObjects" handle, "Local\SM0:<the
 * process id>:304:WilStaging_02", OBJ_OPENIF 80H}, InitialOwner FALSE ). The name carries the creating process' id (a placeholder
 * "<the process id>" when printed). Prototype phnt ntexapi.h NtCreateMutant; OBJ_OPENIF shared\ntdef.h. In this process its own
 * kernelbase made that call during its start; replayed here: the same name (expected: the existing mutant opened), and a name of the same
 * shape no process uses ("...WilStaging_tm") for a first creation; printed: status, the handle shape, NtQueryMutant is not used. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtCreateMutant( PHANDLE MutantHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes, BOOLEAN InitialOwner );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtReleaseMutant( HANDLE MutantHandle, PLONG PreviousCount );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtWaitForSingleObject( HANDLE Handle, BOOLEAN Alertable, PLARGE_INTEGER Timeout );
static void tm_mut( const char* tag, HANDLE root, const char* suffix, const char* shown )
{
	wchar_t name[ 96 ];
	TM_USTR u;
	TM_OA oa;
	HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st;
	swprintf_s( name, 96, L"Local\\SM0:%lu:304:%S", (unsigned long)GetCurrentProcessId(), suffix );
	tm_ustr( &u, name );
	tm_oa( &oa, root, &u );
	oa.Attributes = 0x80;   /* OBJ_OPENIF, as the loader (no OBJ_CASE_INSENSITIVE) */
	st = NtCreateMutant( &h, 0x1F0001, &oa, FALSE );
	printf( "tm_nt %s: NtCreateMutant(&h, 1F0001H, {the BaseNamedObjects handle, \"Local\\SM0:<the process id>:304:%s\", OBJ_OPENIF}, FALSE) -> status %08lXH, handle %s",
			tag, shown, (unsigned long)st, tm_hshape( h ) );
	if ( st >= 0 ) printf( "; NtClose -> %08lXH", (unsigned long)NtClose( h ) );
	printf( "\n" );
}

static void case_mut( void )
{
	const ULONG sid = *(ULONG*)( (unsigned char*)(ULONG_PTR)__readgsqword( 0x60 ) + 0x2C0 );
	wchar_t name[ 96 ];
	TM_USTR u;
	TM_OA oa;
	HANDLE d = NULL;
	swprintf_s( name, 96, L"\\Sessions\\%lu\\BaseNamedObjects", (unsigned long)sid );
	tm_ustr( &u, name );
	tm_oa( &oa, NULL, &u );
	if ( NtOpenDirectoryObject( &d, 0xF, &oa ) < 0 )
	{
		printf( "tm_nt mut: the BaseNamedObjects directory did not open\n" );
		return;
	}
	tm_mut( "mut.loader", d, "WilStaging_02", "WilStaging_02" );
	tm_mut( "mut.fresh", d, "WilStaging_tm", "WilStaging_tm" );
	tm_mut( "mut.fresh2", d, "WilStaging_tm", "WilStaging_tm" );
	{
		/* the loader's next call (trace c13, system call 118): NtWaitForSingleObject( the mutant, FALSE, NULL ) - acquiring it; then
		 * NtReleaseMutant( h, &PreviousCount ) (phnt ntexapi.h); waits / releases on a fresh mutant of the same shape, counts printed */
		HANDLE h = NULL;
		wchar_t wname[ 96 ];
		LONG prev;
		TM_NTSTATUS st;
		int k;
		swprintf_s( wname, 96, L"Local\\SM0:%lu:304:WilStaging_tw", (unsigned long)GetCurrentProcessId() );
		tm_ustr( &u, wname );
		tm_oa( &oa, d, &u );
		oa.Attributes = 0x80;
		st = NtCreateMutant( &h, 0x1F0001, &oa, FALSE );
		printf( "tm_nt mut.wcreate: a fresh mutant of the loader's shape -> status %08lXH\n", (unsigned long)st );
		for ( k = 0; k < 2; k++ )
			printf( "tm_nt mut.wait%d: NtWaitForSingleObject(the mutant, FALSE, NULL) -> status %08lXH\n", k, (unsigned long)NtWaitForSingleObject( h, FALSE, NULL ) );
		for ( k = 0; k < 3; k++ )
		{
			prev = (LONG)0xCCCCCCCC;
			st = NtReleaseMutant( h, &prev );
			printf( "tm_nt mut.rel%d: NtReleaseMutant(the mutant, &PreviousCount) -> status %08lXH, PreviousCount %s%lXH\n", k, (unsigned long)st, prev == (LONG)0xCCCCCCCC ? "not written " : "",
					(unsigned long)prev );
		}
		NtClose( h );
	}
	NtClose( d );
}

/* ------------------------------------------------------------------------------------------------ the WIL staging objects (P41, U2077)
 * After the staging mutant, kernelbase's start opens named semaphores beside it (the emulated trace, P41 run c15, system call 119):
 * NtOpenSemaphore( &h, 1F0003H, {root = the BaseNamedObjects handle, "Local\SM0:<the process id>:304:WilStaging_02_p0", attributes 0} ).
 * In this process its own kernelbase created those objects during its start, so the loader's own names exist here; the FIRST-time
 * answers are measured with names of the same shape that no process uses ("...WilStaging_tm_p0"). Prototypes phnt ntexapi.h
 * NtOpenSemaphore / NtCreateSemaphore. Printed: status, the handle shape (the handle written or not). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtOpenSemaphore( PHANDLE SemaphoreHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtCreateSemaphore( PHANDLE SemaphoreHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes, LONG InitialCount, LONG MaximumCount );
static HANDLE g_wil_bno;

static void tm_wil_open( const char* tag, const char* suffix, ULONG attributes )
{
	wchar_t wname[ 96 ];
	TM_USTR u;
	TM_OA oa;
	HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st;
	swprintf_s( wname, 96, L"Local\\SM0:%lu:304:%S", (unsigned long)GetCurrentProcessId(), suffix );
	tm_ustr( &u, wname );
	tm_oa( &oa, g_wil_bno, &u );
	oa.Attributes = attributes;
	st = NtOpenSemaphore( &h, 0x1F0003, &oa );
	printf( "tm_nt %s: NtOpenSemaphore(&h, 1F0003H, {the BaseNamedObjects handle, \"Local\\SM0:<the process id>:304:%s\", attributes %lXH}) -> status %08lXH, handle %s", tag, suffix,
			(unsigned long)attributes, (unsigned long)st, tm_hshape( h ) );
	if ( st >= 0 ) printf( "; NtClose -> %08lXH", (unsigned long)NtClose( h ) );
	printf( "\n" );
}

static void case_wil( void )
{
	const ULONG sid = *(ULONG*)( (unsigned char*)(ULONG_PTR)__readgsqword( 0x60 ) + 0x2C0 );
	wchar_t wname[ 96 ];
	TM_USTR u;
	TM_OA oa;
	swprintf_s( wname, 96, L"\\Sessions\\%lu\\BaseNamedObjects", (unsigned long)sid );
	tm_ustr( &u, wname );
	tm_oa( &oa, NULL, &u );
	if ( NtOpenDirectoryObject( &g_wil_bno, 0xF, &oa ) < 0 )
	{
		printf( "tm_nt wil: the BaseNamedObjects directory did not open\n" );
		return;
	}
	tm_wil_open( "wil.open.loader", "WilStaging_02_p0", 0 );
	tm_wil_open( "wil.open.fresh", "WilStaging_tm_p0", 0 );
	{
		/* the loader's next call (trace c16, system call 120): NtCreateSemaphore( &h, 1F0003H, {the BaseNamedObjects handle, the same
		 * "..._p0" name, OBJ_OPENIF 80H}, InitialCount 28CF0H, MaximumCount 28CF0H ); replayed with a fresh name (first creation), again
		 * while the first handle is open, and with the loader's own name (it exists in this process) */
		HANDLE h1 = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull, h2 = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull, h3 = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		wchar_t wname2[ 96 ];
		TM_USTR u2;
		TM_OA oa2;
		TM_NTSTATUS s1, s2, s3;
		swprintf_s( wname2, 96, L"Local\\SM0:%lu:304:WilStaging_tm_p0", (unsigned long)GetCurrentProcessId() );
		tm_ustr( &u2, wname2 );
		tm_oa( &oa2, g_wil_bno, &u2 );
		oa2.Attributes = 0x80;
		s1 = NtCreateSemaphore( &h1, 0x1F0003, &oa2, 0x28CF0, 0x28CF0 );
		s2 = NtCreateSemaphore( &h2, 0x1F0003, &oa2, 0x28CF0, 0x28CF0 );
		swprintf_s( wname2, 96, L"Local\\SM0:%lu:304:WilStaging_02_p0", (unsigned long)GetCurrentProcessId() );
		tm_ustr( &u2, wname2 );
		s3 = NtCreateSemaphore( &h3, 0x1F0003, &oa2, 0x28CF0, 0x28CF0 );
		printf( "tm_nt wil.create: NtCreateSemaphore(&h, 1F0003H, {the BaseNamedObjects handle, a fresh \"...WilStaging_tm_p0\", OBJ_OPENIF}, 28CF0H, 28CF0H) -> status %08lXH, handle %s; "
				"again while open -> status %08lXH, handle %s; the loader's \"...WilStaging_02_p0\" -> status %08lXH, handle %s\n", (unsigned long)s1, tm_hshape( h1 ), (unsigned long)s2,
				tm_hshape( h2 ), (unsigned long)s3, tm_hshape( h3 ) );
		if ( s1 >= 0 ) NtClose( h1 );
		if ( s2 >= 0 ) NtClose( h2 );
		if ( s3 >= 0 ) NtClose( h3 );
	}
	NtClose( g_wil_bno );
}

/* ------------------------------------------------------------------------------------------------ a WNF state query (P41, U2078)
 * kernelbase's start (after the WIL staging objects) queries a WNF state (the emulated trace, P41 run c18, system call 123):
 * NtQueryWnfStateData( &StateName = 0280032EA3BC0875H, &TypeId = {48F96463H-52D6-4E85 ... as the two qwords 4E8552D648F96463H,
 * 4E26174EC1F2029CH}, ExplicitScope NULL, &ChangeStamp, Buffer (1000H on the stack), &BufferLength = 1000H ). The state name and the
 * type id are constants of the system DLL (no identity). Prototype phnt ntexapi.h:1902-1909. Printed: status, ChangeStamp (pre-set
 * CCCCCCCCH: written / its value is a counter of the state's changes - printed as zero / non-zero only), BufferLength after the call,
 * buffer bytes written, and the data's size / non-zero byte count (the data itself is not printed). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryWnfStateData( const ULONGLONG* StateName, const void* TypeId, const void* ExplicitScope, PULONG ChangeStamp, PVOID Buffer, PULONG BufferLength );
static void tm_wnf( const char* tag, ULONG len, int with_type )
{
	const ULONGLONG name = with_type == 4 ? 0x418A073AA3BC1C75ull : with_type == 3 ? 0x418A073AA3BC88F5ull : with_type == 2 ? 0x418A073AA3BC7C75ull :
						   0x0280032EA3BC0875ull;   /* 2 / 3 / 4: the second / third / fourth state, no TypeId */
	static const ULONGLONG type[ 2 ] = { 0x4E8552D648F96463ull, 0x4E26174EC1F2029Cull };
	ULONG stamp = 0xCCCCCCCC, bl = len;
	TM_NTSTATUS st;
	size_t w, nz = 0, k;
	memset( g_buf, 0xCC, sizeof( g_buf ) );
	st = NtQueryWnfStateData( &name, with_type == 1 ? type : NULL, NULL, &stamp, len ? g_buf : NULL, &bl );
	w = tm_written( 0x2000 );
	for ( k = 0; k < w; k++ ) nz += g_buf[ k ] != 0;
	if ( with_type == 4 )
	{
		/* the fourth state CHANGES with the system's feature usage and per boot (a table: 44CH at the measurement 2026-10-26, 450H an
		 * hour later; 1EAH after the guest's restart, P43) - its SHAPE only (U2104, P43): the size non-zero and at most 1000H, written =
		 * the size, the table's header (bytes 0-9 and 12-15; bytes 10-11 are a count - 2 on 2026-10-26, 1 on 2026-10-27 - printed as
		 * zero / non-zero) */
		printf( "tm_nt %s: NtQueryWnfStateData(&418A073AA3BC1C75H, NULL, NULL, &stamp, %s, &BufferLength = %lXH) -> status %08lXH, ChangeStamp %s, BufferLength 1..1000H %s, "
				"written = BufferLength %s", tag, len ? "buffer" : "NULL", (unsigned long)len, (unsigned long)st, stamp == 0xCCCCCCCC ? "not written" : ( stamp ? "non-zero" : "0" ),
				bl >= 1 && bl <= 0x1000 ? "yes" : "no", w == ( st >= 0 ? bl : 0 ) ? "yes" : "no" );
		if ( st >= 0 && w >= 16 )
		{
			printf( "; header" );
			for ( k = 0; k < 10; k++ ) printf( " %02X", g_buf[ k ] );
			printf( " <count %s>", g_buf[ 10 ] | g_buf[ 11 ] ? "non-zero" : "0" );
			for ( k = 12; k < 16; k++ ) printf( " %02X", g_buf[ k ] );
		}
		putchar( 10 );
		return;
	}
	printf( "tm_nt %s: NtQueryWnfStateData(&%s, %s, NULL, &stamp, %s, &BufferLength = %lXH) -> status %08lXH, ChangeStamp %s, BufferLength %lXH, written %zXH, "
			"non-zero data bytes %zu\n", tag, with_type == 4 ? "418A073AA3BC1C75H" : with_type == 3 ? "418A073AA3BC88F5H" : with_type == 2 ? "418A073AA3BC7C75H" : "0280032EA3BC0875H", with_type == 1 ? "&the loader's TypeId" : "NULL", len ? "buffer" : "NULL",
			(unsigned long)len, (unsigned long)st,
			stamp == 0xCCCCCCCC ? "not written" : ( stamp ? "non-zero" : "0" ), (unsigned long)bl, w, nz );
	if ( st >= 0 && w <= 16 )
	{
		/* the state's data bytes when they are this small (8 bytes: a configuration value of the system, printed byte by byte) */
		printf( "tm_nt %s.data:", tag );
		for ( k = 0; k < w; k++ ) printf( " %02X", g_buf[ k ] );
		putchar( 10 );
	}
}

static void case_wnf( void )
{
	tm_wnf( "wnf.1000", 0x1000, 1 );
	tm_wnf( "wnf.0", 0, 1 );
	tm_wnf( "wnf.notype", 0x1000, 0 );
	/* P41 trace c21, system call 129: the state 418A073AA3BC7C75H, no TypeId, Buffer NULL, BufferLength 0 (a size probe); then 1000H */
	tm_wnf( "wnf2.0", 0, 2 );
	tm_wnf( "wnf2.1000", 0x1000, 2 );
	/* exploration x1 (system calls 131 / 133): the third state 418A073AA3BC88F5H (size probe), the second state with C8H */
	tm_wnf( "wnf2.c8", 0xC8, 2 );
	tm_wnf( "wnf3.0", 0, 3 );
	tm_wnf( "wnf3.c8", 0xC8, 3 );
	tm_wnf( "wnf3.1000", 0x1000, 3 );
	/* trace c37, system call 209 (ExitProcess): the fourth state 418A073AA3BC1C75H, 1000H; and the size probe */
	tm_wnf( "wnf4.1000", 0x1000, 4 );
	tm_wnf( "wnf4.0", 0, 4 );
}

/* ------------------------------------------------------------------------------------------------ the WNF notification event (P41, U2078)
 * After the state query, ntdll's WNF subscription code (kernelbase's start) creates a synchronization event and a wait completion packet
 * and calls NtSetWnfProcessNotificationEvent( the event ) (the emulated trace, P41 run c19, system call 126). Prototype phnt
 * ntexapi.h:2005-2007. In this process its own start already did so; replayed: a new synchronization event (not signaled), again, and
 * handle 0. Printed: status. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtSetWnfProcessNotificationEvent( HANDLE NotificationEvent );
static void case_wnfe( void )
{
	HANDLE e = NULL;
	TM_NTSTATUS st;
	NtCreateEvent( &e, 0x1F0003, NULL, 1, FALSE );
	st = NtSetWnfProcessNotificationEvent( e );
	printf( "tm_nt wnfe.set: NtSetWnfProcessNotificationEvent(a new synchronization event) -> status %08lXH\n", (unsigned long)st );
	st = NtSetWnfProcessNotificationEvent( e );
	printf( "tm_nt wnfe.again: NtSetWnfProcessNotificationEvent(the same event) -> status %08lXH\n", (unsigned long)st );
	st = NtSetWnfProcessNotificationEvent( NULL );
	printf( "tm_nt wnfe.null: NtSetWnfProcessNotificationEvent(NULL) -> status %08lXH\n", (unsigned long)st );
}

/* ------------------------------------------------------------------------------------------------ the WNF subscription (P41, U2078)
 * Then ntdll subscribes to the state it queried (the emulated trace, P41 run c20, system call 128): NtSubscribeWnfStateChange(
 * &0280032EA3BC0875H, ChangeStamp = the stamp its query returned, EventMask 11H, &SubscriptionId ). Prototype phnt ntexapi.h:1946-1951.
 * Replayed with the stamp of a fresh query of this process, and with the stamp 1 (the emulator's placeholder); printed: status, the
 * SubscriptionId as zero / non-zero (an identifier of the kernel's subscription object - never printed), and whether two ids differ. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtSubscribeWnfStateChange( const ULONGLONG* StateName, ULONG ChangeStamp, ULONG EventMask, PULONGLONG SubscriptionId );
static void case_wnfs( void )
{
	static const ULONGLONG name = 0x0280032EA3BC0875ull;
	static const ULONGLONG type[ 2 ] = { 0x4E8552D648F96463ull, 0x4E26174EC1F2029Cull };
	ULONG stamp = 0, bl = 0x1000;
	ULONGLONG id1 = 0xCCCCCCCCCCCCCCCCull, id2 = 0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st;
	NtQueryWnfStateData( &name, type, NULL, &stamp, g_buf, &bl );
	st = NtSubscribeWnfStateChange( &name, stamp, 0x11, &id1 );
	printf( "tm_nt wnfs.stamp: NtSubscribeWnfStateChange(&0280032EA3BC0875H, the current ChangeStamp, 11H, &id) -> status %08lXH, SubscriptionId %s\n", (unsigned long)st,
			id1 == 0xCCCCCCCCCCCCCCCCull ? "not written" : ( id1 ? "non-zero" : "0" ) );
	st = NtSubscribeWnfStateChange( &name, 1, 0x11, &id2 );
	printf( "tm_nt wnfs.one: NtSubscribeWnfStateChange(&0280032EA3BC0875H, 1, 11H, &id) -> status %08lXH, SubscriptionId %s, = the first %s\n", (unsigned long)st,
			id2 == 0xCCCCCCCCCCCCCCCCull ? "not written" : ( id2 ? "non-zero" : "0" ), id1 == id2 ? "yes" : "no" );
	{
		/* trace c22, system call 130: the second state 418A073AA3BC7C75H, ChangeStamp 0, EventMask 11H */
		static const ULONGLONG name2 = 0x418A073AA3BC7C75ull;
		ULONGLONG id3 = 0xCCCCCCCCCCCCCCCCull;
		st = NtSubscribeWnfStateChange( &name2, 0, 0x11, &id3 );
		printf( "tm_nt wnfs.two: NtSubscribeWnfStateChange(&418A073AA3BC7C75H, 0, 11H, &id) -> status %08lXH, SubscriptionId %s, = the first state's %s\n", (unsigned long)st,
				id3 == 0xCCCCCCCCCCCCCCCCull ? "not written" : ( id3 ? "non-zero" : "0" ), id3 == id1 ? "yes" : "no" );
		{
			/* exploration x1, system call 132: the third state 418A073AA3BC88F5H, ChangeStamp 0, EventMask 11H */
			static const ULONGLONG name3 = 0x418A073AA3BC88F5ull;
			ULONGLONG id4 = 0xCCCCCCCCCCCCCCCCull;
			st = NtSubscribeWnfStateChange( &name3, 0, 0x11, &id4 );
			printf( "tm_nt wnfs.three: NtSubscribeWnfStateChange(&418A073AA3BC88F5H, 0, 11H, &id) -> status %08lXH, SubscriptionId %s, = the first / second state's %s / %s\n", (unsigned long)st,
					id4 == 0xCCCCCCCCCCCCCCCCull ? "not written" : ( id4 ? "non-zero" : "0" ), id4 == id1 ? "yes" : "no", id4 == id3 ? "yes" : "no" );
		}
	}
}

/* ------------------------------------------------------------------------------------------------ NtSetTimer2 (P41, U2079)
 * The thread pool's timer is set during kernelbase's start (the emulated trace, P41 run c24, system call 135): NtSetTimer2( the timer2 of
 * NtCreateTimer2 (tm_nt tmr2: Attributes 8, access 100002H), &DueTime = relative 300 s (100-ns units), Period NULL, &{Version 0, the
 * 4 bytes 00 00 FB 7F (uninitialised), NoWakeTolerance 75 s} ). Prototype phnt ntexapi.h:1470-1475 (T2_SET_PARAMETERS_V0
 * :1430-1450). Replayed on a fresh timer2, twice; printed: statuses, whether DueTime changed. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtSetTimer2( HANDLE TimerHandle, PLARGE_INTEGER DueTime, PLARGE_INTEGER Period, PVOID Parameters );
static void case_t2s( void )
{
	HANDLE h = NULL;
	TM_NTSTATUS st;
	LARGE_INTEGER due;
	unsigned char prm[ 16 ] = { 0, 0, 0, 0, 0x00, 0x00, 0xFB, 0x7F, 0x80, 0x17, 0xB4, 0x2C, 0, 0, 0, 0 };   /* NoWakeTolerance 75 s */
	st = NtCreateTimer2( &h, NULL, NULL, 8, 0x100002 );
	printf( "tm_nt t2s.create: NtCreateTimer2(&h, NULL, NULL, 8, 100002H) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( h ) );
	if ( st < 0 ) return;
	due.QuadPart = -300ll * 10000 * 1000;   /* relative 300 s */
	st = NtSetTimer2( h, &due, NULL, prm );
	printf( "tm_nt t2s.set: NtSetTimer2(the timer, &(relative 300 s), NULL, &{0, 00 00 FB 7F, 75 s}) -> status %08lXH, DueTime %s\n", (unsigned long)st,
			due.QuadPart == -300ll * 10000 * 1000 ? "unchanged" : "changed" );
	st = NtSetTimer2( h, &due, NULL, prm );
	printf( "tm_nt t2s.again: NtSetTimer2(the same, again) -> status %08lXH\n", (unsigned long)st );
	printf( "tm_nt t2s.close: NtClose -> %08lXH\n", (unsigned long)NtClose( h ) );
}

/* ------------------------------------------------------------------------------------------------ the user's policy keys (P41, U2073)
 * After TokenUser, ntdll opens the USER's Safer policy key (the emulated trace, P41 run c31, system call 173): NtOpenKey( &h, 1
 * (KEY_QUERY_VALUE), {"\REGISTRY\USER\<the user SID>\Software\Policies\Microsoft\Windows\Safer\CodeIdentifiers", OBJ_CASE_INSENSITIVE} ).
 * The user SID is a real identifier: this process builds the path from its OWN token's SID in memory and prints it only as the
 * placeholder <the user SID>; printed: status, the handle's shape. Prototype phnt ntregapi.h NtOpenKey. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtOpenKey( PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes );
static int tm_user_sid( wchar_t* out, size_t cap )
{
	ULONG rl = 0;
	const unsigned char* s;
	const ULONG* sa;
	int n, k;
	size_t used;
	memset( g_buf, 0, 0x100 );
	if ( NtQueryInformationToken( (HANDLE)(LONG_PTR)-6, 1, g_buf, 0x100, &rl ) < 0 ) return 0;
	s = *(const unsigned char**)g_buf;
	if ( s < g_buf || s >= g_buf + 0x80 ) return 0;
	n = s[ 1 ];
	sa = (const ULONG*)( s + 8 );
	used = (size_t)swprintf_s( out, cap, L"S-%u-%u", s[ 0 ], s[ 7 ] );
	for ( k = 0; k < n && used < cap; k++ ) used += (size_t)swprintf_s( out + used, cap - used, L"-%lu", (unsigned long)sa[ k ] );
	return 1;
}

static void tm_hku( const char* tag, const wchar_t* rest, ACCESS_MASK access )
{
	wchar_t sid[ 128 ], path[ 512 ];
	TM_USTR u;
	TM_OA oa;
	HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st;
	if ( !tm_user_sid( sid, 128 ) )
	{
		printf( "tm_nt %s: the token's user SID could not be read\n", tag );
		return;
	}
	swprintf_s( path, 512, L"\\REGISTRY\\USER\\%s%s", sid, rest );
	tm_ustr( &u, path );
	tm_oa( &oa, NULL, &u );
	st = NtOpenKey( &h, access, &oa );
	printf( "tm_nt %s: NtOpenKey(&h, %lXH, \"\\REGISTRY\\USER\\<the user SID>%S\" OBJ_CASE_INSENSITIVE) -> status %08lXH, handle %s\n", tag, (unsigned long)access, rest, (unsigned long)st,
			tm_hshape( h ) );
	if ( st >= 0 ) NtClose( h );
}

static void case_hku( void )
{
	tm_hku( "hku.safer", L"\\Software\\Policies\\Microsoft\\Windows\\Safer\\CodeIdentifiers", 1 );
}

/* ------------------------------------------------------------------------------------------------ NtTestAlert (P41, U2081)
 * The end of the loader's process initialisation (the emulated trace, P41 run c35, system call 202 / 200): NtTestAlert() - prototype
 * phnt ntpsapi.h NtTestAlert (no arguments). Measured in a process whose thread has no queued APC and was not alerted: status, twice. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtTestAlert( void );
static void case_alert( void )
{
	TM_NTSTATUS a = NtTestAlert(), b = NtTestAlert();
	printf( "tm_nt alert: NtTestAlert() -> status %08lXH; again -> status %08lXH\n", (unsigned long)a, (unsigned long)b );
}

/* ------------------------------------------------------------------------------------------------ NtTerminateProcess( NULL ) (P41, U2082)
 * ExitProcess -> ntdll's RtlExitUserProcess first calls NtTerminateProcess( NULL, ExitStatus ) (the emulated trace, P41 run c36, system
 * call 204: ProcessHandle 0, 2BH) - "terminate every OTHER thread of the process" - and continues (DLL detach, then NtTerminateProcess(
 * NtCurrentProcess(), ...)). Prototype phnt ntpsapi.h NtTerminateProcess. Measured in this process (it has thread-pool threads of its
 * own start): the status, and that the calling thread continues. Run LAST in a process (case table order; nt_measured runs one case per
 * process). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtTerminateProcess( HANDLE ProcessHandle, TM_NTSTATUS ExitStatus );
static void case_term( void )
{
	TM_NTSTATUS st = NtTerminateProcess( NULL, 0x2B );
	printf( "tm_nt term.null: NtTerminateProcess(NULL, 2BH) -> status %08lXH; the calling thread continues: yes\n", (unsigned long)st );
}

/* ------------------------------------------------------------------------------------------------ time (P42, plan 3.M6 - the one virtual
 * time base): what a process reads as time, measured from inside (Windows time semantics for the emulated environment; USER DECISION
 * 2026-10-26: Microsoft docs + public symbols + our own test PE natively in the guest, decision 3.0.7). Sources: KUSER_SHARED_DATA layout
 * from the public symbols (emu/win_fields.hpp: TickCountLowDeprecated 0H, TickCountMultiplier 4H, InterruptTime 8H, SystemTime 14H,
 * TimeZoneBias 20H, QpcFrequency 300H, TickCount 320H, QpcData 3C6H, QpcShift 3C7H); QueryPerformanceFrequency / QueryPerformanceCounter
 * (um\profileapi.h), GetTickCount64 (um\sysinfoapi.h; phnt ntexapi.h:9700-9722 NtGetTickCount64: TickCountQuad * TickCountMultiplier >>
 * 24), GetSystemTimeAsFileTime (um\sysinfoapi.h); NtQueryPerformanceCounter (phnt ntexapi.h:2267-2284), NtQuerySystemTime (um\winternl.h,
 * phnt ntexapi.h:2194-2206), NtQueryTimerResolution (phnt ntexapi.h:2227-2242), NtDelayExecution (phnt ntexapi.h:26-47),
 * NtWaitForSingleObject (um\winternl.h); the hypervisor page SYSTEM_HYPERVISOR_USER_SHARED_DATA (phnt ntexapi.h:7139-7175: QpcMultiplier
 * +8H, QpcBias +10H). Printed: constants of the environment (frequencies, the multiplier, the timer resolutions, statuses) and RELATIONS
 * (yes / no, within a stated bound) - never a time value, an address or a handle. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryPerformanceCounter( PLARGE_INTEGER PerformanceCounter, PLARGE_INTEGER PerformanceFrequency );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQuerySystemTime( PLARGE_INTEGER SystemTime );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryTimerResolution( PULONG MaximumTime, PULONG MinimumTime, PULONG CurrentTime );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtDelayExecution( BOOLEAN Alertable, PLARGE_INTEGER DelayInterval );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtWaitForSingleObject( HANDLE Handle, BOOLEAN Alertable, PLARGE_INTEGER Timeout );

#define TM_KUSER ( (volatile unsigned char*)(ULONG_PTR)0x7FFE0000 )

/* a KSYSTEM_TIME {LowPart, High1Time, High2Time} read as the x64 ntdll does: retry until High1Time = High2Time */
static ULONGLONG tm_ksystem_time( ULONG off )
{
	for ( ;; )
	{
		const LONG hi = *(volatile LONG*)( TM_KUSER + off + 4 );
		const ULONG lo = *(volatile ULONG*)( TM_KUSER + off );
		if ( hi == *(volatile LONG*)( TM_KUSER + off + 8 ) ) return ( (ULONGLONG)(ULONG)hi << 32 ) | lo;
	}
}

static const char* tm_yes( int b ) { return b ? "yes" : "no"; }

static void case_time( void )
{
	LARGE_INTEGER f, c, c2, t;
	ULONGLONG it0, it1, st0, st1, tc, tq, mult, qpf_k, ft, gt;
	ULONG mx = 0, mn = 0, cur = 0;
	TM_NTSTATUS st;
	int k, ok;
	HANDLE ev = NULL;
	FILETIME fti;

	/* 1. the frequencies */
	f.QuadPart = 0;
	ok = QueryPerformanceFrequency( &f );
	qpf_k = *(volatile ULONGLONG*)( TM_KUSER + 0x300 );
	printf( "tm_nt time.qpf: QueryPerformanceFrequency -> %s, frequency %llXH; KUSER QpcFrequency %llXH; equal %s\n", ok ? "TRUE" : "FALSE",
			(unsigned long long)f.QuadPart, qpf_k, tm_yes( (ULONGLONG)f.QuadPart == qpf_k ) );
	c.QuadPart = 0;
	t.QuadPart = 0;
	st = NtQueryPerformanceCounter( &c, &t );
	printf( "tm_nt time.nqpc: NtQueryPerformanceCounter(&c, &f) -> status %08lXH, frequency %llXH, counter non-zero %s\n", (unsigned long)st,
			(unsigned long long)t.QuadPart, tm_yes( c.QuadPart != 0 ) );
	st = NtQueryPerformanceCounter( &c, NULL );
	printf( "tm_nt time.nqpc.nofreq: NtQueryPerformanceCounter(&c, NULL) -> status %08lXH\n", (unsigned long)st );
	st = NtQueryPerformanceCounter( NULL, &t );
	printf( "tm_nt time.nqpc.null: NtQueryPerformanceCounter(NULL, &f) -> status %08lXH\n", (unsigned long)st );

	/* 2. the KUSER constants and the tick count */
	mult = *(volatile ULONG*)( TM_KUSER + 4 );
	st = NtQueryTimerResolution( &mx, &mn, &cur );
	printf( "tm_nt time.res: NtQueryTimerResolution -> status %08lXH, MaximumTime %lXH, MinimumTime %lXH, CurrentTime in [MinimumTime, MaximumTime] %s; "
			"KUSER TickCountMultiplier %08llXH = (MaximumTime << 24) / 10000 %s\n",
			(unsigned long)st, (unsigned long)mx, (unsigned long)mn, tm_yes( cur >= mn && cur <= mx ), mult, tm_yes( mult == ( ( (ULONGLONG)mx << 24 ) / 10000 ) ) );
	printf( "tm_nt time.kuser: TickCountLowDeprecated %s, TimeZoneBias %s, QpcData %04XH, QpcShift %02XH\n", *(volatile ULONG*)( TM_KUSER + 0 ) ? "non-zero" : "0",
			tm_ksystem_time( 0x20 ) ? "non-zero" : "0", *(volatile USHORT*)( TM_KUSER + 0x3C6 ), *(volatile unsigned char*)( TM_KUSER + 0x3C7 ) );
	for ( k = 0, ok = 0; k < 100 && !ok; k++ )
	{
		tq = *(volatile ULONGLONG*)( TM_KUSER + 0x320 );
		gt = GetTickCount64();
		ok = tq == *(volatile ULONGLONG*)( TM_KUSER + 0x320 );
	}
	tc = ( ( tq & 0xFFFFFFFF ) * mult >> 24 ) + ( ( tq >> 32 ) * mult << 8 );
	printf( "tm_nt time.tick: GetTickCount64() = (TickCountQuad * TickCountMultiplier) >> 24 %s; TickCountQuad = InterruptTime / MaximumTime (within 1) %s\n",
			tm_yes( ok && gt == tc ), tm_yes( mx && ( tm_ksystem_time( 8 ) / mx - tq ) <= 1 ) );

	/* 3. QPC against the interrupt time (both 100 ns when the frequency is 10 MHz): bracketed reads; the difference QPC - InterruptTime is
	 *    a per-boot constant (the reset-based TSC counter runs ahead of the kernel's interrupt time) - printed as its sign and whether
	 *    it stays constant (within 1 ms over 5 reads 20 ms apart), never as a value */
	{
		LONGLONG d, dmin = 0, dmax = 0;
		int pos = 1;
		for ( k = 0; k < 5; k++ )
		{
			it0 = tm_ksystem_time( 8 );
			QueryPerformanceCounter( &c );
			it1 = tm_ksystem_time( 8 );
			d = c.QuadPart - (LONGLONG)( it0 / 2 + it1 / 2 );
			if ( !k || d < dmin ) dmin = d;
			if ( !k || d > dmax ) dmax = d;
			if ( d <= 0 ) pos = 0;
			Sleep( 20 );
		}
		printf( "tm_nt time.qpc.it: QueryPerformanceCounter - KUSER InterruptTime (10 MHz both): positive %s, constant within 1 ms over 5 reads 20 ms apart %s\n",
				tm_yes( f.QuadPart == 10000000 && pos ), tm_yes( f.QuadPart == 10000000 && dmax - dmin <= 10000 ) );
	}
	ok = 1;
	for ( k = 0; k < 5; k++ )
	{
		QueryPerformanceCounter( &c );
		NtQueryPerformanceCounter( &c2, NULL );
		QueryPerformanceCounter( &t );
		if ( !( c.QuadPart <= c2.QuadPart && c2.QuadPart <= t.QuadPart ) ) ok = 0;
	}
	printf( "tm_nt time.qpc.sys: QueryPerformanceCounter <= NtQueryPerformanceCounter <= QueryPerformanceCounter (5 times) %s\n", tm_yes( ok ) );

	/* 4. the system time */
	it0 = tm_ksystem_time( 8 );
	st0 = tm_ksystem_time( 0x14 );
	t.QuadPart = 0;
	st = NtQuerySystemTime( &t );
	st1 = tm_ksystem_time( 0x14 );
	GetSystemTimeAsFileTime( &fti );
	ft = ( (ULONGLONG)fti.dwHighDateTime << 32 ) | fti.dwLowDateTime;
	Sleep( 50 );
	it1 = tm_ksystem_time( 8 );
	gt = tm_ksystem_time( 0x14 );
	printf( "tm_nt time.st: NtQuerySystemTime(&t) -> status %08lXH, t in [KUSER SystemTime before, after] %s; GetSystemTimeAsFileTime >= that %s; "
			"SystemTime - InterruptTime constant within 1 ms over 50 ms %s\n", (unsigned long)st, tm_yes( (ULONGLONG)t.QuadPart >= st0 && (ULONGLONG)t.QuadPart <= st1 ),
			tm_yes( ft >= st1 ), tm_yes( (LONGLONG)( ( gt - it1 ) - ( st0 - it0 ) ) <= 10000 && (LONGLONG)( ( gt - it1 ) - ( st0 - it0 ) ) >= -10000 ) );
	st = NtQuerySystemTime( NULL );
	printf( "tm_nt time.st.null: NtQuerySystemTime(NULL) -> status %08lXH\n", (unsigned long)st );

	/* 5. waits that use time */
	it0 = tm_ksystem_time( 8 );
	t.QuadPart = -100000;   /* relative 10 ms */
	st = NtDelayExecution( FALSE, &t );
	it1 = tm_ksystem_time( 8 );
	printf( "tm_nt time.delay: NtDelayExecution(FALSE, relative 10 ms) -> status %08lXH, InterruptTime advanced >= 10 ms %s\n", (unsigned long)st, tm_yes( it1 - it0 >= 100000 ) );
	t.QuadPart = 0;
	st = NtDelayExecution( FALSE, &t );   /* a yield: STATUS_NO_YIELD_PERFORMED (40000024H, shared\ntstatus.h) when no other thread was ready */
	printf( "tm_nt time.delay.zero: NtDelayExecution(FALSE, 0) -> status 00000000H or 40000024H (STATUS_NO_YIELD_PERFORMED: no other thread ready) %s\n",
			tm_yes( st == 0 || st == 0x40000024 ) );
	t.QuadPart = -1;
	printf( "tm_nt time.delay.alert: NtDelayExecution(TRUE, relative 100 ns), no APC queued -> status %08lXH\n", (unsigned long)NtDelayExecution( TRUE, &t ) );
	t.QuadPart = 1;   /* absolute, 1601 */
	printf( "tm_nt time.delay.past: NtDelayExecution(FALSE, absolute 1) -> status %08lXH\n", (unsigned long)NtDelayExecution( FALSE, &t ) );
	t.QuadPart = (LONGLONG)tm_ksystem_time( 0x14 ) + 100000;   /* absolute, 10 ms ahead */
	it0 = tm_ksystem_time( 8 );
	st = NtDelayExecution( FALSE, &t );
	it1 = tm_ksystem_time( 8 );
	printf( "tm_nt time.delay.abs: NtDelayExecution(FALSE, absolute SystemTime + 10 ms) -> status %08lXH, InterruptTime advanced >= 9 ms %s\n", (unsigned long)st,
			tm_yes( it1 - it0 >= 90000 ) );
	printf( "tm_nt time.delay.null: NtDelayExecution(FALSE, NULL) -> status %08lXH\n", (unsigned long)NtDelayExecution( FALSE, NULL ) );
	st = NtCreateEvent( &ev, 0x1F0003, NULL, 0 /* NotificationEvent */, FALSE );
	it0 = tm_ksystem_time( 8 );
	t.QuadPart = -100000;
	st = NtWaitForSingleObject( ev, FALSE, &t );
	it1 = tm_ksystem_time( 8 );
	printf( "tm_nt time.wait: NtWaitForSingleObject(a non-signaled notification event, FALSE, relative 10 ms) -> status %08lXH, InterruptTime advanced >= 10 ms %s\n",
			(unsigned long)st, tm_yes( it1 - it0 >= 100000 ) );
	t.QuadPart = 1;
	printf( "tm_nt time.wait.past: NtWaitForSingleObject(the event, FALSE, absolute 1) -> status %08lXH\n", (unsigned long)NtWaitForSingleObject( ev, FALSE, &t ) );
	if ( ev ) NtClose( ev );

	/* 6. the TSC rate the environment states: the hypervisor page (QPC = umulh(TSC, QpcMultiplier) + QpcBias when KUSER QpcData has the
	 *    hypervisor bypass), cross-checked with RDTSC against QueryPerformanceCounter over 100 ms */
	{
		unsigned char* va = NULL;
		ULONG rl = 0;
		st = NtQuerySystemInformation( 0xC5, &va, 8, &rl );
		if ( st >= 0 && va && *(ULONGLONG*)( va + 8 ) )
		{
			const ULONGLONG m = *(ULONGLONG*)( va + 8 ), bias = *(ULONGLONG*)( va + 16 );
			unsigned __int64 rem = 0, hz;
			ULONGLONG tsc0, tsc1;
			LARGE_INTEGER q0, q1;
			double ratio;
			/* f * 2^64 / m: the TSC ticks per second that make umulh(TSC, m) count at f */
			hz = _udiv128( (unsigned __int64)f.QuadPart, 0, m, &rem );
			tsc0 = __rdtsc();
			QueryPerformanceCounter( &q0 );
			Sleep( 100 );
			tsc1 = __rdtsc();
			QueryPerformanceCounter( &q1 );
			ratio = (double)( tsc1 - tsc0 ) / ( (double)( q1.QuadPart - q0.QuadPart ) * (double)hz / (double)f.QuadPart );
			printf( "tm_nt time.tsc: hypervisor page QpcMultiplier non-zero, QpcBias %s; TSC frequency = QpcFrequency * 2^64 / QpcMultiplier = %llu.%llu MHz (to 0.1 MHz); "
					"RDTSC / QPC over 100 ms agrees within 0.1%% %s\n",
					bias ? "non-zero" : "0", (unsigned long long)( ( hz + 50000 ) / 100000 ) / 10, (unsigned long long)( ( hz + 50000 ) / 100000 ) % 10,
					tm_yes( ratio > 0.999 && ratio < 1.001 ) );
		}
		else
			printf( "tm_nt time.tsc: no hypervisor page timing (status %08lXH)\n", (unsigned long)st );
	}
	{
		unsigned int aux = 0xCCCCCCCC;
		(void)__rdtscp( &aux );
		printf( "tm_nt time.tscaux: RDTSCP ECX (IA32_TSC_AUX) = GetCurrentProcessorNumber() %s, bits 31:12 %s\n", tm_yes( ( aux & 0xFFF ) == GetCurrentProcessorNumber() ),
				( aux >> 12 ) ? "non-zero" : "0" );
	}
}

struct tm_case
{
	const char* name;
	void ( *fn )( void );
};
/* ------------------------------------------------------------------------------------------------ the DLL path search (P43, U2106)
 * The loader maps a system DLL that is NOT a KnownDll (sample run 8: d3d11, dxgi, dcomp, dwmapi, kernel.appcore; exploration x1 system
 * calls 360-372) with: NtQueryAttributesFile( "\??\<the image's directory>\<name>", attributes 840H (OBJ_CASE_INSENSITIVE |
 * OBJ_IGNORE_IMPERSONATED_DEVICEMAP) ) - not there; NtQueryAttributesFile( "\??\<SystemRoot>\SYSTEM32\<name>", 840H ); NtOpenFile( &h,
 * 100021H (SYNCHRONIZE | FILE_EXECUTE | FILE_READ_DATA), that path 840H, &iosb, ShareAccess 5 (READ | DELETE), OpenOptions 60H
 * (FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE) ); NtCreateSection( &s, 0DH, NULL, NULL, PAGE_EXECUTE (10H), SEC_IMAGE
 * (1000000H), h ); NtMapViewOfSection( s, NtCurrentProcess(), &base (0), 0, 0, NULL, &size (0), ViewShare, 800000H, 80H ); NtClose( s ),
 * NtClose( h ). Prototypes: phnt ntioapi.h NtQueryAttributesFile / NtOpenFile, ntmmapi.h NtCreateSection / NtMapViewOfSection (the WDK's
 * Zw* forms; FILE_BASIC_INFORMATION {CreationTime, LastAccessTime, LastWriteTime, ChangeTime, FileAttributes} - WDK wdm.h). Replayed here
 * in this process with the same arguments for the same five DLLs (none of them loaded by this test PE; the image's directory = this test
 * PE's own); printed: statuses, bytes written, the FileAttributes value, the four times only as zero / non-zero, handle shapes, the
 * view's shape as in kd.map (addresses and paths never printed). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryAttributesFile( TM_OA* ObjectAttributes, PVOID FileInformation );
static void case_sp( void )
{
	static const wchar_t* const dll[] = { L"d3d11.dll", L"dxgi.dll", L"dcomp.dll", L"dwmapi.dll", L"kernel.appcore.dll" };
	static wchar_t app[ 600 ], win[ 300 ], path[ 1000 ];
	DWORD al = GetModuleFileNameW( NULL, app, 600 );
	UINT wl = GetWindowsDirectoryW( win, 300 );
	size_t q, k;
	while ( al && app[ al - 1 ] != L'\\' ) app[ --al ] = 0;
	if ( al ) app[ --al ] = 0;   /* the directory without its final backslash */
	if ( !al || !wl ) { printf( "tm_nt sp: no directory\n" ); return; }
	for ( q = 0; q < sizeof( dll ) / sizeof( dll[ 0 ] ); q++ )
	{
		TM_USTR n;
		TM_OA oa;
		unsigned char fi[ 0x40 ];
		ULONG_PTR iosb[ 2 ];
		HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull, sct = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		TM_NTSTATUS st;
		size_t w;
		/* 1. the image's directory */
		wcscpy( path, L"\\??\\" );
		wcscat( path, app );
		wcscat( path, L"\\" );
		wcscat( path, dll[ q ] );
		tm_ustr( &n, path );
		tm_oa( &oa, NULL, &n );
		oa.Attributes = 0x840;
		memset( fi, 0xCC, sizeof( fi ) );
		st = NtQueryAttributesFile( &oa, fi );
		for ( w = 0, k = 0; k < sizeof( fi ); k++ ) w += fi[ k ] != 0xCC;
		printf( "tm_nt sp.%zu.app: NtQueryAttributesFile(\"\\??\\<the image's directory>\\", q );
		tm_wprint( dll[ q ], wcslen( dll[ q ] ) );
		{
			size_t lo = sizeof( fi ), hi = 0, nz = 0;
			for ( k = 0; k < sizeof( fi ); k++ )
				if ( fi[ k ] != 0xCC ) { if ( k < lo ) lo = k; hi = k; nz += fi[ k ] != 0; }
			printf( "\" 840H, buf) -> status %08lXH, bytes written %zu", (unsigned long)st, w );
			if ( w ) printf( " (offsets %zXH..%zXH, non-zero %zu)", lo, hi, nz );
			printf( "\n" );
		}
		/* 2. <SystemRoot>\SYSTEM32 */
		wcscpy( path, L"\\??\\" );
		wcscat( path, win );
		wcscat( path, L"\\SYSTEM32\\" );
		wcscat( path, dll[ q ] );
		tm_ustr( &n, path );
		memset( fi, 0xCC, sizeof( fi ) );
		st = NtQueryAttributesFile( &oa, fi );
		for ( w = 0, k = 0; k < sizeof( fi ); k++ ) w += fi[ k ] != 0xCC;
		printf( "tm_nt sp.%zu.sys: NtQueryAttributesFile(\"\\??\\<SystemRoot>\\SYSTEM32\\", q );
		tm_wprint( dll[ q ], wcslen( dll[ q ] ) );
		printf( "\" 840H, buf) -> status %08lXH, bytes written %zu", (unsigned long)st, w );
		if ( st >= 0 )
			printf( "; CreationTime %s, LastAccessTime %s, LastWriteTime %s, ChangeTime %s, FileAttributes %lXH, bytes 24H..27H %02X %02X %02X %02X",
					*(ULONGLONG*)( fi + 0 ) ? "non-zero" : "0", *(ULONGLONG*)( fi + 8 ) ? "non-zero" : "0", *(ULONGLONG*)( fi + 0x10 ) ? "non-zero" : "0",
					*(ULONGLONG*)( fi + 0x18 ) ? "non-zero" : "0", (unsigned long)*(ULONG*)( fi + 0x20 ), fi[ 0x24 ], fi[ 0x25 ], fi[ 0x26 ], fi[ 0x27 ] );
		printf( "\n" );
		/* 3. the open, the image section, the view */
		iosb[ 0 ] = iosb[ 1 ] = (ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		st = NtOpenFile( &h, 0x100021, &oa, iosb, 5, 0x60 );
		printf( "tm_nt sp.%zu.open: NtOpenFile(&h, 100021H, that path 840H, &iosb, 5, 60H) -> status %08lXH, handle %s, IoStatus.Status %08lXH, Information %llXH\n", q,
				(unsigned long)st, tm_hshape( h ), (unsigned long)(ULONG)iosb[ 0 ], (unsigned long long)iosb[ 1 ] );
		if ( st < 0 ) continue;
		st = NtCreateSection( &sct, 0xD, NULL, NULL, 0x10, 0x1000000, h );
		printf( "tm_nt sp.%zu.sec: NtCreateSection(&s, 0DH, NULL, NULL, 10H, 1000000H, the file) -> status %08lXH, handle %s\n", q, (unsigned long)st, tm_hshape( sct ) );
		if ( st >= 0 )
		{
			PVOID base = NULL;
			SIZE_T vs = 0, rl2 = 0;
			MEMORY_BASIC_INFORMATION mbi;
			st = NtMapViewOfSection( sct, (HANDLE)(LONG_PTR)-1, &base, 0, 0, NULL, &vs, 1, 0x800000, 0x80 );
			printf( "tm_nt sp.%zu.map: NtMapViewOfSection(the section, -1, &base (0), 0, 0, NULL, &size (0), ViewShare, 800000H, 80H) -> status %08lXH", q, (unsigned long)st );
			if ( st >= 0 && base )
			{
				const unsigned char* b = (const unsigned char*)base;
				const LONG lfanew = *(const LONG*)( b + 0x3C );
				const ULONGLONG hib = *(const ULONGLONG*)( b + lfanew + 0x18 + 0x18 );
				const ULONG soi = *(const ULONG*)( b + lfanew + 0x18 + 0x38 );
				printf( ", base <va> (64K-aligned %s), ViewSize %s SizeOfImage (%zXH vs %lXH), header ImageBase %s", ( (ULONG_PTR)base & 0xFFFF ) ? "no" : "yes", vs == soi ? "=" : "!=", vs,
						(unsigned long)soi, hib == (ULONGLONG)(ULONG_PTR)base ? "= the base" : "!= the base" );
				memset( &mbi, 0, sizeof( mbi ) );
				NtQueryVirtualMemory( (HANDLE)(LONG_PTR)-1, base, 0, &mbi, sizeof( mbi ), &rl2 );
				printf( "; MBI at the base: AllocationProtect %lXH, State %lXH, Protect %lXH, Type %lXH, RegionSize %zXH", (unsigned long)mbi.AllocationProtect, (unsigned long)mbi.State,
						(unsigned long)mbi.Protect, (unsigned long)mbi.Type, mbi.RegionSize );
				printf( "; NtUnmapViewOfSection -> %08lXH", (unsigned long)NtUnmapViewOfSection( (HANDLE)(LONG_PTR)-1, base ) );
			}
			printf( "\n" );
			printf( "tm_nt sp.%zu.close: NtClose(the section) -> %08lXH", q, (unsigned long)NtClose( sct ) );
		}
		else
			printf( "tm_nt sp.%zu.close:", q );
		printf( ", NtClose(the file) -> %08lXH\n", (unsigned long)NtClose( h ) );
	}
}

/* ------------------------------------------------------------------------------------------------ ProcessMitigationPolicy (P43, U2108)
 * user32's start (sample run 8, exploration x6 system call 487) queries NtQueryInformationProcess( NtCurrentProcess(), 34H
 * ProcessMitigationPolicy (phnt ntpsapi.h PROCESSINFOCLASS 52), {Policy 0BH, flags}, Length 8, ReturnLength NULL ) - Policy 0BH =
 * ProcessSystemCallFilterPolicy (um\winnt.h PROCESS_MITIGATION_POLICY; PROCESS_MITIGATION_SYSTEM_CALL_FILTER_POLICY {DWORD Flags:
 * FilterId 4 bits}); the documented user-mode form is GetProcessMitigationPolicy. Replayed for this process (an ordinary process, as the
 * emulated one): Length 8 with ReturnLength NULL (the call's) and set; printed: status, the flags, the policy field after the call. */
static void case_mit( void )
{
	ULONG b[ 4 ], rl;
	TM_NTSTATUS st;
	b[ 0 ] = 0xB; b[ 1 ] = 0xCCCCCCCC; b[ 2 ] = b[ 3 ] = 0xCCCCCCCC;
	st = NtQueryInformationProcess( (HANDLE)(LONG_PTR)-1, 0x34, b, 8, NULL );
	printf( "tm_nt mit.b: NtQueryInformationProcess(-1, 34H, {0BH ProcessSystemCallFilterPolicy, CCH}, 8, NULL) -> status %08lXH, Policy after %lXH, Flags %lXH, bytes 8.. %s\n",
			(unsigned long)st, (unsigned long)b[ 0 ], (unsigned long)b[ 1 ], b[ 2 ] == 0xCCCCCCCC ? "untouched" : "written" );
	b[ 0 ] = 0xB; b[ 1 ] = 0xCCCCCCCC; rl = 0xCCCCCCCC;
	st = NtQueryInformationProcess( (HANDLE)(LONG_PTR)-1, 0x34, b, 8, &rl );
	printf( "tm_nt mit.b.rl: the same with ReturnLength -> status %08lXH, Flags %lXH, ReturnLength %lXH\n", (unsigned long)st, (unsigned long)b[ 1 ], (unsigned long)rl );
	b[ 0 ] = 0xB; b[ 1 ] = 0xCCCCCCCC;
	st = NtQueryInformationProcess( (HANDLE)(LONG_PTR)-1, 0x34, b, 4, NULL );
	printf( "tm_nt mit.b.4: Length 4 -> status %08lXH, Flags %s\n", (unsigned long)st, b[ 1 ] == 0xCCCCCCCC ? "not written" : "written" );
	/* P44 (U2116): policy 6 ProcessExtensionPointDisablePolicy (um\winnt.h PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY) - kernel32's
	 * BasepAreExtensionPointsBlocked in user32's LoadAppInitDlls (P44 exploration x11, system call 316): Length 8, ReturnLength NULL */
	b[ 0 ] = 6; b[ 1 ] = 0xCCCCCCCC; b[ 2 ] = 0xCCCCCCCC;
	st = NtQueryInformationProcess( (HANDLE)(LONG_PTR)-1, 0x34, b, 8, NULL );
	printf( "tm_nt mit.6: NtQueryInformationProcess(-1, 34H, {6 ProcessExtensionPointDisablePolicy, CCH}, 8, NULL) -> status %08lXH, Policy after %lXH, Flags %lXH, bytes 8.. %s\n",
			(unsigned long)st, (unsigned long)b[ 0 ], (unsigned long)b[ 1 ], b[ 2 ] == 0xCCCCCCCC ? "untouched" : "written" );
}

/* ------------------------------------------------------------------------------------------------ user32's start: the kernel side (P44, U2116)
 * The services of an ordinary GUI program's start that are not win32k (P44 explorations x6-x14 of T6, each replayed in this process with
 * the arguments the emulated trace showed):
 *   sat.eff  - the loader (after user32's IMM lookup): NtQuerySecurityAttributesToken( NtCurrentThreadEffectiveToken(), "WIN://SYSAPPID" +
 *              "WIN://PKG" / "WIN://SYSAPPID", Length 330H ) (phnt ntseapi.h:947-954)
 *   svc.*    - kernel32 BasepIsServiceSidBlocked (user32's LoadAppInitDlls in its ClientThreadSetup): NtOpenProcessToken( -1, 0AH ),
 *              DuplicateToken( SecurityImpersonation ) (um\securitybaseapi.h; kernelbase builds NtDuplicateToken's object attributes),
 *              NtAccessCheck( an absolute SD - owner = group = a service SID, a DACL (AclSize 198H) of five ACCESS_ALLOWED ACEs (mask 1) for
 *              four service SIDs and S-1-5-83-0, as the trace shows it; the SIDs as their bytes), the duplicate, 1, the mapping {20001H, 20000H,
 *              20000H, 1F0001H}, PrivilegeSetLength 38H ) (phnt ntseapi.h:977-986)
 *   hklm.*   - kernelbase's HKEY_LOCAL_MACHINE: NtOpenKeyEx( "\REGISTRY\MACHINE", MAXIMUM_ALLOWED, options 0 ), NtQueryKey( it, 7
 *              KeyHandleTagsInformation, 4 ) (phnt ntregapi.h NtOpenKeyEx / NtQueryKey / KEY_INFORMATION_CLASS)
 *   tls.*    - kernelbase TlsFree (gdi32full's detach): NtSetInformationThread( NtCurrentThread(), 0AH ThreadZeroTlsCell, &index, 4 )
 *              (phnt ntpsapi.h THREADINFOCLASS) on an index of TlsAlloc, the cell set before
 *   fl.*     - ntdll LdrpFindLoadedDllByMappingFile (GetModuleHandleW( the IMM file's full path ), user32 _InitializeImmEntryTable):
 *              NtOpenFile( "\??\<SystemRoot>\system32\IMM32.DLL", 100001H, 840H, 5, 60H ), NtCreateSection( 4, NULL, NULL, PAGE_READONLY,
 *              SEC_COMMIT, the file ), NtMapViewOfSection( it, -1, 0, 0, 0, NULL, 0, ViewShare, 0, PAGE_READONLY ), NtUnmapViewOfSection,
 *              NtClose x2 (phnt ntioapi.h / ntmmapi.h)
 * Printed: statuses, handle shapes, the written fields / their shapes; the SIDs never (constants of the system's services, as bytes). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtOpenKeyEx( PHANDLE KeyHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes, ULONG OpenOptions );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryKey( HANDLE KeyHandle, ULONG KeyInformationClass, PVOID KeyInformation, ULONG Length, PULONG ResultLength );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtSetInformationThread( HANDLE ThreadHandle, ULONG ThreadInformationClass, PVOID ThreadInformation, ULONG ThreadInformationLength );
static void case_u32k( void )
{
	TM_NTSTATUS st;
	{
		TM_USTR nm[ 2 ];
		static unsigned char buf[ 0x330 ];
		ULONG rl, n;
		size_t w, k;
		tm_ustr( &nm[ 0 ], L"WIN://SYSAPPID" );
		tm_ustr( &nm[ 1 ], L"WIN://PKG" );
		for ( n = 2; n >= 1; n-- )
		{
			memset( buf, 0xCC, sizeof( buf ) );
			rl = 0xCCCCCCCC;
			st = NtQuerySecurityAttributesToken( (HANDLE)(LONG_PTR)-6, nm, n, buf, 0x330, &rl );
			for ( w = 0, k = 0; k < sizeof( buf ); k++ ) w += buf[ k ] != 0xCC;
			printf( "tm_nt u32k.sat.eff.%lu: NtQuerySecurityAttributesToken(NtCurrentThreadEffectiveToken(), %lu name(s), Length 330H) -> status %08lXH, ReturnLength %lXH, written %zXH\n",
					(unsigned long)n, (unsigned long)n, (unsigned long)st, (unsigned long)rl, w );
		}
	}
	{
		static const unsigned char sid_owner[ 32 ] = { 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x50, 0x00, 0x00, 0x00, 0x1C, 0x8F, 0x72, 0x3C,
													   0x73, 0xE4, 0x39, 0xC5, 0xB8, 0x0B, 0x81, 0xC6, 0xD2, 0x18, 0x3A, 0x10, 0x2C, 0xD3, 0x8F, 0x05 };
		static const unsigned char sid_a[ 32 ] = { 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x50, 0x00, 0x00, 0x00, 0xF0, 0x5B, 0x58, 0x07,
												   0xC3, 0x43, 0x8C, 0x9A, 0xC7, 0x8A, 0x72, 0xDD, 0x8F, 0x8C, 0xB4, 0xDF, 0x44, 0x47, 0xE7, 0xF8 };
		static const unsigned char sid_b[ 32 ] = { 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x50, 0x00, 0x00, 0x00, 0xBF, 0x55, 0x08, 0x72,
												   0x3B, 0xE0, 0x28, 0xD0, 0x89, 0x79, 0x4B, 0xF8, 0x91, 0x89, 0x6E, 0x7C, 0x40, 0x25, 0xEC, 0xF4 };
		static const unsigned char sid_c[ 32 ] = { 0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x50, 0x00, 0x00, 0x00, 0x1B, 0x6D, 0x39, 0x16,
												   0x2D, 0x47, 0x25, 0x79, 0xA6, 0x40, 0xB1, 0xD0, 0xEE, 0xA7, 0x7C, 0x0C, 0x27, 0x4C, 0xEB, 0x61 };
		static const unsigned char sid_vm[ 16 ] = { 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		const unsigned char* ace_sid[ 5 ] = { sid_a, sid_b, sid_c, sid_vm, sid_owner };
		const unsigned char ace_len[ 5 ] = { 32, 32, 32, 16, 32 };
		static unsigned char acl[ 0x198 ], sd[ 40 ], ps[ 0x38 ];
		static const ULONG gm[ 4 ] = { 0x20001, 0x20000, 0x20000, 0x1F0001 };
		HANDLE tok = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull, dup = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		ULONG psl = 0x38, granted = 0xCCCCCCCC, ast = 0xCCCCCCCC;
		size_t off = 8, i, w;
		st = NtOpenProcessToken( (HANDLE)(LONG_PTR)-1, 0xA, &tok );
		printf( "tm_nt u32k.svc.open: NtOpenProcessToken(NtCurrentProcess(), 0AH TOKEN_DUPLICATE | TOKEN_QUERY, &h) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( tok ) );
		if ( st < 0 ) return;
		{
			/* kernelbase's DuplicateToken (the export kernel32 calls through api-ms-win-security-base; taken from this process' kernelbase:
			   linking advapi32.lib would change this program's loaded DLL set) */
			typedef BOOL( WINAPI * dup_fn )( HANDLE, SECURITY_IMPERSONATION_LEVEL, PHANDLE );
			const dup_fn df = (dup_fn)GetProcAddress( GetModuleHandleW( L"kernelbase.dll" ), "DuplicateToken" );
			printf( "tm_nt u32k.svc.dup: DuplicateToken(the token, SecurityImpersonation, &d) -> %s, LastError %lXH, handle %s\n",
					df && df( tok, SecurityImpersonation, &dup ) ? "TRUE" : "FALSE", (unsigned long)GetLastError(), tm_hshape( dup ) );
		}
		memset( acl, 0, sizeof( acl ) );
		acl[ 0 ] = 2;                       /* AclRevision */
		*(USHORT*)( acl + 2 ) = 0x198;      /* AclSize (the trace's) */
		*(USHORT*)( acl + 4 ) = 5;          /* AceCount */
		for ( i = 0; i < 5; i++ )
		{
			acl[ off ] = 0;                                       /* ACCESS_ALLOWED_ACE_TYPE */
			acl[ off + 1 ] = 0;                                   /* AceFlags */
			*(USHORT*)( acl + off + 2 ) = (USHORT)( 8 + ace_len[ i ] );
			*(ULONG*)( acl + off + 4 ) = 1;                       /* Mask */
			memcpy( acl + off + 8, ace_sid[ i ], ace_len[ i ] );
			off += 8 + ace_len[ i ];
		}
		memset( sd, 0, sizeof( sd ) );
		sd[ 0 ] = 1;                        /* Revision */
		*(USHORT*)( sd + 2 ) = 4;           /* Control: SE_DACL_PRESENT (absolute) */
		*(const void**)( sd + 8 ) = sid_owner;
		*(const void**)( sd + 16 ) = sid_owner;
		*(void**)( sd + 32 ) = acl;
		memset( ps, 0xCC, sizeof( ps ) );
		st = NtAccessCheck( sd, dup, 1, (PVOID)gm, ps, &psl, &granted, (TM_NTSTATUS*)&ast );
		for ( w = 0, i = 0; i < sizeof( ps ); i++ ) w += ps[ i ] != 0xCC;
		printf( "tm_nt u32k.svc.acc: NtAccessCheck(the service-SID SD (5 ACEs), the duplicate, 1, {20001H, 20000H, 20000H, 1F0001H}, ps, &38H, ...) -> status %08lXH, GrantedAccess %lXH, "
				"AccessStatus %08lXH, PrivilegeSet bytes written %zu (PrivilegeCount %lXH, Control %lXH), PrivilegeSetLength %lXH\n", (unsigned long)st, (unsigned long)granted,
				(unsigned long)ast, w, (unsigned long)*(ULONG*)ps, (unsigned long)*(ULONG*)( ps + 4 ), (unsigned long)psl );
		printf( "tm_nt u32k.svc.close: NtClose(the duplicate) -> %08lXH, NtClose(the token) -> %08lXH\n", (unsigned long)NtClose( dup ), (unsigned long)NtClose( tok ) );
	}
	{
		TM_USTR n;
		TM_OA oa;
		HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		ULONG tags = 0xCCCCCCCC, rl = 0xCCCCCCCC;
		tm_ustr( &n, L"\\REGISTRY\\MACHINE" );
		tm_oa( &oa, NULL, &n );
		st = NtOpenKeyEx( &h, 0x2000000, &oa, 0 );
		printf( "tm_nt u32k.hklm.open: NtOpenKeyEx(&h, 2000000H MAXIMUM_ALLOWED, \"\\REGISTRY\\MACHINE\" attributes %lXH, 0) -> status %08lXH, handle %s\n", (unsigned long)oa.Attributes,
				(unsigned long)st, tm_hshape( h ) );
		if ( st >= 0 )
		{
			st = NtQueryKey( h, 7, &tags, 4, &rl );
			printf( "tm_nt u32k.hklm.tags: NtQueryKey(it, 7 KeyHandleTagsInformation, 4) -> status %08lXH, HandleTags %lXH, ResultLength %lXH\n", (unsigned long)st, (unsigned long)tags, (unsigned long)rl );
			tags = 0xCCCCCCCC; rl = 0xCCCCCCCC;
			st = NtQueryKey( h, 7, &tags, 0, &rl );
			printf( "tm_nt u32k.hklm.tags.0: NtQueryKey(it, 7, Length 0) -> status %08lXH, HandleTags %s, ResultLength %lXH\n", (unsigned long)st, tags == 0xCCCCCCCC ? "not written" : "written", (unsigned long)rl );
			printf( "tm_nt u32k.hklm.close: NtClose -> %08lXH\n", (unsigned long)NtClose( h ) );
		}
	}
	{
		DWORD idx = TlsAlloc();
		ULONG i4 = idx;
		void* before;
		TlsSetValue( idx, (void*)(ULONG_PTR)0x1234 );
		before = TlsGetValue( idx );
		st = NtSetInformationThread( (HANDLE)(LONG_PTR)-2, 0xA, &i4, 4 );
		printf( "tm_nt u32k.tls: NtSetInformationThread(NtCurrentThread(), 0AH ThreadZeroTlsCell, &index (< 64 %s), 4) -> status %08lXH, the cell %s -> %s\n", idx < 64 ? "yes" : "no",
				(unsigned long)st, before ? "set" : "0", TlsGetValue( idx ) ? "still set" : "0" );
		st = NtSetInformationThread( (HANDLE)(LONG_PTR)-2, 0xA, &i4, 8 );
		printf( "tm_nt u32k.tls.8: the same with Length 8 -> status %08lXH\n", (unsigned long)st );
		TlsFree( idx );
	}
	{
		static wchar_t win[ 300 ], path[ 400 ];
		TM_USTR n;
		TM_OA oa;
		ULONG_PTR iosb[ 2 ] = { 0xCCCCCCCCCCCCCCCCull, 0xCCCCCCCCCCCCCCCCull };
		HANDLE f = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull, s = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
		PVOID base = NULL;
		SIZE_T vs = 0, rl2 = 0;
		MEMORY_BASIC_INFORMATION mbi;
		WIN32_FILE_ATTRIBUTE_DATA fa;
		GetWindowsDirectoryW( win, 300 );
		wcscpy( path, L"\\??\\" );
		wcscat( path, win );
		wcscat( path, L"\\system32\\IMM32.DLL" );
		tm_ustr( &n, path );
		tm_oa( &oa, NULL, &n );
		oa.Attributes = 0x840;
		st = NtOpenFile( &f, 0x100001, &oa, iosb, 5, 0x60 );
		printf( "tm_nt u32k.fl.open: NtOpenFile(&h, 100001H, \"\\??\\<SystemRoot>\\system32\\IMM32.DLL\" 840H, iosb, 5, 60H) -> status %08lXH, handle %s, IoStatus %08lXH / %llXH\n",
				(unsigned long)st, tm_hshape( f ), (unsigned long)iosb[ 0 ], (unsigned long long)iosb[ 1 ] );
		if ( st < 0 ) return;
		st = NtCreateSection( &s, 4, NULL, NULL, 2, 0x8000000, f );
		printf( "tm_nt u32k.fl.sec: NtCreateSection(&s, 4, NULL, NULL, PAGE_READONLY, SEC_COMMIT, the file) -> status %08lXH, handle %s\n", (unsigned long)st, tm_hshape( s ) );
		if ( st >= 0 )
		{
			GetFileAttributesExW( path + 4, GetFileExInfoStandard, &fa );
			st = NtMapViewOfSection( s, (HANDLE)(LONG_PTR)-1, &base, 0, 0, NULL, &vs, 1, 0, 2 );
			printf( "tm_nt u32k.fl.map: NtMapViewOfSection(it, -1, &base (0), 0, 0, NULL, &size (0), ViewShare, 0, PAGE_READONLY) -> status %08lXH", (unsigned long)st );
			if ( st >= 0 && base )
			{
				memset( &mbi, 0, sizeof( mbi ) );
				NtQueryVirtualMemory( (HANDLE)(LONG_PTR)-1, base, 0, &mbi, sizeof( mbi ), &rl2 );
				printf( ", base 64K-aligned %s, ViewSize = the file size rounded up to pages %s (%zXH), the bytes = the file's start %s; MBI AllocationProtect %lXH, State %lXH, Protect %lXH, Type %lXH, RegionSize %zXH",
						( (ULONG_PTR)base & 0xFFFF ) ? "no" : "yes", vs == ( ( (SIZE_T)fa.nFileSizeLow + 0xFFF ) & ~(SIZE_T)0xFFF ) ? "yes" : "no", vs,
						( (const unsigned char*)base )[ 0 ] == 'M' && ( (const unsigned char*)base )[ 1 ] == 'Z' ? "yes (MZ)" : "no", (unsigned long)mbi.AllocationProtect,
						(unsigned long)mbi.State, (unsigned long)mbi.Protect, (unsigned long)mbi.Type, mbi.RegionSize );
				printf( "; NtUnmapViewOfSection -> %08lXH", (unsigned long)NtUnmapViewOfSection( (HANDLE)(LONG_PTR)-1, base ) );
			}
			printf( "\n" );
			printf( "tm_nt u32k.fl.close: NtClose(the section) -> %08lXH", (unsigned long)NtClose( s ) );
		}
		else
			printf( "tm_nt u32k.fl.close:" );
		printf( ", NtClose(the file) -> %08lXH\n", (unsigned long)NtClose( f ) );
	}
}

/* ------------------------------------------------------------------------------------------------ the file system's MISSES (P45, U2125)
 * USER DECISION 2026-10-27 "virtual FS, measured misses": the emulated file system holds only the image (at its emulated path), the
 * environment's captured system files (System32) and the captured NLS / API-set files; every other path is answered with the status
 * Windows gives for that KIND of miss in that context - measured here, in this process, on OUR OWN names only: a name made of this
 * process' id that no file has ("<a missing name>"), the subfolder SystemResources (the Windows 10 .mun side folder of an image - sample
 * run 9 system call 539) and the System32 folder. Nothing is created, written or deleted; only opens / attribute queries that miss.
 * Paths (placeholders printed, never a real path):
 *   dir      "\??\<the image's directory>\<a missing name>"                 a missing file next to this image
 *   sub      "\??\<the image's directory>\SystemResources\tm_nt.exe.mun"     a missing subfolder (the .mun case)
 *   sys      "\??\<SystemRoot>\System32\<a missing name>"                    a missing file in System32
 *   syssub   "\??\<SystemRoot>\System32\<a missing name>\<a missing name>"   a missing subfolder of System32
 *   ads      "\??\<the image's directory>\<a missing name>:s"                a stream of a missing file
 *   adsimg   "\??\<the image's directory>\tm_nt.exe:<a missing name>"        a missing stream of this (existing) image
 *   dev      "\Device\<a missing name>"                                      a device name nothing created
 *   win      "\??\<SystemRoot>\<a missing name>"                              a missing file in the Windows folder
 *   winsub   "\??\<SystemRoot>\<a missing name>\<a missing name>.mun"   a missing subfolder of the Windows folder
 *   par      "\??\<the parent of the image's directory>\<a missing name>"    a missing file beside the image's folder
 *   parsub   "\??\<the parent of the image's directory>\SystemResources\tm_nt.exe.mun"   the .mun search of
 *            an image (sample run 9 / red9: SystemResources is a sibling of the image's folder, like of System32); nt_measured.py runs
 *            tm_nt from <a fresh folder>\img\tm_nt.exe, so this parent is a fresh folder of its own
 * Operations (the argument sets of the emulated traces; phnt ntioapi.h:2194-2207 NtCreateFile, :2294-2301 NtOpenFile, :3053-3057
 * NtQueryAttributesFile, :3070-3074 NtQueryFullAttributesFile; FILE_BASIC_INFORMATION 28H / FILE_NETWORK_OPEN_INFORMATION :703-712 38H):
 *   of1 NtOpenFile( 100001H, 40H, 5, 60H )   ntdll LdrpMapResourceFile (sample run 9, the .mun open)
 *   of2 NtOpenFile( 100021H, 840H, 5, 60H )  the loader's DLL open (tm_nt sp.open)
 *   of3 NtOpenFile( 100001H, 840H, 5, 60H )  LdrpFindLoadedDllByMappingFile (tm_nt u32k.fl.open)
 *   of4 NtOpenFile( 100020H, 40H, 3, 21H )   the loader's directory open (tm_nt of.curdir)
 *   cf1 NtCreateFile( 80100080H, 40H, NULL, 80H, 1, FILE_OPEN, 60H, NULL, 0 )   CreateFileW( GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING )
 *   cf2 NtCreateFile( 80100080H, 40H, NULL, 80H, 7, FILE_OPEN, 60H, NULL, 0 )   the same with every share mode
 *   qa1 / qa2 NtQueryAttributesFile( 40H / 840H ), qf1 / qf2 NtQueryFullAttributesFile( 40H / 840H )
 * Printed per call: the status, the handle (its shape, or "not written"), the IoStatusBlock (its two fields, or "not written"), the output
 * bytes written (FileInformation pre-filled with CCH). Directory listing (dq.*): this image's directory opened as FindFirstFileExW opens a
 * directory (100001H, 40H, 7, 4021H), then one query per class with a mask no file matches - a fresh handle per query (the mask is kept
 * by the handle). */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtCreateFile( PHANDLE FileHandle, ACCESS_MASK DesiredAccess, TM_OA* ObjectAttributes, PVOID IoStatusBlock, PLARGE_INTEGER AllocationSize,
														ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PVOID EaBuffer, ULONG EaLength );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryFullAttributesFile( TM_OA* ObjectAttributes, PVOID FileInformation );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryDirectoryFile( HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, PVOID IoStatusBlock, PVOID FileInformation,
																ULONG Length, ULONG FileInformationClass, BOOLEAN ReturnSingleEntry, TM_USTR* FileName, BOOLEAN RestartScan );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryDirectoryFileEx( HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, PVOID IoStatusBlock, PVOID FileInformation,
																  ULONG Length, ULONG FileInformationClass, ULONG QueryFlags, TM_USTR* FileName );

/* "IoStatus not written" or "IoStatus <status>H / <information>H" */
static const char* tm_iosb( const ULONG_PTR* iosb )
{
	static char b[ 64 ];
	if ( iosb[ 0 ] == (ULONG_PTR)0xCCCCCCCCCCCCCCCCull && iosb[ 1 ] == (ULONG_PTR)0xCCCCCCCCCCCCCCCCull ) return "IoStatus not written";
	snprintf( b, sizeof( b ), "IoStatus %08lXH / %llXH%s", (unsigned long)(ULONG)iosb[ 0 ], (unsigned long long)iosb[ 1 ], ( iosb[ 0 ] >> 32 ) == 0xCCCCCCCCull ? " (upper half not written)" : "" );
	return b;
}

/* the output bytes written into FI (N bytes pre-filled CCH): "written 0" or "written K (offsets L..HH, non-zero Z)" */
static const char* tm_fiw( const unsigned char* fi, size_t n )
{
	static char b[ 96 ];
	size_t k, w = 0, lo = n, hi = 0, nz = 0;
	for ( k = 0; k < n; k++ )
		if ( fi[ k ] != 0xCC ) { w++; if ( k < lo ) lo = k; hi = k; nz += fi[ k ] != 0; }
	if ( !w ) snprintf( b, sizeof( b ), "written 0" );
	else snprintf( b, sizeof( b ), "written %zu (offsets %zXH..%zXH, non-zero %zu)", w, lo, hi, nz );
	return b;
}

static void case_fs( void )
{
	static wchar_t app[ 600 ], par[ 600 ], win[ 300 ], miss[ 64 ], path[ 11 ][ 1000 ];
	static const char* const tag[] = { "dir", "sub", "sys", "syssub", "ads", "adsimg", "dev", "win", "winsub", "par", "parsub" };
	static const char* const shown[] = { "\\??\\<the image's directory>\\<a missing name>", "\\??\\<the image's directory>\\SystemResources\\tm_nt.exe.mun",
										 "\\??\\<SystemRoot>\\System32\\<a missing name>", "\\??\\<SystemRoot>\\System32\\<a missing name>\\<a missing name>",
										 "\\??\\<the image's directory>\\<a missing name>:s", "\\??\\<the image's directory>\\tm_nt.exe:<a missing name>",
										 "\\Device\\<a missing name>", "\\??\\<SystemRoot>\\<a missing name>",
										 "\\??\\<SystemRoot>\\<a missing name>\\<a missing name>.mun",
										 "\\??\\<the parent of the image's directory>\\<a missing name>",
										 "\\??\\<the parent of the image's directory>\\SystemResources\\tm_nt.exe.mun" };
	DWORD al = GetModuleFileNameW( NULL, app, 600 );
	UINT wl = GetWindowsDirectoryW( win, 300 );
	size_t q;
	const wchar_t* img;
	while ( al && app[ al - 1 ] != L'\\' ) al--;
	img = app + al;
	if ( !al || !wl || _wcsicmp( img, L"tm_nt.exe" ) ) { printf( "tm_nt fs: no directory / not tm_nt.exe\n" ); return; }
	app[ al - 1 ] = 0;   /* the directory without its final backslash */
	swprintf_s( miss, 64, L"tm_nt_fs_%lu_absent", (unsigned long)GetCurrentProcessId() );
	swprintf_s( path[ 0 ], 1000, L"\\??\\%s\\%s", app, miss );
	swprintf_s( path[ 1 ], 1000, L"\\??\\%s\\SystemResources\\tm_nt.exe.mun", app );
	swprintf_s( path[ 2 ], 1000, L"\\??\\%s\\System32\\%s", win, miss );
	swprintf_s( path[ 3 ], 1000, L"\\??\\%s\\System32\\%s\\%s.mun", win, miss, miss );
	swprintf_s( path[ 4 ], 1000, L"\\??\\%s\\%s:s", app, miss );
	swprintf_s( path[ 5 ], 1000, L"\\??\\%s\\tm_nt.exe:%s", app, miss );
	swprintf_s( path[ 6 ], 1000, L"\\Device\\%s", miss );
	swprintf_s( path[ 7 ], 1000, L"\\??\\%s\\%s", win, miss );
	swprintf_s( path[ 8 ], 1000, L"\\??\\%s\\%s\\%s.mun", win, miss, miss );
	wcscpy_s( par, 600, app );
	{
		wchar_t* cut = wcsrchr( par, L'\\' );
		if ( !cut || cut <= par + 2 ) { printf( "tm_nt fs: the image's directory has no parent\n" ); return; }
		*cut = 0;
	}
	swprintf_s( path[ 9 ], 1000, L"\\??\\%s\\%s", par, miss );
	swprintf_s( path[ 10 ], 1000, L"\\??\\%s\\SystemResources\\tm_nt.exe.mun", par );
	for ( q = 0; q < 11; q++ )
	{
		TM_USTR n;
		TM_OA oa;
		HANDLE h;
		ULONG_PTR iosb[ 2 ];
		unsigned char fi[ 0x40 ];
		TM_NTSTATUS st;
		int k;
		static const struct { const char* op; ULONG acc, attr, share, opt; } ofs[] = { { "of1", 0x100001, 0x40, 5, 0x60 }, { "of2", 0x100021, 0x840, 5, 0x60 },
																						 { "of3", 0x100001, 0x840, 5, 0x60 }, { "of4", 0x100020, 0x40, 3, 0x21 } };
		tm_ustr( &n, path[ q ] );
		for ( k = 0; k < 4; k++ )
		{
			tm_oa( &oa, NULL, &n );
			oa.Attributes = ofs[ k ].attr;
			h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
			iosb[ 0 ] = iosb[ 1 ] = (ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
			st = NtOpenFile( &h, ofs[ k ].acc, &oa, iosb, ofs[ k ].share, ofs[ k ].opt );
			printf( "tm_nt fs.%s.%s: NtOpenFile(&h, %lXH, \"%s\" %lXH, &iosb, %lu, %lXH) -> status %08lXH, handle %s, %s\n", tag[ q ], ofs[ k ].op, (unsigned long)ofs[ k ].acc, shown[ q ],
					(unsigned long)ofs[ k ].attr, (unsigned long)ofs[ k ].share, (unsigned long)ofs[ k ].opt, (unsigned long)st,
					h == (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull ? "not written" : tm_hshape( h ), tm_iosb( iosb ) );
			if ( st >= 0 ) NtClose( h );
		}
		for ( k = 1; k <= 2; k++ )
		{
			const ULONG share = k == 1 ? 1 : 7;
			tm_oa( &oa, NULL, &n );
			oa.Attributes = 0x40;
			h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
			iosb[ 0 ] = iosb[ 1 ] = (ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
			st = NtCreateFile( &h, 0x80100080, &oa, iosb, NULL, 0x80, share, 1, 0x60, NULL, 0 );
			printf( "tm_nt fs.%s.cf%d: NtCreateFile(&h, 80100080H, \"%s\" 40H, &iosb, NULL, 80H, %lu, FILE_OPEN 1, 60H, NULL, 0) -> status %08lXH, handle %s, %s\n", tag[ q ], k, shown[ q ],
					(unsigned long)share, (unsigned long)st, h == (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull ? "not written" : tm_hshape( h ), tm_iosb( iosb ) );
			if ( st >= 0 ) NtClose( h );
		}
		for ( k = 0; k < 4; k++ )
		{
			const ULONG attr = ( k & 1 ) ? 0x840 : 0x40;
			const int full = k >= 2;
			tm_oa( &oa, NULL, &n );
			oa.Attributes = attr;
			memset( fi, 0xCC, sizeof( fi ) );
			st = full ? NtQueryFullAttributesFile( &oa, fi ) : NtQueryAttributesFile( &oa, fi );
			printf( "tm_nt fs.%s.%s%d: %s(\"%s\" %lXH, buf) -> status %08lXH, %s\n", tag[ q ], full ? "qf" : "qa", ( k & 1 ) + 1, full ? "NtQueryFullAttributesFile" : "NtQueryAttributesFile",
					shown[ q ], (unsigned long)attr, (unsigned long)st, tm_fiw( fi, sizeof( fi ) ) );
		}
	}
	/* the directory listing: this image's directory opened as FindFirstFileExW does, one fresh handle per query, a mask nothing matches */
	{
		static const ULONG cls[] = { 1, 2, 3, 12, 37, 38 };
		static wchar_t dpath[ 700 ];
		unsigned char* fi = g_buf;
		size_t c;
		int ex;
		TM_USTR n, mask;
		swprintf_s( dpath, 700, L"\\??\\%s\\", app );
		tm_ustr( &n, dpath );
		tm_ustr( &mask, miss );
		for ( ex = 0; ex <= 1; ex++ )
			for ( c = 0; c < sizeof( cls ) / sizeof( cls[ 0 ] ); c++ )
			{
				TM_OA oa;
				HANDLE h = (HANDLE)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
				ULONG_PTR iosb[ 2 ] = { (ULONG_PTR)0xCCCCCCCCCCCCCCCCull, (ULONG_PTR)0xCCCCCCCCCCCCCCCCull };
				TM_NTSTATUS st;
				tm_oa( &oa, NULL, &n );
				oa.Attributes = 0x40;
				st = NtOpenFile( &h, 0x100001, &oa, iosb, 7, 0x4021 );
				if ( !ex && !c )
					printf( "tm_nt fs.dq.open: NtOpenFile(&h, 100001H, \"\\??\\<the image's directory>\\\" 40H, &iosb, 7, 4021H) -> status %08lXH, handle %s, %s\n", (unsigned long)st,
							tm_hshape( h ), tm_iosb( iosb ) );
				if ( st < 0 ) { printf( "tm_nt fs.dq.%s%lu: the directory did not open\n", ex ? "x" : "c", (unsigned long)cls[ c ] ); continue; }
				memset( fi, 0xCC, 0x1000 );
				iosb[ 0 ] = iosb[ 1 ] = (ULONG_PTR)0xCCCCCCCCCCCCCCCCull;
				if ( ex )
				{
					st = NtQueryDirectoryFileEx( h, NULL, NULL, NULL, iosb, fi, 0x1000, cls[ c ], 3, &mask );
					printf( "tm_nt fs.dq.x%lu: NtQueryDirectoryFileEx(the directory, NULL, NULL, NULL, &iosb, buf, 1000H, class %lu, SL_RESTART_SCAN | SL_RETURN_SINGLE_ENTRY 3, \"<a missing name>\") -> "
							"status %08lXH, %s, %s\n", (unsigned long)cls[ c ], (unsigned long)cls[ c ], (unsigned long)st, tm_iosb( iosb ), tm_fiw( fi, 0x1000 ) );
				}
				else
				{
					st = NtQueryDirectoryFile( h, NULL, NULL, NULL, iosb, fi, 0x1000, cls[ c ], TRUE, &mask, TRUE );
					printf( "tm_nt fs.dq.c%lu: NtQueryDirectoryFile(the directory, NULL, NULL, NULL, &iosb, buf, 1000H, class %lu, TRUE, \"<a missing name>\", TRUE) -> status %08lXH, %s, %s\n",
							(unsigned long)cls[ c ], (unsigned long)cls[ c ], (unsigned long)st, tm_iosb( iosb ), tm_fiw( fi, 0x1000 ) );
				}
				NtClose( h );
			}
	}
}

/* ------------------------------------------------------------------------------------------------ NtReadVirtualMemory (P45, U2134)
 * phnt ntmmapi.h:840-846 NtReadVirtualMemory( ProcessHandle, BaseAddress, Buffer, NumberOfBytesToRead, NumberOfBytesRead ) (the documented
 * ReadProcessMemory's service). Sample exploration x1 system call 573 (after the GUI conversion, a DLL's start): NtReadVirtualMemory(
 * NtCurrentProcess(), PEB + 18H (Ldr), a stack buffer, 8, &read ) right after NtQueryInformationProcess( ProcessBasicInformation ). Here the
 * same on THIS process' own memory only: the PEB's Ldr (8 bytes; with / without NumberOfBytesRead), a reserved page, a committed page
 * followed by a reserved one (4 + 4 bytes), a PAGE_NOACCESS page, a read-only destination, 0 bytes. Printed: the status, the count (pre-set
 * CCH), the destination bytes written and whether they equal the source; never an address or a value. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtReadVirtualMemory( HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, SIZE_T NumberOfBytesToRead, PSIZE_T NumberOfBytesRead );
static void tm_rvm( const char* tag, const char* what, const void* src, SIZE_T n, unsigned char* dst, int with_count )
{
	SIZE_T got = (SIZE_T)0xCCCCCCCCCCCCCCCCull;
	TM_NTSTATUS st;
	size_t k, w = 0;
	int same = 1;
	unsigned char before[ 16 ];
	if ( dst == g_buf ) memset( g_buf, 0xCC, 64 );
	memcpy( before, dst, 16 );
	st = NtReadVirtualMemory( (HANDLE)(LONG_PTR)-1, (PVOID)src, dst, n, with_count ? &got : NULL );
	for ( k = 0; k < 16; k++ ) w += dst[ k ] != before[ k ];
	if ( st >= 0 && n && n <= 16 ) same = !memcmp( dst, src, n );
	printf( "tm_nt rvm.%s: NtReadVirtualMemory(NtCurrentProcess(), %s, buf, %zXH, %s) -> status %08lXH, NumberOfBytesRead %s, destination bytes changed %zu%s\n", tag, what, n,
			with_count ? "&read" : "NULL", (unsigned long)st, !with_count ? "-" : got == (SIZE_T)0xCCCCCCCCCCCCCCCCull ? "not written" : ( got <= 0x1000 ? "written (the .count line)" : "<large>" ),
			w, ( st >= 0 && n && n <= 16 ) ? ( same ? ", equal to the source" : ", NOT equal to the source" ) : "" );
	if ( with_count && got != (SIZE_T)0xCCCCCCCCCCCCCCCCull && got <= 0x1000 ) printf( "tm_nt rvm.%s.count: %zXH\n", tag, got );
}

static void case_rvm( void )
{
	const unsigned char* peb = (const unsigned char*)__readgsqword( 0x60 );   /* TEB +60H ProcessEnvironmentBlock (the guest ntdll.pdb _TEB) */
	unsigned char* res = (unsigned char*)VirtualAlloc( NULL, 0x10000, MEM_RESERVE, PAGE_READWRITE );
	unsigned char *ro, *na;
	if ( !res ) { printf( "tm_nt rvm: no reservation\n" ); return; }
	VirtualAlloc( res, 0x1000, MEM_COMMIT, PAGE_READWRITE );   /* res: page 0 committed, page 1.. reserved */
	memset( res, 0x5A, 0x1000 );
	ro = (unsigned char*)VirtualAlloc( NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	na = (unsigned char*)VirtualAlloc( NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS );
	if ( !ro || !na ) { printf( "tm_nt rvm: no pages\n" ); return; }
	memset( ro, 0xCC, 0x1000 );
	{
		DWORD old;
		VirtualProtect( ro, 0x1000, PAGE_READONLY, &old );
	}
	tm_rvm( "ldr", "PEB + 18H (Ldr)", peb + 0x18, 8, g_buf, 1 );
	tm_rvm( "ldr.nocount", "PEB + 18H (Ldr)", peb + 0x18, 8, g_buf, 0 );
	tm_rvm( "reserved", "a reserved page", res + 0x1000, 8, g_buf, 1 );
	tm_rvm( "edge", "the last 4 bytes of a committed page + 4 of the reserved page after it", res + 0xFFC, 8, g_buf, 1 );
	tm_rvm( "noaccess", "a PAGE_NOACCESS page", na, 8, g_buf, 1 );
	tm_rvm( "rodst", "PEB + 18H into a PAGE_READONLY destination", peb + 0x18, 8, ro, 1 );
	tm_rvm( "zero", "PEB + 18H, 0 bytes", peb + 0x18, 0, g_buf, 1 );
	VirtualFree( res, 0, MEM_RELEASE );
	VirtualFree( ro, 0, MEM_RELEASE );
	VirtualFree( na, 0, MEM_RELEASE );
}

/* ------------------------------------------------------------------------------------------------ the atom table (P45, U2137)
 * phnt ntexapi.h:10140-10146 NtAddAtomEx( AtomName, Length, Atom, Flags ), :10163 NtFindAtom, :10183 NtDeleteAtom, NtQueryInformationAtom
 * (AtomBasicInformation {USHORT UsageCount, USHORT Flags, USHORT NameLength, WCHAR Name[]}). Sample exploration x3 system call 599: dxgi's
 * DllMain -> kernel32 GlobalAddAtomW -> NtAddAtomEx( a 27-character name, 36H bytes, &atom, Flags 0 ). Here with a name of OUR OWN
 * (tm_nt_atom_<the process id>, never printed) - added three times, queried, deleted three times, looked up; the atom VALUE is printed only
 * as its range (it depends on the atoms the session holds). Nothing else in the table is touched. */
__declspec( dllimport ) TM_NTSTATUS NTAPI NtAddAtomEx( PCWSTR AtomName, ULONG Length, USHORT* Atom, ULONG Flags );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtFindAtom( PCWSTR AtomName, ULONG Length, USHORT* Atom );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtDeleteAtom( USHORT Atom );
__declspec( dllimport ) TM_NTSTATUS NTAPI NtQueryInformationAtom( USHORT Atom, ULONG AtomInformationClass, PVOID AtomInformation, ULONG AtomInformationLength, PULONG ReturnLength );
static void case_atom( void )
{
	static wchar_t name[ 64 ];
	const ULONG len = (ULONG)( swprintf_s( name, 64, L"tm_nt_atom_%lu", (unsigned long)GetCurrentProcessId() ) * 2 );
	USHORT a = 0xCCCC, b = 0xCCCC, f = 0xCCCC;
	TM_NTSTATUS st;
	ULONG rl = 0xCCCCCCCC;
	int k;
	/* the process without user32 (a console process that never connected to its window station): the first add */
	st = NtAddAtomEx( name, len, &a, 0 );
	printf( "tm_nt atom.nouser32: NtAddAtomEx(\"<a name of its own>\", Flags 0) before user32 is loaded -> status %08lXH, atom %s\n", (unsigned long)st, a == 0xCCCC ? "not written" : "written" );
	if ( st >= 0 ) NtDeleteAtom( a );
	/* the sample's state: user32 loaded (its DllMain connects the process to its window station; the sample's GUI conversion came before
	 * dxgi's DllMain) - LoadLibraryW of the system's user32.dll in THIS process (our own native test PE) */
	printf( "tm_nt atom.user32: LoadLibraryW(\"user32.dll\") -> %s\n", LoadLibraryW( L"user32.dll" ) ? "loaded" : "failed" );
	a = 0xCCCC;
	st = NtAddAtomEx( name, len, &a, 0 );
	printf( "tm_nt atom.add1: NtAddAtomEx(\"<a name of its own>\", %s bytes, &atom, Flags 0) -> status %08lXH, atom %s\n", "2 x characters", (unsigned long)st,
			a == 0xCCCC ? "not written" : ( a >= 0xC000 ? "in C000H..FFFFH" : "below C000H" ) );
	st = NtAddAtomEx( name, len, &b, 0 );
	printf( "tm_nt atom.add2: the same name again -> status %08lXH, atom %s\n", (unsigned long)st, b == a ? "the same" : "another" );
	st = NtAddAtomEx( name, len, NULL, 0 );
	printf( "tm_nt atom.add3: the same name, Atom NULL -> status %08lXH\n", (unsigned long)st );
	memset( g_buf, 0xCC, 0x200 );
	st = NtQueryInformationAtom( a, 0, g_buf, 0x100, &rl );
	/* the name's length depends on the process id's digits: printed as relations only */
	printf( "tm_nt atom.query: NtQueryInformationAtom(atom, AtomBasicInformation 0, buf, 100H, &ret) -> status %08lXH, ReturnLength = 6 + NameLength + 2: %s, UsageCount %u, Flags %u, "
			"NameLength = 2 x characters: %s, the name %s, the NUL after it %s\n",
			(unsigned long)st, rl == 6ul + *(USHORT*)( g_buf + 4 ) + 2 ? "yes" : "no", *(USHORT*)g_buf, *(USHORT*)( g_buf + 2 ), *(USHORT*)( g_buf + 4 ) == len ? "yes" : "no",
			!memcmp( g_buf + 6, name, len ) ? "equal" : "different", *(USHORT*)( g_buf + 6 + len ) == 0 ? "written" : "not written" );
	st = NtFindAtom( name, len, &f );
	printf( "tm_nt atom.find: NtFindAtom(the name) -> status %08lXH, atom %s\n", (unsigned long)st, f == a ? "the same" : "another / not written" );
	for ( k = 1; k <= 3; k++ ) printf( "tm_nt atom.del%d: NtDeleteAtom(atom) -> status %08lXH\n", k, (unsigned long)NtDeleteAtom( a ) );
	f = 0xCCCC;
	st = NtFindAtom( name, len, &f );
	printf( "tm_nt atom.gone: NtFindAtom(the name) after the deletes -> status %08lXH, atom %s\n", (unsigned long)st, f == 0xCCCC ? "not written" : "written" );
}

/* ------------------------------------------------------------------------------------------------ ProcessImageInformation (P45, U2141)
 * phnt ntpsapi.h PROCESSINFOCLASS 37 (25H) ProcessImageInformation (q: SECTION_IMAGE_INFORMATION, ntmmapi.h:1274-1320, 40H bytes on x64:
 * TransferAddress +0, ZeroBits +8, MaximumStackSize +10H, CommittedStackSize +18H, SubSystemType +20H, SubSystemVersion +24H,
 * OperatingSystemVersion +28H, ImageCharacteristics +2CH, DllCharacteristics +2EH, Machine +30H, ImageContainsCode +32H, ImageFlags +33H,
 * LoaderFlags +34H, ImageFileSize +38H, CheckSum +3CH). Sample exploration x8 system call 633 (d3d11's DllMain): NtQueryInformationProcess(
 * NtCurrentProcess(), 25H, buf, 40H, NULL). Here on THIS process; every field printed as its RELATION to this image's own PE header / file
 * (an address never: TransferAddress as "= the mapped base + AddressOfEntryPoint" etc.); the image file's size from its own path. */
static void tm_pii( const char* tag, ULONG len, int with_ret )
{
	const unsigned char* base = (const unsigned char*)GetModuleHandleW( NULL );
	const unsigned char* nt = base + *(const LONG*)( base + 0x3C );
	const unsigned char* oh = nt + 0x18;
	ULONG rl = 0xCCCCCCCC;
	TM_NTSTATUS st;
	size_t k, w = 0;
	memset( g_buf, 0xCC, 0x100 );
	st = NtQueryInformationProcess( (HANDLE)(LONG_PTR)-1, 0x25, g_buf, len, with_ret ? &rl : NULL );
	for ( k = 0; k < 0x100; k++ ) if ( g_buf[ k ] != 0xCC ) w = k + 1;
	printf( "tm_nt %s: NtQueryInformationProcess(NtCurrentProcess(), 25H, buf, %lXH, %s) -> status %08lXH, ReturnLength %lXH, written %zXH\n", tag, (unsigned long)len,
			with_ret ? "&ret" : "NULL", (unsigned long)st, (unsigned long)rl, w );
	if ( st < 0 || len < 0x40 ) return;
	{
		const ULONGLONG ta = *(ULONGLONG*)( g_buf + 0 ), mx = *(ULONGLONG*)( g_buf + 0x10 ), cm = *(ULONGLONG*)( g_buf + 0x18 );
		const ULONG entry = *(const ULONG*)( oh + 0x10 );
		const ULONGLONG ib = *(const ULONGLONG*)( oh + 0x18 );
		WIN32_FILE_ATTRIBUTE_DATA fa;
		wchar_t self[ 600 ];
		ULONG fsize = 0;
		if ( GetModuleFileNameW( NULL, self, 600 ) && GetFileAttributesExW( self, GetFileExInfoStandard, &fa ) ) fsize = fa.nFileSizeLow;
		printf( "tm_nt %s.fields: TransferAddress %s, ZeroBits %lu, bytes +0CH..+0FH %02X %02X %02X %02X, MaximumStackSize %s, CommittedStackSize %s, SubSystemType %s, "
				"SubSystemVersion %s, OperatingSystemVersion %s, ImageCharacteristics %s, DllCharacteristics %s, Machine %s, ImageContainsCode %u, ImageFlags %02XH, "
				"LoaderFlags %s, ImageFileSize %s, CheckSum %s\n",
				tag, ta == (ULONGLONG)( ULONG_PTR )base + entry ? "= the mapped base + AddressOfEntryPoint" : ta == ib + entry ? "= the header ImageBase + AddressOfEntryPoint" : "<other>",
				(unsigned long)*(ULONG*)( g_buf + 8 ), g_buf[ 0xC ], g_buf[ 0xD ], g_buf[ 0xE ], g_buf[ 0xF ],
				mx == *(const ULONGLONG*)( oh + 0x48 ) ? "= SizeOfStackReserve" : "<other>", cm == *(const ULONGLONG*)( oh + 0x50 ) ? "= SizeOfStackCommit" : "<other>",
				*(ULONG*)( g_buf + 0x20 ) == *(const USHORT*)( oh + 0x44 ) ? "= Subsystem" : "<other>",
				*(ULONG*)( g_buf + 0x24 ) == ( ( (ULONG)*(const USHORT*)( oh + 0x30 ) << 16 ) | *(const USHORT*)( oh + 0x32 ) ) ? "= MajorSubsystemVersion << 16 | MinorSubsystemVersion" : "<other>",
				*(ULONG*)( g_buf + 0x28 ) == ( ( (ULONG)*(const USHORT*)( oh + 0x2A ) << 16 ) | *(const USHORT*)( oh + 0x28 ) ) ? "= MinorOperatingSystemVersion << 16 | MajorOperatingSystemVersion" :
				*(ULONG*)( g_buf + 0x28 ) == ( ( (ULONG)*(const USHORT*)( oh + 0x28 ) << 16 ) | *(const USHORT*)( oh + 0x2A ) ) ? "= MajorOperatingSystemVersion << 16 | MinorOperatingSystemVersion" : "<other>",
				*(USHORT*)( g_buf + 0x2C ) == *(const USHORT*)( nt + 0x16 ) ? "= FileHeader.Characteristics" : "<other>",
				*(USHORT*)( g_buf + 0x2E ) == *(const USHORT*)( oh + 0x46 ) ? "= DllCharacteristics" : "<other>",
				*(USHORT*)( g_buf + 0x30 ) == *(const USHORT*)( nt + 4 ) ? "= FileHeader.Machine" : "<other>", g_buf[ 0x32 ], g_buf[ 0x33 ],
				*(ULONG*)( g_buf + 0x34 ) == *(const ULONG*)( oh + 0x68 ) ? "= LoaderFlags" : "<other>",
				fsize && *(ULONG*)( g_buf + 0x38 ) == fsize ? "= the file's size" : "<other>",
				*(ULONG*)( g_buf + 0x3C ) == *(const ULONG*)( oh + 0x40 ) ? "= CheckSum" : "<other>" );
		printf( "tm_nt %s.image: this image mapped at its header ImageBase %s, mapped base below 4 GB %s, its DllCharacteristics DYNAMIC_BASE %s\n", tag,
				(ULONGLONG)(ULONG_PTR)base == ib ? "yes" : "no", (ULONG_PTR)base < 0x100000000ull ? "yes" : "no", ( *(const USHORT*)( oh + 0x46 ) & 0x40 ) ? "yes" : "no" );
	}
}

static void case_pii( void )
{
	tm_pii( "pii", 0x40, 0 );
	tm_pii( "pii.r", 0x40, 1 );
	tm_pii( "pii.0", 0, 1 );
	tm_pii( "pii.3f", 0x3F, 1 );
	tm_pii( "pii.48", 0x48, 1 );
}

static const struct tm_case g_cases[] = { { "sat", case_sat }, { "qsi", case_qsi }, { "hvp", case_hvp }, { "qip", case_qip }, { "wcp", case_wcp }, { "qsix", case_qsix }, { "iocp", case_iocp }, { "wkf", case_wkf }, { "tmr2", case_tmr2 }, { "awcp", case_awcp }, { "swf", case_swf }, { "etw", case_etw }, { "kd", case_kd }, { "of", case_of }, { "tls", case_tls }, { "ahc", case_ahc }, { "vmi", case_vmi }, { "csr", case_csr }, { "sec", case_sec }, { "csrc", case_csrc }, { "nls", case_nls }, { "con", case_con }, { "etwt", case_etwt }, { "tok", case_tok }, { "acc", case_acc }, { "bno", case_bno }, { "mut", case_mut }, { "wil", case_wil }, { "wnf", case_wnf }, { "wnfe", case_wnfe }, { "wnfs", case_wnfs }, { "t2s", case_t2s }, { "hku", case_hku }, { "alert", case_alert }, { "term", case_term }, { "time", case_time }, { "sp", case_sp }, { "mit", case_mit }, { "u32", case_u32 }, { "u32k", case_u32k }, { "fs", case_fs }, { "rvm", case_rvm }, { "atom", case_atom }, { "pii", case_pii } };

int main( int argc, char** argv )
{
	int i;
	size_t k;
	setvbuf( stdout, NULL, _IONBF, 0 );
	if ( argc == 4 && !strcmp( argv[ 1 ], "con.child" ) )   /* P41: the detached child of case con (never run by itself) */
	{
		tm_con_child( argv[ 2 ], argv[ 3 ] );
		return 0;
	}
	if ( argc == 3 && !strcmp( argv[ 1 ], "run.detached" ) ) return tm_run_detached( argv[ 2 ] );   /* P41: a test PE started detached */
	if ( argc == 3 && !strcmp( argv[ 1 ], "run.normal" ) ) return tm_run_normal( argv[ 2 ] );   /* P44: a test PE started normally */
	if ( argc < 2 )
	{
		for ( k = 0; k < sizeof( g_cases ) / sizeof( g_cases[ 0 ] ); k++ ) g_cases[ k ].fn();
		return 0;
	}
	for ( i = 1; i < argc; i++ )
	{
		for ( k = 0; k < sizeof( g_cases ) / sizeof( g_cases[ 0 ] ); k++ )
			if ( !strcmp( argv[ i ], g_cases[ k ].name ) ) break;
		if ( k == sizeof( g_cases ) / sizeof( g_cases[ 0 ] ) )
		{
			printf( "tm_nt: unknown case %s\n", argv[ i ] );
			return 2;
		}
		g_cases[ k ].fn();
	}
	return 0;
}
