// Plan step 1.5: risk tests for Unicorn on the QEMU 7.2.22 branch (a 7.2 x86 translator on a 5.0 TCG core).
//
//   R1  TCG temp exhaustion: long straight-line blocks of AVX2 / FMA / VSIB-gather forms carrying
//       immediates (the 7.2 translator's tcg_constant_* maps to 5.0's tcg_const_*, which allocates a
//       temp per call). Every block runs in Unicorn and natively on the host CPU; all YMM registers
//       must match bit-for-bit.
//   R2  uc_context save/restore carries YMM0-15, ZMM0-31 (if writable), K0-K7, MXCSR, XCR0.
//   R3  QEMU's CPUX86State.old_exception: two independent contributory faults in successive
//       uc_emu_start calls must each arrive as themselves (#DE=0 / #GP=13), not as #DF=8.
//   R4  CPUID consistency of the CPU models (leaf 7 levels, XSAVE components).
//   R5  hardware-quirk switch (FCOMI/FUCOMI C1).
//   R6  MXCSR rules of XRSTOR/XSAVE (RFBM[1]/RFBM[2]) and the reserved-bit #GP, vs hardware.
//   R7  x87 FCW/FSW as loaded by FLDCW/FLDENV/FRSTOR/FXRSTOR/XRSTOR (reserved bits, ES/B), vs hardware.
//
// Only self-generated test code runs natively here (hardware reference). Nothing from the sample.
#include <unicorn/unicorn.h>
#include <keystone/keystone.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>

static int g_failures = 0;

#define CHECK( cond, ... )                                                         \
	do {                                                                           \
		if ( !( cond ) ) { std::printf( "    FAIL: " __VA_ARGS__ ); std::printf( "\n" ); ++g_failures; } \
	} while ( 0 )

// ── assembling (self-generated code only) ─────────────────────────────────────────────────────

static std::vector<uint8_t> assemble( const std::string& text )
{
	ks_engine* ks = nullptr;
	if ( ks_open( KS_ARCH_X86, KS_MODE_64, &ks ) != KS_ERR_OK )
	{
		std::printf( "    keystone: ks_open failed\n" );
		return {};
	}
	unsigned char* enc = nullptr;
	size_t size = 0, count = 0;
	std::vector<uint8_t> out;
	if ( ks_asm( ks, text.c_str(), 0, &enc, &size, &count ) != KS_ERR_OK )
		std::printf( "    keystone: %s\n", ks_strerror( ks_errno( ks ) ) );
	else
		out.assign( enc, enc + size );
	if ( enc ) ks_free( enc );
	ks_close( ks );
	return out;
}

// ── R1: long AVX2 / FMA / VSIB blocks, Unicorn vs hardware ────────────────────────────────────
//
// State buffer (both engines): ymm0..15 at +0 (32 bytes each), gather table (128 x int32) at +512,
// host callee-saved xmm6..15 spill at +1024 (used only so the native call keeps the Win64 ABI).
// Register roles keep the arithmetic well defined:
//   ymm0-5  integer results      ymm6  gather mask (reset to all-ones before each gather)
//   ymm7    gather indices 0..7 (read only)
//   ymm8-13 float results         ymm14 = -1e6f, ymm15 = +1e6f (clamp constants, read only)

struct rng
{
	uint64_t s;
	uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return uint32_t( s ); }
	uint32_t below( uint32_t n ) { return next() % n; }
};

static std::string body_for( size_t count, uint64_t seed )
{
	rng r{ seed };
	std::string s;
	auto ir = [ & ] { return "ymm" + std::to_string( r.below( 6 ) ); };        // ymm0-5
	auto fr = [ & ] { return "ymm" + std::to_string( 8 + r.below( 6 ) ); };    // ymm8-13
	size_t emitted = 0;
	while ( emitted < count )
	{
		char line[ 160 ];
		switch ( r.below( 14 ) )
		{
			case 0:  std::snprintf( line, sizeof( line ), "vpaddd %s, %s, %s\n", ir().c_str(), ir().c_str(), ir().c_str() ); break;
			case 1:  std::snprintf( line, sizeof( line ), "vpmulld %s, %s, %s\n", ir().c_str(), ir().c_str(), ir().c_str() ); break;
			case 2:  std::snprintf( line, sizeof( line ), "vpslld %s, %s, %u\n", ir().c_str(), ir().c_str(), r.below( 32 ) ); break;
			case 3:  std::snprintf( line, sizeof( line ), "vpsrlq %s, %s, %u\n", ir().c_str(), ir().c_str(), r.below( 64 ) ); break;
			case 4:  std::snprintf( line, sizeof( line ), "vpshufd %s, %s, %u\n", ir().c_str(), ir().c_str(), r.below( 256 ) ); break;
			case 5:  std::snprintf( line, sizeof( line ), "vpermq %s, %s, %u\n", ir().c_str(), ir().c_str(), r.below( 256 ) ); break;
			case 6:  std::snprintf( line, sizeof( line ), "vpblendd %s, %s, %s, %u\n", ir().c_str(), ir().c_str(), ir().c_str(), r.below( 256 ) ); break;
			case 7:  std::snprintf( line, sizeof( line ), "vpalignr %s, %s, %s, %u\n", ir().c_str(), ir().c_str(), ir().c_str(), r.below( 32 ) ); break;
			case 8:  std::snprintf( line, sizeof( line ), "vpcmpeqd ymm6, ymm6, ymm6\nvpgatherdd %s, dword ptr [rax + ymm7*4], ymm6\n", ir().c_str() ); ++emitted; break;
			case 9:  std::snprintf( line, sizeof( line ), "vfmadd231ps %s, %s, %s\n", fr().c_str(), fr().c_str(), fr().c_str() ); break;
			case 10: std::snprintf( line, sizeof( line ), "vmulps %s, %s, %s\n", fr().c_str(), fr().c_str(), fr().c_str() ); break;
			case 11: std::snprintf( line, sizeof( line ), "vroundps %s, %s, %u\n", fr().c_str(), fr().c_str(), r.below( 4 ) ); break;
			case 12: std::snprintf( line, sizeof( line ), "vshufps %s, %s, %s, %u\n", fr().c_str(), fr().c_str(), fr().c_str(), r.below( 256 ) ); break;
			default:
			{
				// keep the float pool finite: clamp a float register into [-1e6, 1e6]
				std::string f = fr();
				std::snprintf( line, sizeof( line ), "vminps %s, %s, ymm15\nvmaxps %s, %s, ymm14\n", f.c_str(), f.c_str(), f.c_str(), f.c_str() );
				++emitted;
				break;
			}
		}
		s += line;
		++emitted;
	}
	return s;
}

