/* tm_u32.c - test PE "tm_u32" (P44, ledger U2115; notes\eread\P44_user32_csr.md): the NATIVE MEASUREMENT of what an ordinary GUI program
 * gets from user32's initialization (USER DECISION 2026-10-27 STEP 3). Self-written C, never derived from the sample.
 *
 * What it is: an ordinary GUI-subsystem program that links user32.dll like any GUI program and is STARTED NORMALLY in the VMware guest
 * (decision 3.0.7; tm_nt case u32 starts it with CreateProcessW and no creation flags). Windows' own user32 code makes its connection
 * to the session's CSR server (CsrClientConnectToServer for USERSRV) and the win32k start during the loader's DLL initialization - this
 * program hand-builds NO message, never opens csrss, never calls NtAlpc* / NtConnectPort / CsrClientCallServer. After that start it
 * reads ONLY ITS OWN MEMORY: what user32 received and stored (its SHAREDINFO copy gSharedInfo, gpsi, gfServerProcess), the PEB / TEB
 * fields the start set, the regions those pointers lead to (NtQueryVirtualMemory / VirtualQuery of its own address space) and the
 * bytes of them user32's initialization reads (the offsets from the emulated trace of user32's start, P44 explorations).
 *
 * Real identifiers are never printed: addresses as region-relative offsets / relations ("<R1>+10H", "user32+92070H"), kernel
 * pointers as "<kernel va>" (shape only), handles as <handle>; machine / session / user values (system metrics, colours, the kernel
 * delta) as zero / non-zero or their shape. The emulator implements the model from these lines (Emulator/src/nt_kernel_win32k.cpp).
 *
 * The user32 offsets used here (gSharedInfo B4070H, gpsi B42A8H, gfServerProcess B44A8H, apfnDispatch 92070H) are the PUBLIC symbols
 * of the guest's user32.dll 10.0.19044.7807 (Microsoft symbol server, user32.pdb 5201D0E610EE8C31D9BCB3C4398D252A1; E:\sources\
 * DOWNLOADS.md): this program refuses another user32 build (SizeOfImage / TimeDateStamp / CheckSum checked first).
 *
 * usage: tm_u32.exe OUTFILE   (writes its lines to OUTFILE - a GUI process has no console; tm_nt case u32 prints them); exit 0 = ran,
 *        2 = usage, 3 = another user32 build */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* the guest build's user32 (public symbols, see above) */
#define U32_SIZEOFIMAGE 0x1A1000u   /* the capture's user32.dll: SizeOfImage 1A1000H, TimeDateStamp CF0B5B34H, CheckSum 1B2DDCH */
#define U32_GSHAREDINFO 0xB4070u
#define U32_GPSI 0xB42A8u
#define U32_GFSERVER 0xB44A8u
#define U32_APFNDISPATCH 0x92070u

static FILE* g_out;

static void say( const char* fmt, ... )
{
	va_list a;
	va_start( a, fmt );
	vfprintf( g_out, fmt, a );
	va_end( a );
}

/* the regions found so far (allocation bases), numbered in the order met: R1, R2, ... */
static ULONG_PTR g_reg[ 16 ];
static int g_nreg;
static ULONG_PTR g_u32;
static SIZE_T g_u32size;

static int tm_region( ULONG_PTR va )
{
	MEMORY_BASIC_INFORMATION m;
	int i;
	if ( !VirtualQuery( (LPCVOID)va, &m, sizeof( m ) ) || m.State == MEM_FREE ) return 0;
	for ( i = 0; i < g_nreg; i++ )
		if ( g_reg[ i ] == (ULONG_PTR)m.AllocationBase ) return i + 1;
	if ( g_nreg == 16 ) return 0;
	g_reg[ g_nreg++ ] = (ULONG_PTR)m.AllocationBase;
	return g_nreg;
}

/* the shape of a 64-bit value that may be a pointer: NULL, user32+X, <Rn>+X, <kernel va>, <user va unmapped>, or a small value */
static void tm_shape( char* o, size_t cap, ULONGLONG v )
{
	int r;
	if ( !v ) { snprintf( o, cap, "0" ); return; }
	if ( v >= g_u32 && v < g_u32 + g_u32size ) { snprintf( o, cap, "user32+%llXH", v - g_u32 ); return; }
	if ( v >= 0xFFFF800000000000ull ) { snprintf( o, cap, "<kernel va>" ); return; }
	if ( v < 0x10000ull ) { snprintf( o, cap, "%llXH", v ); return; }
	r = tm_region( (ULONG_PTR)v );
	if ( r ) { snprintf( o, cap, "<R%d>+%llXH", r, v - g_reg[ r - 1 ] ); return; }
	snprintf( o, cap, v < 0x800000000000ull ? "<user va, not mapped>" : "<non-canonical / large value>" );
}

