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
 * usage: tm_nt.exe [CASE ...]   (no argument = every case, in table order); exit 0 = every case ran, 2 = unknown case */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

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
		{ 14, 4, 0x180 }, { 14, 4, 0x200 }, { 0, 8, -10000000LL }, { 1, 8, -10000000LL }, { 2, 8, -600000000LL }, { 5, 8, 0x200 }, { 5, 0, 0x200 } };
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
		static const unsigned char guids[ 3 ][ 16 ] = {
			{ 0x5C, 0xAB, 0x95, 0x35, 0x2A, 0x04, 0x8E, 0x4C, 0xB9, 0x42, 0x2D, 0x05, 0x9B, 0xFE, 0xB1, 0xB1 },
			{ 0x7B, 0xC8, 0x5F, 0x30, 0x2A, 0x00, 0x26, 0x5E, 0xD2, 0x97, 0x60, 0x22, 0x30, 0x12, 0xCA, 0x9C },
			{ 0xFA, 0x2A, 0x1D, 0x90, 0xF6, 0x4F, 0xD7, 0x46, 0x8D, 0x0E, 0x53, 0x64, 0x5E, 0x1A, 0x47, 0xF5 } };
		static const USHORT logger[ 3 ] = { 1, 2, 3 };
		static const int with_cb[ 3 ] = { 0, 1, 1 };
		int g;
		for ( g = 0; g < 3; g++ )
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
			printf( "tm_nt %s.fields: RegType %lu, LoggerId %u, Reserved0 %u, ReplyCallback %s, ReplyBufferSize %lXH, Reserved1 %lXH, EnableCallbackContext %llXH, EnableFilterDesc %llXH, "
					"EnableCallback %llXH, EnableCallbackReserved %llXH, ProviderTraits %llXH, TraitsFlags %lXH, HasPayload %lXH\n",
					tag, (unsigned long)*(ULONG*)( io + 0x10 ), *(USHORT*)( io + 0x14 ), *(USHORT*)( io + 0x16 ), *(void**)( io + 0x20 ) == *(void**)( in0 + 0x20 ) ? "unchanged" : "changed",
					(unsigned long)*(ULONG*)( io + 0x28 ), (unsigned long)*(ULONG*)( io + 0x2C ), *(ULONGLONG*)( io + 0x70 ), *(ULONGLONG*)( io + 0x78 ), *(ULONGLONG*)( io + 0x80 ),
					*(ULONGLONG*)( io + 0x88 ), *(ULONGLONG*)( io + 0x90 ), (unsigned long)*(ULONG*)( io + 0x98 ), (unsigned long)*(ULONG*)( io + 0x9C ) );
			{
				size_t nz = 0;
				for ( k = 0x30; k < 0x70; k++ ) nz += io[ k ] != 0;
				printf( "tm_nt %s.reserved2: non-zero bytes in Reserved2 [+30H, +70H): %zu\n", tag, nz );
			}
			if ( st >= 0 && *(HANDLE*)( io + 0x18 ) ) printf( "tm_nt %s.close: NtClose(ReplyHandle) -> %08lXH\n", tag, (unsigned long)NtClose( *(HANDLE*)( io + 0x18 ) ) );
		}
	}
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
	static const wchar_t* const names[] = { L"KERNELBASE.dll", L"KERNEL32.DLL", L"kernel32.dll", L"ntdll.dll", L"tm_nt_no_such.dll" };
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

struct tm_case
{
	const char* name;
	void ( *fn )( void );
};
static const struct tm_case g_cases[] = { { "sat", case_sat }, { "qsi", case_qsi }, { "hvp", case_hvp }, { "qip", case_qip }, { "wcp", case_wcp }, { "qsix", case_qsix }, { "iocp", case_iocp }, { "wkf", case_wkf }, { "tmr2", case_tmr2 }, { "awcp", case_awcp }, { "swf", case_swf }, { "etw", case_etw }, { "kd", case_kd }, { "of", case_of }, { "tls", case_tls }, { "ahc", case_ahc }, { "vmi", case_vmi }, { "csr", case_csr }, { "sec", case_sec }, { "csrc", case_csrc }, { "nls", case_nls } };

int main( int argc, char** argv )
{
	int i;
	size_t k;
	setvbuf( stdout, NULL, _IONBF, 0 );
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