static std::string wrap( const std::string& body )
{
	std::string s;
	for ( int i = 6; i < 16; ++i ) s += "movdqu xmmword ptr [rcx + " + std::to_string( 1024 + ( i - 6 ) * 16 ) + "], xmm" + std::to_string( i ) + "\n";
	s += "lea rax, [rcx + 512]\n";
	for ( int i = 0; i < 16; ++i ) s += "vmovdqu ymm" + std::to_string( i ) + ", ymmword ptr [rcx + " + std::to_string( i * 32 ) + "]\n";
	s += body;
	for ( int i = 0; i < 16; ++i ) s += "vmovdqu ymmword ptr [rcx + " + std::to_string( i * 32 ) + "], ymm" + std::to_string( i ) + "\n";
	s += "vzeroupper\n";
	for ( int i = 6; i < 16; ++i ) s += "movdqu xmm" + std::to_string( i ) + ", xmmword ptr [rcx + " + std::to_string( 1024 + ( i - 6 ) * 16 ) + "]\n";
	s += "ret\n";
	return s;
}

static void init_state( uint8_t* st, uint64_t seed )
{
	rng r{ seed ^ 0x9E3779B97F4A7C15ull };
	std::memset( st, 0, 2048 );
	for ( int reg = 0; reg < 6; ++reg )
		for ( int i = 0; i < 8; ++i ) ( ( uint32_t* ) ( st + reg * 32 ) )[ i ] = r.next();
	for ( int i = 0; i < 8; ++i ) ( ( uint32_t* ) ( st + 7 * 32 ) )[ i ] = uint32_t( i * 13 % 128 );     // gather indices
	for ( int reg = 8; reg < 14; ++reg )
		for ( int i = 0; i < 8; ++i ) ( ( float* ) ( st + reg * 32 ) )[ i ] = 0.5f + float( r.below( 1000 ) ) / 667.0f;
	for ( int i = 0; i < 8; ++i ) ( ( float* ) ( st + 14 * 32 ) )[ i ] = -1e6f, ( ( float* ) ( st + 15 * 32 ) )[ i ] = 1e6f;
	for ( int i = 0; i < 128; ++i ) ( ( uint32_t* ) ( st + 512 ) )[ i ] = r.next();
}