static void tm_region_lines( void )
{
	int i;
	for ( i = 0; i < g_nreg; i++ )
	{
		MEMORY_BASIC_INFORMATION m;
		ULONG_PTR p = g_reg[ i ];
		SIZE_T total = 0, committed = 0;
		DWORD prot = 0, type = 0, aprot = 0;
		int first = 1;
		while ( VirtualQuery( (LPCVOID)p, &m, sizeof( m ) ) && (ULONG_PTR)m.AllocationBase == g_reg[ i ] )
		{
			if ( first ) { prot = m.Protect; type = m.Type; aprot = m.AllocationProtect; first = 0; }
			total += m.RegionSize;
			if ( m.State == MEM_COMMIT ) committed += m.RegionSize;
			p += m.RegionSize;
		}
		say( "tm_nt u32.region.R%d: AllocationBase 64K-aligned %s, AllocationProtect %lXH, first region Protect %lXH, Type %lXH, size %zXH, committed %zXH\n", i + 1,
			 ( g_reg[ i ] & 0xFFFF ) ? "no" : "yes", (unsigned long)aprot, (unsigned long)prot, (unsigned long)type, total, committed );
	}
}

/* a dump of N qwords at P as shapes, one line, keyed TAG */
static void tm_qwords( const char* tag, const ULONGLONG* p, int n, int base_off )
{
	char s[ 64 ];
	int i;
	say( "tm_nt %s:", tag );
	for ( i = 0; i < n; i++ )
	{
		tm_shape( s, sizeof( s ), p[ i ] );
		/* CLIENTINFO +60H: a block of the desktop heap allocated for the thread - VOLATILE (P44: E3430H, CE640H, DB2D0H, 138B90H in four
		   measurements): printed as its shape */
		if ( base_off + 8 * i == 0x860 && !strncmp( s, "<R", 2 ) && strstr( s, ">+" ) ) snprintf( strstr( s, ">+" ) + 1, 32, "+<a heap block>" );
		say( " +%XH %s", base_off + 8 * i, s );
	}
	say( "\n" );
}