static bool run_native( const std::vector<uint8_t>& code, uint8_t* st )
{
	void* mem = VirtualAlloc( nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
	if ( !mem ) return false;
	std::memcpy( mem, code.data(), code.size() );
	FlushInstructionCache( GetCurrentProcess(), mem, code.size() );
	( ( void( * )( uint8_t* ) ) mem )( st );
	VirtualFree( mem, 0, MEM_RELEASE );
	return true;
}

static uc_err run_unicorn( const std::vector<uint8_t>& code, uint8_t* st, double& ms )
{
	const uint64_t CODE = 0x100000, STATE = 0x400000, STACK = 0x500000;
	uc_engine* uc = nullptr;
	uc_err e = uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	if ( e ) return e;
	size_t code_map = ( code.size() + 0xFFF ) & ~size_t( 0xFFF );
	uc_mem_map( uc, CODE, code_map, UC_PROT_ALL );
	uc_mem_map( uc, STATE, 0x1000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_map( uc, STACK, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, CODE, code.data(), code.size() );
	uc_mem_write( uc, STATE, st, 2048 );
	uint64_t rcx = STATE, rsp = STACK + 0x8000;
	uc_reg_write( uc, UC_X86_REG_RCX, &rcx );
	uc_reg_write( uc, UC_X86_REG_RSP, &rsp );
	auto t0 = std::chrono::steady_clock::now();
	e = uc_emu_start( uc, CODE, CODE + code.size() - 1, 0, 0 );     // stop before the final `ret`
	ms = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - t0 ).count();
	if ( !e ) uc_mem_read( uc, STATE, st, 2048 );
	uc_close( uc );
	return e;
}

static void test_r1()
{
	std::printf( "R1  long AVX2/FMA/VSIB blocks with immediates: Unicorn vs hardware\n" );
	const size_t sizes[] = { 64, 256, 511, 512, 1024, 4096 };
	for ( size_t n : sizes )
	{
		for ( uint64_t seed = 1; seed <= 3; ++seed )
		{
			std::string text = wrap( body_for( n, seed * 7919 + n ) );
			std::vector<uint8_t> code = assemble( text );
			CHECK( !code.empty(), "assembly failed for n=%zu seed=%llu", n, ( unsigned long long ) seed );
			if ( code.empty() ) continue;
			alignas( 64 ) uint8_t hw[ 2048 ], uc[ 2048 ];
			init_state( hw, seed );
			std::memcpy( uc, hw, sizeof( hw ) );
			double ms = 0;
			uc_err e = run_unicorn( code, uc, ms );
			bool native_ok = run_native( code, hw );
			int first_diff = -1;
			for ( int reg = 0; reg < 16 && first_diff < 0; ++reg )
				if ( std::memcmp( hw + reg * 32, uc + reg * 32, 32 ) ) first_diff = reg;
			std::printf( "    n=%-5zu seed=%llu bytes=%-6zu uc_emu_start=%-2d (%s) %.1f ms  native=%s  ymm %s",
						 n, ( unsigned long long ) seed, code.size(), int( e ), uc_strerror( e ), ms, native_ok ? "ok" : "FAILED",
						 first_diff < 0 ? "all 16 match\n" : "MISMATCH" );
			if ( first_diff >= 0 )
			{
				std::printf( " (first at ymm%d)\n      hw: ", first_diff );
				for ( int i = 31; i >= 0; --i ) std::printf( "%02x", hw[ first_diff * 32 + i ] );
				std::printf( "\n      uc: " );
				for ( int i = 31; i >= 0; --i ) std::printf( "%02x", uc[ first_diff * 32 + i ] );
				std::printf( "\n" );
			}
			CHECK( e == UC_ERR_OK, "uc_emu_start failed (n=%zu): %s", n, uc_strerror( e ) );
			CHECK( native_ok, "native reference run failed" );
			CHECK( first_diff < 0, "ymm%d differs from hardware (n=%zu seed=%llu)", first_diff, n, ( unsigned long long ) seed );
		}
	}
}

// ── R2: uc_context round trip ─────────────────────────────────────────────────────────────────

static void fill( uint8_t* p, size_t n, uint8_t base ) { for ( size_t i = 0; i < n; ++i ) p[ i ] = uint8_t( base + i * 7 ); }

static void test_r2()
{
	std::printf( "R2  uc_context save/restore: YMM0-15, ZMM0-31, K0-K7, MXCSR, XCR0\n" );
	uc_engine* uc = nullptr;
	if ( uc_open( UC_ARCH_X86, UC_MODE_64, &uc ) ) { CHECK( false, "uc_open" ); return; }

	struct item { const char* name; int id; size_t size; bool required; };
	std::vector<item> items;
	static char names[ 80 ][ 8 ];
	int k = 0;
	for ( int i = 0; i < 16; ++i, ++k ) { std::snprintf( names[ k ], 8, "ymm%d", i ); items.push_back( { names[ k ], UC_X86_REG_YMM0 + i, 32, true } ); }
	for ( int i = 0; i < 32; ++i, ++k ) { std::snprintf( names[ k ], 8, "zmm%d", i ); items.push_back( { names[ k ], UC_X86_REG_ZMM0 + i, 64, false } ); }
	for ( int i = 0; i < 8; ++i, ++k ) { std::snprintf( names[ k ], 8, "k%d", i ); items.push_back( { names[ k ], UC_X86_REG_K0 + i, 8, true } ); }
	items.push_back( { "mxcsr", UC_X86_REG_MXCSR, 4, true } );
	items.push_back( { "xcr0", UC_X86_REG_XCR0, 8, true } );

	auto value_a = [ & ]( const item& it, uint8_t* v ) {
		fill( v, it.size, uint8_t( 0x11 + it.id ) );
		if ( it.id == UC_X86_REG_MXCSR ) { uint32_t m = 0xFF80; std::memcpy( v, &m, 4 ); }         // RC=11, FTZ, all masks
		if ( it.id == UC_X86_REG_XCR0 ) { uint64_t x = 0x7; std::memcpy( v, &x, 8 ); }              // x87|SSE|AVX
	};
	auto value_b = [ & ]( const item& it, uint8_t* v ) {
		fill( v, it.size, uint8_t( 0xA5 ^ it.id ) );
		if ( it.id == UC_X86_REG_MXCSR ) { uint32_t m = 0x1F80; std::memcpy( v, &m, 4 ); }
		if ( it.id == UC_X86_REG_XCR0 ) { uint64_t x = 0x3; std::memcpy( v, &x, 8 ); }              // x87|SSE
	};

	// ZMM is written last: ZMM i overlaps YMM i, so write ZMM first, then YMM on top.
	std::vector<bool> writable( items.size(), true );
	uint8_t v[ 64 ];
	for ( int pass = 0; pass < 2; ++pass )
		for ( size_t i = 0; i < items.size(); ++i )
		{
			bool is_zmm = items[ i ].id >= UC_X86_REG_ZMM0 && items[ i ].id < UC_X86_REG_ZMM0 + 32;
			if ( ( pass == 0 ) != is_zmm ) continue;
			value_a( items[ i ], v );
			uc_err e = uc_reg_write( uc, items[ i ].id, v );
			if ( e ) { writable[ i ] = false; std::printf( "    write %-6s -> %s%s\n", items[ i ].name, uc_strerror( e ), items[ i ].required ? "" : " (optional)" ); }
			CHECK( !e || !items[ i ].required, "uc_reg_write(%s) failed: %s", items[ i ].name, uc_strerror( e ) );
		}
	// expected values after the writes (YMM i overlays the low half of ZMM i)
	std::vector<std::vector<uint8_t>> expect( items.size() );
	for ( size_t i = 0; i < items.size(); ++i )
	{
		expect[ i ].resize( items[ i ].size );
		uc_reg_read( uc, items[ i ].id, expect[ i ].data() );
	}

	uc_context* ctx = nullptr;
	CHECK( uc_context_alloc( uc, &ctx ) == UC_ERR_OK, "uc_context_alloc" );
	CHECK( uc_context_save( uc, ctx ) == UC_ERR_OK, "uc_context_save" );
	for ( size_t i = 0; i < items.size(); ++i ) if ( writable[ i ] ) { value_b( items[ i ], v ); uc_reg_write( uc, items[ i ].id, v ); }
	CHECK( uc_context_restore( uc, ctx ) == UC_ERR_OK, "uc_context_restore" );

	int ok = 0, bad = 0;
	for ( size_t i = 0; i < items.size(); ++i )
	{
		if ( !writable[ i ] ) continue;
		uint8_t got[ 64 ] = {};
		uc_reg_read( uc, items[ i ].id, got );
		value_a( items[ i ], v );
		// ZMM0-15 share their low 256 bits with YMM0-15, which were written after them.
		int zi = items[ i ].id - UC_X86_REG_ZMM0;
		if ( zi >= 0 && zi < 16 )
		{
			item y{ "", UC_X86_REG_YMM0 + zi, 32, true };
			value_a( y, v );
		}
		bool match = !std::memcmp( got, expect[ i ].data(), items[ i ].size );
		bool wrote_back = !std::memcmp( expect[ i ].data(), v, items[ i ].size );
		if ( match ) ++ok; else ++bad;
		if ( !match || !wrote_back )
			std::printf( "    %-6s %s%s\n", items[ i ].name, match ? "restored" : "NOT RESTORED",
						 wrote_back ? "" : " (note: value read back after write differs from value written)" );
		CHECK( match, "%s not restored by uc_context_restore", items[ i ].name );
	}
	std::printf( "    %d registers restored correctly, %d wrong\n", ok, bad );
	uc_context_free( ctx );
	uc_close( uc );
}

// ── R3: old_exception / spurious #DF ──────────────────────────────────────────────────────────

struct intr_log { std::vector<uint32_t> seen; };

static void on_intr( uc_engine* uc, uint32_t intno, void* user )
{
	( ( intr_log* ) user )->seen.push_back( intno );
	uc_emu_stop( uc );
}

static void test_r3()
{
	std::printf( "R3  successive contributory faults across uc_emu_start calls (#DE=0, #GP=13, #DF=8)\n" );
	// 0x1000: div ecx (ecx=0) -> #DE        0x1010: mov ds, ax (ax=0x7ff8: selector beyond GDT) -> #GP
	std::vector<uint8_t> de = assemble( "div ecx" );
	std::vector<uint8_t> gp = assemble( "mov ds, ax" );
	CHECK( !de.empty() && !gp.empty(), "assembly" );
	const char* seqs[] = { "DE,DE", "GP,GP", "DE,GP", "GP,DE" };
	for ( const char* seq : seqs )
	{
		uc_engine* uc = nullptr;
		uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
		uc_mem_map( uc, 0x1000, 0x1000, UC_PROT_ALL );
		uc_mem_map( uc, 0x100000, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
		uc_mem_write( uc, 0x1000, de.data(), de.size() );
		uc_mem_write( uc, 0x1010, gp.data(), gp.size() );
		uint64_t zero = 0, ax = 0x7ff8, rsp = 0x108000;
		uc_reg_write( uc, UC_X86_REG_RCX, &zero );
		uc_reg_write( uc, UC_X86_REG_RAX, &ax );
		uc_reg_write( uc, UC_X86_REG_RSP, &rsp );
		intr_log log;
		uc_hook h;
		uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
		std::vector<uint32_t> expect;
		for ( const char* p = seq; *p; p += 3 )
		{
			bool is_de = p[ 0 ] == 'D';
			uint64_t start = is_de ? 0x1000 : 0x1010;
			uc_reg_write( uc, UC_X86_REG_RCX, &zero );
			uc_reg_write( uc, UC_X86_REG_RAX, &ax );
			uc_emu_start( uc, start, start + 0x8, 0, 1 );
			expect.push_back( is_de ? 0 : 13 );
			if ( !p[ 2 ] ) break;
		}
		std::string got;
		for ( uint32_t v : log.seen ) got += ( got.empty() ? "" : "," ) + std::to_string( v );
		bool ok = log.seen == expect;
		std::printf( "    %-6s expected %s  got %-8s %s\n", seq, expect.size() == 2 ? ( std::to_string( expect[ 0 ] ) + "," + std::to_string( expect[ 1 ] ) ).c_str() : "?",
					 got.c_str(), ok ? "ok" : "WRONG (stale old_exception -> keep the calibration workaround)" );
		CHECK( ok, "sequence %s delivered %s", seq, got.c_str() );
		uc_close( uc );
	}
}

// ── R4: CPUID consistency of the CPU models (plan 1.8 re-audit) ──────────────────────────────

struct cpuid_out { uint32_t eax, ebx, ecx, edx; uc_err err; int fault; };

static cpuid_out run_cpuid( int model, uint32_t leaf, uint32_t sub )
{
	cpuid_out o{ 0, 0, 0, 0, UC_ERR_OK, -1 };
	std::vector<uint8_t> code = assemble( "cpuid" );
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, model );
	uc_mem_map( uc, 0x1000, 0x1000, UC_PROT_ALL );
	uc_mem_write( uc, 0x1000, code.data(), code.size() );
	uint64_t a = leaf, c = sub;
	uc_reg_write( uc, UC_X86_REG_RAX, &a );
	uc_reg_write( uc, UC_X86_REG_RCX, &c );
	o.err = uc_emu_start( uc, 0x1000, 0x1000 + code.size(), 0, 1 );
	uint64_t r[ 4 ] = {};
	uc_reg_read( uc, UC_X86_REG_RAX, &r[ 0 ] ); uc_reg_read( uc, UC_X86_REG_RBX, &r[ 1 ] );
	uc_reg_read( uc, UC_X86_REG_RCX, &r[ 2 ] ); uc_reg_read( uc, UC_X86_REG_RDX, &r[ 3 ] );
	o.eax = uint32_t( r[ 0 ] ); o.ebx = uint32_t( r[ 1 ] ); o.ecx = uint32_t( r[ 2 ] ); o.edx = uint32_t( r[ 3 ] );
	uc_close( uc );
	return o;
}

static int run_xsetbv( int model, uint64_t xcr0 )
{
	std::vector<uint8_t> code = assemble( "xsetbv" );
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, model );
	uc_mem_map( uc, 0x1000, 0x1000, UC_PROT_ALL );
	uc_mem_write( uc, 0x1000, code.data(), code.size() );
	uint64_t a = xcr0 & 0xFFFFFFFF, d = xcr0 >> 32, c = 0;
	uc_reg_write( uc, UC_X86_REG_RAX, &a );
	uc_reg_write( uc, UC_X86_REG_RDX, &d );
	uc_reg_write( uc, UC_X86_REG_RCX, &c );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_emu_start( uc, 0x1000, 0x1000 + code.size(), 0, 1 );
	uc_close( uc );
	return log.seen.empty() ? -1 : int( log.seen[ 0 ] );
}

static void test_r4()
{
	std::printf( "R4  CPUID consistency: UC_CPU_X86_MAX leaf 7 levels and XSAVE components (leaf 0xD, XSETBV)\n" );
	cpuid_out l7 = run_cpuid( UC_CPU_X86_MAX, 7, 0 ), l71 = run_cpuid( UC_CPU_X86_MAX, 7, 1 );
	std::printf( "    MAX  leaf 7.0: eax(max sub-leaf)=%u ebx=%08X ecx=%08X   leaf 7.1 eax=%08X\n", l7.eax, l7.ebx, l7.ecx, l71.eax );
	CHECK( l7.eax >= 1, "CPUID.7.0:EAX (max sub-leaf) is %u - leaf 7.1 unreachable", l7.eax );
	CHECK( ( l71.eax >> 7 ) & 1, "CPUID.7.1:EAX lacks CMPCCXADD" );
	for ( int model : { int( UC_CPU_X86_MAX ), int( UC_CPU_X86_ICELAKE_SERVER ) } )
	{
		cpuid_out d0 = run_cpuid( model, 0xD, 0 );
		const char* nm = model == UC_CPU_X86_MAX ? "MAX" : "Icelake-Server";
		std::printf( "    %-14s leaf 0xD.0: xcr0 components eax=%08X edx=%08X, max size ecx=%u\n", nm, d0.eax, d0.edx, d0.ecx );
		CHECK( ( d0.eax & 0x7 ) == 0x7, "%s: x87/SSE/AVX components missing (%08X)", nm, d0.eax );
		CHECK( ( d0.eax & 0xE0 ) == 0, "%s: advertises AVX-512 XSAVE components TCG cannot save (%08X)", nm, d0.eax );
		int ok7 = run_xsetbv( model, 0x7 ), bad = run_xsetbv( model, 0xE7 );
		std::printf( "    %-14s xsetbv 0x7 -> %s, xsetbv 0xE7 -> %s\n", nm, ok7 < 0 ? "ok" : ( "vector " + std::to_string( ok7 ) ).c_str(),
					 bad < 0 ? "accepted" : ( "vector " + std::to_string( bad ) ).c_str() );
		CHECK( ok7 < 0, "%s: xsetbv XCR0=7 faulted (vector %d)", nm, ok7 );
		CHECK( bad == 13, "%s: xsetbv with AVX-512 components did not #GP (%d)", nm, bad );
	}
}