int WINAPI wWinMain( HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show )
{
	const unsigned char* u;
	const IMAGE_NT_HEADERS64* nt;
	const unsigned char* peb = (const unsigned char*)(ULONG_PTR)__readgsqword( 0x60 );
	const unsigned char* teb = (const unsigned char*)(ULONG_PTR)__readgsqword( 0x30 );
	const ULONGLONG* si;
	wchar_t buf[ 4 ] = L"tm";
	char s[ 64 ];
	(void)hi; (void)hp; (void)show;
	if ( !cmd || !*cmd || _wfopen_s( &g_out, cmd, L"w" ) || !g_out ) return 2;
	CharUpperW( buf );   /* the import that makes this an ordinary user32 program (kept used) */
	u = (const unsigned char*)GetModuleHandleW( L"user32.dll" );
	nt = (const IMAGE_NT_HEADERS64*)( u + *(const LONG*)( u + 0x3C ) );
	g_u32 = (ULONG_PTR)u;
	g_u32size = nt->OptionalHeader.SizeOfImage;
	say( "tm_nt u32.mod: user32.dll SizeOfImage %lXH, TimeDateStamp %08lXH, CheckSum %08lXH; CharUpperW(L\"tm\") -> %s\n", (unsigned long)nt->OptionalHeader.SizeOfImage,
		 (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long)nt->OptionalHeader.CheckSum, buf[ 0 ] == 'T' && buf[ 1 ] == 'M' ? "L\"TM\"" : "other" );
	if ( nt->OptionalHeader.SizeOfImage != U32_SIZEOFIMAGE )
	{
		say( "tm_nt u32.refused: another user32 build - the offsets of this program are those of 10.0.19044.7807\n" );
		fclose( g_out );
		return 3;
	}
	si = (const ULONGLONG*)( u + U32_GSHAREDINFO );
	/* gSharedInfo = the SHAREDINFO user32 copied from its connection reply (+8 .. +240H of the 240H bytes; P44 x1 disassembly of
	   _UserClientDllInitialize: user32+189AB .. +18A1A) */
	tm_qwords( "u32.si.0", si, 5, 0 );
	say( "tm_nt u32.si.10: HeEntrySize %lXH\n", (unsigned long)*(const ULONG*)( (const unsigned char*)si + 0x10 ) );
	{
		int k;
		for ( k = 0; k < 33; k++ )
		{
			const unsigned char* w = (const unsigned char*)si + 0x28 + 0x10 * k;
			tm_shape( s, sizeof( s ), *(const ULONGLONG*)( w + 8 ) );
			say( "tm_nt u32.si.wm%d: +%XH maxMsgs %lXH, abMsgs %s\n", k, 0x28 + 0x10 * k, (unsigned long)*(const ULONG*)w, s );
		}
	}
	tm_shape( s, sizeof( s ), *(const ULONGLONG*)( u + U32_GPSI ) );
	say( "tm_nt u32.gpsi: gpsi %s, = gSharedInfo.psi %s; gfServerProcess %lXH\n", s, *(const ULONGLONG*)( u + U32_GPSI ) == si[ 0 ] ? "yes" : "no",
		 (unsigned long)*(const ULONG*)( u + U32_GFSERVER ) );
	tm_shape( s, sizeof( s ), *(const ULONGLONG*)( peb + 0x58 ) );
	say( "tm_nt u32.peb: KernelCallbackTable (58H) %s (= user32+%XH apfnDispatch %s)", s, U32_APFNDISPATCH, *(const ULONGLONG*)( peb + 0x58 ) == g_u32 + U32_APFNDISPATCH ? "yes" : "no" );
	tm_shape( s, sizeof( s ), *(const ULONGLONG*)( peb + 0xF8 ) );
	say( ", GdiSharedHandleTable (F8H) %s", s );
	tm_shape( s, sizeof( s ), *(const ULONGLONG*)( peb + 0x230 ) );
	say( ", PostProcessInitRoutine (230H) %s, GdiDCAttributeList (108H) %lXH\n", s, (unsigned long)*(const ULONG*)( peb + 0x108 ) );
	{
		/* TEB (P44 x8: what user32 / gdi32 read after the GUI conversion) */
		const ULONGLONG w32ti = *(const ULONGLONG*)( teb + 0x78 );
		const unsigned char* self = (const unsigned char*)GetModuleHandleW( NULL );
		const IMAGE_NT_HEADERS64* me = (const IMAGE_NT_HEADERS64*)( self + *(const LONG*)( self + 0x3C ) );
		const ULONG expver = ( (ULONG)me->OptionalHeader.MajorSubsystemVersion << 8 ) | me->OptionalHeader.MinorSubsystemVersion;
		const unsigned char* ci = teb + 0x800;
		say( "tm_nt u32.teb: Win32ThreadInfo (78H) non-zero %s, = GetCurrentThreadId() %s\n", w32ti ? "yes" : "no", w32ti == GetCurrentThreadId() ? "yes" : "no" );
		tm_shape( s, sizeof( s ), *(const ULONGLONG*)( ci + 0x20 ) );
		say( "tm_nt u32.ci: CI_flags (+0) %llXH, +8 %llXH, dwExpWinVer (+10H) %lXH (= the image's subsystem version %lXH %s), dwCompatFlags (+14H) %lXH, dwCompatFlags2 (+18H) %lXH, "
			 "pDeskInfo (+20H) %s, +E0H byte %02XH, +D0H = user32+B4364H %s\n", *(const ULONGLONG*)ci, *(const ULONGLONG*)( ci + 8 ), (unsigned long)*(const ULONG*)( ci + 0x10 ), (unsigned long)expver,
			 *(const ULONG*)( ci + 0x10 ) == expver ? "yes" : "no", (unsigned long)*(const ULONG*)( ci + 0x14 ), (unsigned long)*(const ULONG*)( ci + 0x18 ), s, ci[ 0xE0 ],
			 *(const ULONGLONG*)( ci + 0xD0 ) == g_u32 + 0xB4364 ? "yes" : "no" );
	}
	{
		/* the shared info user32 read: psi +0 (dwSRVIFlags, user32+17A2C) and psi +8BCH (user32+181FD) */
		const unsigned char* psi = (const unsigned char*)(ULONG_PTR)si[ 0 ];
		say( "tm_nt u32.psi: +0 %08lXH, +8BCH %02XH\n", (unsigned long)*(const ULONG*)psi, psi[ 0x8BC ] );
	}
	{
		/* the GDI shared memory (PEB GdiSharedHandleTable): what gdi32full read (P44 x8): +180094H (GdiProcessSetup), the stock-object
		   array from +1800B0H (GetStockObject: 8 bytes per object), +180140H / +180148H (ghbrDCBrush / ghbrDCPen) */
		const unsigned char* g = *(const unsigned char* const*)( peb + 0xF8 );
		int k;
		say( "tm_nt u32.gdi: +180094H %08lXH", (unsigned long)*(const ULONG*)( g + 0x180094 ) );
		for ( k = 0; k < 24; k++ )
		{
			const ULONGLONG h = *(const ULONGLONG*)( g + 0x1800B0 + 8 * k );
			if ( !h ) say( "; stock %d 0", k );
			else say( "; stock %d <gdi handle> type %02lXH stock-bit %lu upper %s", k, (unsigned long)( ( h >> 16 ) & 0x7F ), (unsigned long)( ( h >> 23 ) & 1 ), ( h >> 32 ) ? "non-zero" : "0" );
		}
		for ( k = 0; k < 2; k++ )
		{
			const ULONGLONG h = *(const ULONGLONG*)( g + 0x180140 + 8 * k );
			say( "; +%XH %s", 0x180140 + 8 * k, h ? "<gdi handle>" : "0" );
			if ( h ) say( " type %02lXH stock-bit %lu", (unsigned long)( ( h >> 16 ) & 0x7F ), (unsigned long)( ( h >> 23 ) & 1 ) );
		}
		say( "\n" );
	}
	{
		/* user32's globals after its initialization (its ClientThreadSetup callback: P44 x8): gcxGray B4314H, gcyGray B4310H, ghbrWhite
		   B4390H / ghbrBlack B4388H (= GetStockObject( WHITE_BRUSH / BLACK_BRUSH )), gfSystemInitialized B4348H, B43A4H, guiContextInfo
		   B4730H (8 bytes), gfFirstThread B3924H */
		const ULONGLONG wh = *(const ULONGLONG*)( u + 0xB4390 ), bl = *(const ULONGLONG*)( u + 0xB4388 );
		say( "tm_nt u32.g: gcxGray %lXH, gcyGray %lXH, ghbrWhite = GetStockObject(0) %s, ghbrBlack = GetStockObject(4) %s, gfSystemInitialized %lXH, B43A4H %lXH, "
			 "guiContextInfo %08lXH %08lXH, gfFirstThread %lXH\n", (unsigned long)*(const ULONG*)( u + 0xB4314 ), (unsigned long)*(const ULONG*)( u + 0xB4310 ),
			 wh && wh == (ULONGLONG)(ULONG_PTR)GetStockObject( 0 ) ? "yes" : "no", bl && bl == (ULONGLONG)(ULONG_PTR)GetStockObject( 4 ) ? "yes" : "no",
			 (unsigned long)*(const ULONG*)( u + 0xB4348 ), (unsigned long)*(const ULONG*)( u + 0xB43A4 ), (unsigned long)*(const ULONG*)( u + 0xB4730 ),
			 (unsigned long)*(const ULONG*)( u + 0xB4734 ), (unsigned long)*(const ULONG*)( u + 0xB3924 ) );
	}
	{
		/* P46 (U2159; sample run 11 exploration x63): user32 RegisterWindowMessageW -> win32u NtUserRegisterWindowMessage( &UNICODE_STRING ) for
		   the OLE name below (combase / ole32's start). Its value is an atom of the window station's table (0C000H..0FFFFH; the number depends on
		   the session - only its shape is printed), the same on the second call; GlobalFindAtomW of the name: the same atom or not */
		static const wchar_t nm[] = L"{FB8F0821-0164-101B-84ED-08002B2EC713}";
		const UINT a = RegisterWindowMessageW( nm ), b = RegisterWindowMessageW( nm );
		const ATOM f = GlobalFindAtomW( nm );
		say( "tm_nt u32.rwm: RegisterWindowMessageW(\"{FB8F0821-0164-101B-84ED-08002B2EC713}\") -> %s; again -> %s; GlobalFindAtomW of the name -> %s\n",
			 a >= 0xC000 && a <= 0xFFFF ? "an atom in C000H..FFFFH" : a ? "another value" : "0", b == a ? "the same" : "another value",
			 f == a ? "the same atom" : f ? "another atom" : "0 (not in the global atom table)" );
	}
	tm_qwords( "u32.teb.ci", (const ULONGLONG*)( teb + 0x800 ), 62, 0x800 );
	say( "tm_nt u32.peb.more: AppCompatFlagsUser (2D0H) %llXH\n", *(const ULONGLONG*)( peb + 0x2D0 ) );
	tm_region_lines();
	fclose( g_out );
	return 0;
}