// ── R5: hardware-quirk switch (plan 1.9.1) ──────────────────────────────────────────────────

static uint16_t fcomi_c1_run( bool quirk, bool fucomi, uint32_t* readback )
{
	// ST1 = 2.0, ST0 = -1.0; fxam sets C1 = sign(ST0) = 1; then fcomi/fucomi st(0), st(1)
	std::vector<uint8_t> code = assemble( std::string( "fld qword ptr [rax]\nfld qword ptr [rax + 8]\nfxam\n" ) +
										  ( fucomi ? "fucomi st(0), st(1)\n" : "fcomi st(0), st(1)\n" ) );
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	if ( quirk ) uc_ctl_set_x86_hw_quirks( uc, UC_X86_QUIRK_FCOMI_KEEPS_C1 );
	if ( readback ) uc_ctl_get_x86_hw_quirks( uc, readback );
	uc_mem_map( uc, 0x1000, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, 0x2000, 0x1000, UC_PROT_READ | UC_PROT_WRITE );
	double vals[ 2 ] = { 2.0, -1.0 };
	uc_mem_write( uc, 0x2000, vals, sizeof( vals ) );
	uc_mem_write( uc, 0x1000, code.data(), code.size() );
	uint64_t rax = 0x2000;
	uc_reg_write( uc, UC_X86_REG_RAX, &rax );
	uc_emu_start( uc, 0x1000, 0x1000 + code.size(), 0, 0 );
	uint64_t fsw = 0;
	uc_reg_read( uc, UC_X86_REG_FPSW, &fsw );
	uc_close( uc );
	return uint16_t( fsw );
}

static void test_r5()
{
	std::printf( "R5  hardware-quirk switch: FCOMI/FUCOMI C1 (SDM: cleared; i5-13600K: unchanged)\n" );
	for ( bool fu : { false, true } )
	{
		uint32_t rb = 0xFFFF;
		uint16_t sdm = fcomi_c1_run( false, fu, &rb ), hw = fcomi_c1_run( true, fu, nullptr );
		std::printf( "    %-7s default (manual): fsw=%04X C1=%d (quirks=%u)   UC_X86_QUIRK_FCOMI_KEEPS_C1: fsw=%04X C1=%d\n",
					 fu ? "fucomi" : "fcomi", sdm, ( sdm >> 9 ) & 1, rb, hw, ( hw >> 9 ) & 1 );
		CHECK( rb == 0, "default quirks %u, expected 0", rb );
		CHECK( ( ( sdm >> 9 ) & 1 ) == 0, "%s: default must clear C1 (SDM)", fu ? "fucomi" : "fcomi" );
		CHECK( ( ( hw >> 9 ) & 1 ) == 1, "%s: quirk must keep C1 (hardware)", fu ? "fucomi" : "fcomi" );
	}
	uint32_t rb = 0;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_x86_hw_quirks( uc, UC_X86_QUIRK_FCOMI_KEEPS_C1 );
	uc_ctl_get_x86_hw_quirks( uc, &rb );
	uc_close( uc );
	CHECK( rb == UC_X86_QUIRK_FCOMI_KEEPS_C1, "uc_ctl round trip %u", rb );
}

// ── R6: MXCSR load/store rules of XRSTOR / XSAVE / LDMXCSR (plan 1.10.2) ───────────────────────
//
// SDM Vol1 13.7 / 13.8.1: the standard forms of XSAVE and XRSTOR handle MXCSR whenever RFBM[1] or
// RFBM[2] is set; Vol2 LDMXCSR/FXRSTOR/XRSTOR: writing a reserved MXCSR bit (outside MXCSR_MASK)
// is #GP(0) and nothing is loaded. Each case runs the same self-generated thunk on the host and in
// Unicorn. Thunk ABI: rcx = 64-byte aligned XSAVE area, rdx = RFBM, r8d = MXCSR value, r9 = out.
// The XRSTOR thunk first XSAVEs the live state so restoring it changes nothing but MXCSR.

struct r6_result { int fault; uint32_t out[ 4 ]; };

static const char* r6_xrstor = "stmxcsr dword ptr [r9]\n"
							   "mov r10, rdx\nmov eax, 7\nxor edx, edx\nxsave [rcx]\n"
							   "mov dword ptr [rcx + 24], r8d\n"
							   "mov eax, r10d\nxor edx, edx\nxrstor [rcx]\n"
							   "stmxcsr dword ptr [r9 + 4]\nldmxcsr dword ptr [r9]\nret\n";
static const char* r6_xsave = "mov dword ptr [rcx + 24], 0xDEADBEEF\nmov dword ptr [rcx + 28], 0xDEADBEEF\n"
							  "mov eax, edx\nxor edx, edx\nxsave [rcx]\n"
							  "mov eax, dword ptr [rcx + 24]\nmov dword ptr [r9 + 8], eax\n"
							  "mov eax, dword ptr [rcx + 28]\nmov dword ptr [r9 + 12], eax\nret\n";
static const char* r6_ldmxcsr = "stmxcsr dword ptr [r9]\nmov dword ptr [r9 + 8], r8d\nldmxcsr dword ptr [r9 + 8]\n"
								"stmxcsr dword ptr [r9 + 4]\nldmxcsr dword ptr [r9]\nret\n";

static r6_result r6_native( const std::vector<uint8_t>& code, uint64_t rfbm, uint32_t mxcsr )
{
	r6_result r{ -1, { 0, 0, 0, 0 } };
	alignas( 64 ) static uint8_t area[ 4096 ];
	std::memset( area, 0, sizeof( area ) );
	void* mem = VirtualAlloc( nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
	if ( !mem ) return r;
	std::memcpy( mem, code.data(), code.size() );
	FlushInstructionCache( GetCurrentProcess(), mem, code.size() );
	using fn_t = void( * )( void*, uint64_t, uint64_t, uint32_t* );
	__try { ( ( fn_t ) mem )( area, rfbm, mxcsr, r.out ); }
	__except ( GetExceptionCode() == EXCEPTION_PRIV_INSTRUCTION || GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
				   ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH ) { r.fault = 13; }
	VirtualFree( mem, 0, MEM_RELEASE );
	return r;
}

static r6_result r6_unicorn( const std::vector<uint8_t>& code, uint64_t rfbm, uint32_t mxcsr, uint32_t host_mxcsr )
{
	r6_result r{ -1, { 0, 0, 0, 0 } };
	const uint64_t R6_CODE = 0x1000, R6_AREA = 0x10000, R6_OUT = 0x20000, R6_STACK = 0x30000;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
	uc_mem_map( uc, R6_CODE, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, R6_AREA, 0x2000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_map( uc, R6_OUT, 0x1000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_map( uc, R6_STACK, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, R6_CODE, code.data(), code.size() );
	uint64_t rcx = R6_AREA, rdx = rfbm, r8 = mxcsr, r9 = R6_OUT, rsp = R6_STACK + 0x8000, mx = host_mxcsr, xcr0 = 0x7;     // host runs with x87|SSE|AVX enabled
	uc_reg_write( uc, UC_X86_REG_RCX, &rcx ); uc_reg_write( uc, UC_X86_REG_RDX, &rdx );
	uc_reg_write( uc, UC_X86_REG_R8, &r8 );   uc_reg_write( uc, UC_X86_REG_R9, &r9 );
	uc_reg_write( uc, UC_X86_REG_RSP, &rsp ); uc_reg_write( uc, UC_X86_REG_MXCSR, &mx );
	uc_reg_write( uc, UC_X86_REG_XCR0, &xcr0 );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_err e = uc_emu_start( uc, R6_CODE, R6_CODE + code.size() - 1, 0, 0 );     // stop before the final `ret`
	if ( e ) std::printf( "    uc_emu_start: %s\n", uc_strerror( e ) );
	if ( !log.seen.empty() ) r.fault = int( log.seen[ 0 ] );
	else uc_mem_read( uc, R6_OUT, r.out, sizeof( r.out ) );
	uc_close( uc );
	return r;
}

static void test_r6()
{
	std::printf( "R6  MXCSR rules: XRSTOR/XSAVE with RFBM[1]/RFBM[2], reserved-bit #GP (SDM Vol1 13.7/13.8.1)\n" );
	std::vector<uint8_t> xr = assemble( r6_xrstor ), xs = assemble( r6_xsave ), ld = assemble( r6_ldmxcsr );
	CHECK( !xr.empty() && !xs.empty() && !ld.empty(), "assembly" );
	if ( xr.empty() || xs.empty() || ld.empty() ) return;
	uint32_t host_mx = 0;
	{
		r6_result probe = r6_native( ld, 0, 0x1F80 );      // also proves the thunk runs natively
		host_mx = probe.out[ 0 ];
	}
	struct c { const char* what; const std::vector<uint8_t>* code; uint64_t rfbm; uint32_t mx; };
	const c cases[] = {
		{ "xrstor rfbm=2 mxcsr=5F80 ", &xr, 2, 0x5F80 },  { "xrstor rfbm=4 mxcsr=5F80 ", &xr, 4, 0x5F80 },
		{ "xrstor rfbm=1 mxcsr=5F80 ", &xr, 1, 0x5F80 },  { "xrstor rfbm=2 mxcsr=11F80", &xr, 2, 0x11F80 },
		{ "xrstor rfbm=4 mxcsr=11F80", &xr, 4, 0x11F80 }, { "xrstor rfbm=1 mxcsr=11F80", &xr, 1, 0x11F80 },
		{ "xsave  rfbm=4            ", &xs, 4, 0 },       { "xsave  rfbm=2            ", &xs, 2, 0 },
		{ "xsave  rfbm=1            ", &xs, 1, 0 },       { "ldmxcsr 1FC0 (DAZ)       ", &ld, 0, 0x1FC0 },
		{ "ldmxcsr 11F80            ", &ld, 0, 0x11F80 }, { "ldmxcsr 80001F80         ", &ld, 0, 0x80001F80u },
	};
	for ( const c& k : cases )
	{
		r6_result hw = r6_native( *k.code, k.rfbm, k.mx ), uc = r6_unicorn( *k.code, k.rfbm, k.mx, host_mx );
		// out[0] = MXCSR before (host-specific, not compared), out[1] = after, out[2..3] = XSAVE mxcsr/mask fields
		bool same = hw.fault == uc.fault && ( hw.fault >= 0 || ( hw.out[ 1 ] == uc.out[ 1 ] && hw.out[ 2 ] == uc.out[ 2 ] && hw.out[ 3 ] == uc.out[ 3 ] ) );
		auto show = [ & ]( const r6_result& r ) {
			char b[ 64 ];
			if ( r.fault >= 0 ) std::snprintf( b, sizeof( b ), "vector %d", r.fault );
			else std::snprintf( b, sizeof( b ), "mxcsr=%04X xs=%08X/%08X", r.out[ 1 ], r.out[ 2 ], r.out[ 3 ] );
			return std::string( b );
		};
		std::printf( "    %s hw: %-34s uc: %-34s %s\n", k.what, show( hw ).c_str(), show( uc ).c_str(), same ? "ok" : "DIFFERS" );
		CHECK( same, "%s", k.what );
	}
}

// ── R7: x87 control/status word loads (plan 1.10.2b) ───────────────────────────────────────────
//
// FLDCW, FLDENV, FRSTOR, FXRSTOR and XRSTOR(x87) load FCW (and FSW) from memory; hardware
// normalizes the reserved FCW bits and derives FSW.ES/B, the SDM only calls the bits "reserved"
// and defines ES as "one or more unmasked exceptions" (Vol1 8.1.3, 8.4). Each case runs the same
// self-generated thunk natively and in Unicorn and compares FNSTCW/FNSTSW right after the load.
// Thunk ABI: rcx = in { u16 fcw, u16 fsw, u16 fldcw_after, u16 do_fldcw }, rdx = out { u16 fcw,
// u16 fsw }, r8 = 64-aligned backup (the host x87/SSE state is restored from it), r9 = 64-aligned
// image. Only no-wait x87 instructions follow the load, so a pending unmasked exception cannot fire.

struct r7_result { int fault; uint16_t fcw, fsw; };

// 0 until plan 1.10.2c makes Unicorn raise pending x87 exceptions (#MF) on waiting instructions.
#define R7_PENDING_MF 0

static std::string r7_thunk( int kind )
{
	std::string s = "mov r10, rdx\nfxsave [r8]\nfninit\n";
	const char* patch_fxsave = "mov ax, word ptr [rcx]\nmov word ptr [r9], ax\nmov ax, word ptr [rcx + 2]\nmov word ptr [r9 + 2], ax\n";
	const char* patch_env = "mov ax, word ptr [rcx]\nmov word ptr [r9], ax\nmov ax, word ptr [rcx + 2]\nmov word ptr [r9 + 4], ax\n";
	switch ( kind )
	{
		case 0: s += "fldcw word ptr [rcx]\n"; break;
		case 1: s += std::string( "fnstenv [r9]\n" ) + patch_env + "fldenv [r9]\n"; break;
		case 2: s += std::string( "fnsave [r9]\n" ) + patch_env + "frstor [r9]\n"; break;
		case 3: s += std::string( "fxsave [r9]\n" ) + patch_fxsave + "fxrstor [r9]\n"; break;
		default:
			s += std::string( "mov eax, 1\nxor edx, edx\nxsave [r9]\n" ) + patch_fxsave +
				 "or byte ptr [r9 + 512], 1\nmov eax, 1\nxor edx, edx\nxrstor [r9]\n";
			break;
	}
	s += "cmp word ptr [rcx + 6], 0\nje r7_skip\nfldcw word ptr [rcx + 4]\nr7_skip:\n";
	s += "fnstcw word ptr [r10]\nfnstsw word ptr [r10 + 2]\nfxrstor [r8]\nret\n";
	return s;
}

static r7_result r7_native( const std::vector<uint8_t>& code, const std::vector<uint8_t>& restore, const uint16_t in[ 4 ] )
{
	r7_result r{ -1, 0, 0 };
	alignas( 64 ) static uint8_t backup[ 4096 ], image[ 4096 ];
	std::memset( image, 0, sizeof( image ) );
	uint16_t out[ 2 ] = {};
	void* mem = VirtualAlloc( nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
	if ( !mem ) return r;
	std::memcpy( mem, code.data(), code.size() );
	std::memcpy( ( uint8_t* ) mem + 0x1000, restore.data(), restore.size() );
	FlushInstructionCache( GetCurrentProcess(), mem, 0x2000 );
	using fn_t = void( * )( const uint16_t*, uint16_t*, void*, void* );
	__try { ( ( fn_t ) mem )( in, out, backup, image ); r.fcw = out[ 0 ]; r.fsw = out[ 1 ]; }
	__except ( EXCEPTION_EXECUTE_HANDLER )
	{
		// vector numbers so they compare with Unicorn's UC_HOOK_INTR: float status codes are #MF (16)
		DWORD code = GetExceptionCode();
		r.fault = ( code >= 0xC000008D && code <= 0xC0000093 ) || code == 0xC00002B4 || code == 0xC00002B5 ? 16
				: code == EXCEPTION_ILLEGAL_INSTRUCTION ? 6 : 13;
		( ( void( * )( void* ) ) ( ( uint8_t* ) mem + 0x1000 ) )( backup );     // fnclex; fxrstor [rcx]
	}
	VirtualFree( mem, 0, MEM_RELEASE );
	return r;
}

static r7_result r7_unicorn( const std::vector<uint8_t>& code, const uint16_t in[ 4 ], uint32_t host_mxcsr )
{
	r7_result r{ -1, 0, 0 };
	const uint64_t R7_CODE = 0x1000, R7_IN = 0x10000, R7_OUTP = 0x11000, R7_BACKUP = 0x12000, R7_IMAGE = 0x14000, R7_STACK = 0x30000;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
	uc_mem_map( uc, R7_CODE, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, R7_IN, 0x8000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_map( uc, R7_STACK, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, R7_CODE, code.data(), code.size() );
	uc_mem_write( uc, R7_IN, in, 8 );
	uint64_t rcx = R7_IN, rdx = R7_OUTP, r8 = R7_BACKUP, r9 = R7_IMAGE, rsp = R7_STACK + 0x8000, mx = host_mxcsr, xcr0 = 0x7;
	uc_reg_write( uc, UC_X86_REG_RCX, &rcx ); uc_reg_write( uc, UC_X86_REG_RDX, &rdx );
	uc_reg_write( uc, UC_X86_REG_R8, &r8 );   uc_reg_write( uc, UC_X86_REG_R9, &r9 );
	uc_reg_write( uc, UC_X86_REG_RSP, &rsp ); uc_reg_write( uc, UC_X86_REG_MXCSR, &mx );
	uc_reg_write( uc, UC_X86_REG_XCR0, &xcr0 );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_err e = uc_emu_start( uc, R7_CODE, R7_CODE + code.size() - 1, 0, 0 );     // stop before the final `ret`
	if ( e ) std::printf( "    uc_emu_start: %s\n", uc_strerror( e ) );
	if ( !log.seen.empty() ) r.fault = int( log.seen[ 0 ] );
	else { uint16_t out[ 2 ] = {}; uc_mem_read( uc, R7_OUTP, out, sizeof( out ) ); r.fcw = out[ 0 ]; r.fsw = out[ 1 ]; }
	uc_close( uc );
	return r;
}

static void test_r7()
{
	std::printf( "R7  x87 FCW/FSW loads: FLDCW, FLDENV, FRSTOR, FXRSTOR, XRSTOR(x87) vs hardware\n" );
	const char* names[] = { "fldcw", "fldenv", "frstor", "fxrstor", "xrstor" };
	std::vector<uint8_t> restore = assemble( "fnclex\nfxrstor [rcx]\nret\n" );
	uint32_t host_mx = 0x1F80;
	{
		std::vector<uint8_t> probe = assemble( "stmxcsr dword ptr [rcx]\nret\n" );
		void* mem = VirtualAlloc( nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
		if ( mem && !probe.empty() )
		{
			std::memcpy( mem, probe.data(), probe.size() );
			( ( void( * )( uint32_t* ) ) mem )( &host_mx );
			VirtualFree( mem, 0, MEM_RELEASE );
		}
	}
	const uint16_t fcws[] = { 0x0000, 0xFFFF, 0x037F, 0x0040, 0x0080, 0xE000, 0x1000, 0x0C00, 0x7A6F, 0xCDE5 };
	const uint16_t fsws[] = { 0x0000, 0x8080, 0x0001, 0x0020, 0x0041, 0x0002, 0x3800, 0x4700, 0xFFFF, 0x0B70, 0x5E38, 0x0040, 0x00C0 };
	for ( int kind = 0; kind < 5; ++kind )
	{
		std::vector<uint8_t> code = assemble( r7_thunk( kind ) );
		CHECK( !code.empty() && !restore.empty(), "%s: assembly", names[ kind ] );
		if ( code.empty() || restore.empty() ) continue;
		int cases = 0, diffs = 0, shown = 0, pending_mf = 0;
		auto run = [ & ]( uint16_t fcw, uint16_t fsw, uint16_t after, uint16_t do_after ) {
			const uint16_t in[ 4 ] = { fcw, fsw, after, do_after };
			r7_result hw = r7_native( code, restore, in ), uc = r7_unicorn( code, in, host_mx );
			bool same = hw.fault == uc.fault && ( hw.fault >= 0 || ( hw.fcw == uc.fcw && hw.fsw == uc.fsw ) );
			++cases;
			if ( !same && hw.fault == 16 && uc.fault < 0 && !R7_PENDING_MF )
			{
				// FLDCW is a waiting instruction: with FSW.ES=1 hardware raises #MF before it loads.
				// Unicorn does not raise pending x87 exceptions yet -> plan 1.10.2c flips this on.
				++pending_mf;
				return hw;
			}
			if ( !same )
			{
				++diffs;
				if ( shown++ < 12 )
					std::printf( "    %-7s in fcw=%04X fsw=%04X%s  hw: %s fcw=%04X fsw=%04X   uc: %s fcw=%04X fsw=%04X\n", names[ kind ], fcw, fsw,
								 do_after ? ( " +fldcw " + std::to_string( after ) ).c_str() : "", hw.fault >= 0 ? "fault" : "ok", hw.fcw, hw.fsw,
								 uc.fault >= 0 ? "fault" : "ok", uc.fcw, uc.fsw );
			}
			return hw;
		};
		for ( uint16_t fcw : fcws )
		{
			if ( kind == 0 ) { run( fcw, 0, 0, 0 ); continue; }
			for ( uint16_t fsw : fsws ) run( fcw, fsw, 0, 0 );
		}
		if ( kind == 1 )
		{
			// flags pending under a fully masked FCW, then FLDCW unmasks them: does ES/B follow at once?
			// and the reverse: flags pending and unmasked (ES=1), then FLDCW masks them again.
			for ( uint16_t fcw0 : { uint16_t( 0x037F ), uint16_t( 0x0340 ) } )
				for ( uint16_t fsw : { uint16_t( 0x0001 ), uint16_t( 0x0020 ), uint16_t( 0x0004 ), uint16_t( 0x0000 ) } )
					for ( uint16_t after : { uint16_t( 0x0340 ), uint16_t( 0x037E ), uint16_t( 0x035F ), uint16_t( 0x037F ) } )
					{
						r7_result hw = run( fcw0, fsw, after, 1 );
						std::printf( "    fldenv fcw=%04X fsw=%04X then fldcw %04X: hw %s fsw=%04X fcw=%04X\n", fcw0, fsw, after,
									 hw.fault >= 0 ? ( "fault " + std::to_string( hw.fault ) ).c_str() : "ok", hw.fsw, hw.fcw );
					}
		}
		std::printf( "    %-7s %3d cases, %d differ", names[ kind ], cases, diffs );
		if ( pending_mf ) std::printf( ", %d hardware #MF on a waiting instruction not raised by Unicorn yet (plan 1.10.2c)", pending_mf );
		std::printf( "\n" );
		CHECK( diffs == 0, "%s: %d of %d cases differ from hardware", names[ kind ], diffs, cases );
	}
}

int main()
{
	unsigned maj = 0, min = 0;
	uc_version( &maj, &min );
	std::printf( "uc72_risk: Unicorn %u.%u, QEMU 7.2.22 branch (plan 1.5)\n\n", maj, min );
	test_r1();
	std::printf( "\n" );
	test_r2();
	std::printf( "\n" );
	test_r3();
	std::printf( "\n" );
	test_r4();
	std::printf( "\n" );
	test_r5();
	std::printf( "\n" );
	test_r6();
	std::printf( "\n" );
	test_r7();
	std::printf( "\n%s: %d failure(s)\n", g_failures ? "FAILED" : "SUCCESS", g_failures );
	return g_failures ? 1 : 0;
}
