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
//   R8  non-canonical data references fault (#GP / #SS) instead of aliasing into mapped memory.
//   R9  MIN/MAX (SSE/AVX, scalar/packed) and F16C conversions under DAZ/FTZ, vs hardware.
//   R10 MMX <-> x87 aliasing (TOP/tags, ST(i) bits 79:64, CVTPI2Px m64), manual default and hardware quirks.
//   R11 x87 C1 rounding direction, precision control and stack overflow/underflow, every form vs hardware.
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
#include <iterator>
#include <xmmintrin.h>

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
	// fninit first: the RESET x87 state (SDM Vol3 Table 11-1: all eight registers valid +0.0) makes the
	// first FLD a stack overflow
	std::vector<uint8_t> code = assemble( std::string( "fninit\nfld qword ptr [rax]\nfld qword ptr [rax + 8]\nfxam\n" ) +
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

// ── R8: non-canonical data references (plan 1.10.3) ─────────────────────────────────────────────
//
// SDM Vol1 3.3.7.1 / Vol3 6.15: in 64-bit mode a non-canonical memory reference is #GP(0), or
// #SS(0) when it references the SS segment (RSP/RBP base, PUSH/POP). Raw Unicorn runs IA-32e with
// paging off and aliased such addresses into mapped memory. Natively, Windows reports both as an
// access violation; the record (code, info[0], info[1]) is printed so Phase 6 can map it.

struct r8_native_result { DWORD code; ULONG_PTR info0, info1; bool faulted; };

static r8_native_result r8_native( const std::vector<uint8_t>& code, uint64_t addr )
{
	r8_native_result r{ 0, 0, 0, false };
	void* mem = VirtualAlloc( nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
	if ( !mem ) return r;
	std::memcpy( mem, code.data(), code.size() );
	FlushInstructionCache( GetCurrentProcess(), mem, code.size() );
	EXCEPTION_RECORD rec{};
	__try { ( ( void( * )( uint64_t ) ) mem )( addr ); }
	__except ( rec = *GetExceptionInformation()->ExceptionRecord, EXCEPTION_EXECUTE_HANDLER )
	{
		r.faulted = true;
		r.code = rec.ExceptionCode;
		r.info0 = rec.NumberParameters > 0 ? rec.ExceptionInformation[ 0 ] : 0;
		r.info1 = rec.NumberParameters > 1 ? rec.ExceptionInformation[ 1 ] : 0;
	}
	VirtualFree( mem, 0, MEM_RELEASE );
	return r;
}

static int r8_unicorn( const std::vector<uint8_t>& code, uint64_t addr, bool& touched )
{
	const uint64_t R8_CODE = 0x1000, R8_DATA = 0x30000, R8_STACK = 0x100000;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_mem_map( uc, R8_CODE, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, R8_DATA, 0x1000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_map( uc, R8_STACK, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, R8_CODE, code.data(), code.size() );
	uint64_t rcx = addr, rsp = R8_STACK + 0x8000, marker = 0x1122334455667788ull;
	uc_mem_write( uc, R8_DATA, &marker, 8 );
	uc_reg_write( uc, UC_X86_REG_RCX, &rcx );
	uc_reg_write( uc, UC_X86_REG_RSP, &rsp );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_err e = uc_emu_start( uc, R8_CODE, R8_CODE + code.size() - 1, 0, 0 );     // stop before the final `ret`
	uint64_t now = 0;
	uc_mem_read( uc, R8_DATA, &now, 8 );
	touched = now != marker;
	uc_close( uc );
	if ( !log.seen.empty() ) return int( log.seen[ 0 ] );
	return e ? -2 : -1;
}

static void test_r8()
{
	std::printf( "R8  non-canonical data references in 64-bit mode (SDM: #GP(0), #SS(0) for SS references)\n" );
	struct c { const char* what; const char* text; uint64_t addr; int sdm_vector; };
	// aliases of the mapped page 0x30000 (high bits set): raw Unicorn used to read/write the page
	const c cases[] = {
		{ "mov rax, [rcx]        0x0000800000030000", "mov rax, qword ptr [rcx]\nret\n", 0x0000800000030000ull, 13 },
		{ "mov [rcx], rax        0x8000000000030000", "mov qword ptr [rcx], rax\nret\n", 0x8000000000030000ull, 13 },
		{ "bts [rcx], rax        rcx+(1<<62)/8     ", "mov rax, 0x4000000000000000\nbts qword ptr [rcx], rax\nret\n", 0x30000, 13 },
		// explicit SS override instead of an RBP base: the thunk must not touch RSP/RBP, because a
		// fault inside it is unwound as a leaf function (no unwind info for generated code)
		{ "mov rax, ss:[rcx]     0x0000800000030000", "mov rax, qword ptr ss:[rcx]\nret\n", 0x0000800000030000ull, 12 },
		{ "mov rax, [rcx] canon. 0x00007FFFFFFFF000", "mov rax, qword ptr [rcx]\nret\n", 0x00007FFFFFFFF000ull, 14 },
	};
	for ( const c& k : cases )
	{
		std::vector<uint8_t> code = assemble( k.text );
		CHECK( !code.empty(), "%s: assembly", k.what );
		if ( code.empty() ) continue;
		r8_native_result hw = r8_native( code, k.addr );
		bool touched = false;
		int v = r8_unicorn( code, k.addr, touched );
		std::printf( "    %s  hw: %s code=%08lX info=%llX/%llX   uc: %s%s   SDM vector %d\n", k.what, hw.faulted ? "fault" : "ok   ",
					 hw.code, ( unsigned long long ) hw.info0, ( unsigned long long ) hw.info1,
					 v >= 0 ? ( "vector " + std::to_string( v ) ).c_str() : v == -2 ? "uc error (unmapped)" : "no fault",
					 touched ? " (mapped page modified!)" : "", k.sdm_vector );
		CHECK( hw.faulted, "%s: hardware did not fault", k.what );
		CHECK( !touched, "%s: Unicorn aliased the address into mapped memory", k.what );
		if ( k.sdm_vector == 13 ) CHECK( v == 13, "%s: Unicorn vector %d, SDM #GP(0)", k.what, v );
		if ( k.sdm_vector == 12 ) CHECK( v == 13 || v == 12, "%s: Unicorn vector %d (SDM #SS(0); #GP accepted until 1.10.3b)", k.what, v );
	}
}

// ── R9: MIN/MAX and F16C conversions under DAZ/FTZ (plan 1.10.4) ────────────────────────────────
//
// SDM Vol2 MINPS/MAXPS...: MIN(a,b) = b when both are 0.0, when either is NaN, else the smaller;
// Vol1 Table 14-11/14-12: VCVTPH2PS on a denormal half input leaves DE unchanged, VCVTPS2PH sets DE
// and ignores FTZ. Each op runs as `op xmm0, xmm1` on the host and in Unicorn for every value pair x
// MXCSR {default, DAZ, FTZ, DAZ+FTZ}; the full XMM0 and the MXCSR flags must match.
// Thunk ABI: rcx = in { u32 mxcsr, pad[12], xmm0[16], xmm1[16] }, rdx = out { xmm0[16], u32 mxcsr, u32 host }.

struct r9_result { int fault; uint8_t x[ 16 ]; uint32_t mxcsr; };

static const char* r9_frame = "stmxcsr dword ptr [rdx + 20]\nldmxcsr dword ptr [rcx]\nmovups xmm0, xmmword ptr [rcx + 16]\n"
							  "movups xmm1, xmmword ptr [rcx + 32]\n%s\nstmxcsr dword ptr [rdx + 16]\nmovups xmmword ptr [rdx], xmm0\n"
							  "ldmxcsr dword ptr [rdx + 20]\nret\n";

static r9_result r9_native( const std::vector<uint8_t>& code, const uint8_t in[ 48 ], uint32_t host_mx )
{
	r9_result r{ -1, {}, 0 };
	uint8_t out[ 24 ] = {};
	void* mem = VirtualAlloc( nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
	if ( !mem ) return r;
	std::memcpy( mem, code.data(), code.size() );
	FlushInstructionCache( GetCurrentProcess(), mem, code.size() );
	__try { ( ( void( * )( const uint8_t*, uint8_t* ) ) mem )( in, out ); std::memcpy( r.x, out, 16 ); std::memcpy( &r.mxcsr, out + 16, 4 ); }
	__except ( EXCEPTION_EXECUTE_HANDLER )
	{
		DWORD c = GetExceptionCode();
		r.fault = ( c >= 0xC000008D && c <= 0xC0000093 ) || c == 0xC00002B4 || c == 0xC00002B5 ? 19 : c == EXCEPTION_ILLEGAL_INSTRUCTION ? 6 : 13;
		_mm_setcsr( host_mx );
	}
	VirtualFree( mem, 0, MEM_RELEASE );
	return r;
}

static r9_result r9_unicorn( const std::vector<uint8_t>& code, const uint8_t in[ 48 ], uint32_t host_mx )
{
	r9_result r{ -1, {}, 0 };
	const uint64_t R9_CODE = 0x1000, R9_IN = 0x10000, R9_OUTP = 0x11000, R9_STACK = 0x30000;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
	uc_mem_map( uc, R9_CODE, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, R9_IN, 0x2000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_map( uc, R9_STACK, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, R9_CODE, code.data(), code.size() );
	uc_mem_write( uc, R9_IN, in, 48 );
	uint64_t rcx = R9_IN, rdx = R9_OUTP, rsp = R9_STACK + 0x8000, mx = host_mx, xcr0 = 0x7;
	uc_reg_write( uc, UC_X86_REG_RCX, &rcx ); uc_reg_write( uc, UC_X86_REG_RDX, &rdx );
	uc_reg_write( uc, UC_X86_REG_RSP, &rsp ); uc_reg_write( uc, UC_X86_REG_MXCSR, &mx );
	uc_reg_write( uc, UC_X86_REG_XCR0, &xcr0 );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_err e = uc_emu_start( uc, R9_CODE, R9_CODE + code.size() - 1, 0, 0 );     // stop before the final `ret`
	if ( e ) std::printf( "    uc_emu_start: %s\n", uc_strerror( e ) );
	if ( !log.seen.empty() ) r.fault = int( log.seen[ 0 ] );
	else { uint8_t out[ 24 ] = {}; uc_mem_read( uc, R9_OUTP, out, sizeof( out ) ); std::memcpy( r.x, out, 16 ); std::memcpy( &r.mxcsr, out + 16, 4 ); }
	uc_close( uc );
	return r;
}

static void test_r9()
{
	std::printf( "R9  MIN/MAX and F16C under DAZ/FTZ: SSE/AVX scalar+packed vs hardware\n" );
	const uint32_t host_mx = _mm_getcsr();
	const uint32_t f32[] = { 0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x00800000, 0x3F800000, 0xBF800000,
							 0x7FC00001, 0x7F800001, 0xFFC00000, 0x7F800000 };
	const uint64_t f64[] = { 0, 0x8000000000000000ull, 1, 0x8000000000000001ull, 0x000FFFFFFFFFFFFFull, 0x0010000000000000ull,
							 0x3FF0000000000000ull, 0xBFF0000000000000ull, 0x7FF8000000000001ull, 0x7FF0000000000001ull,
							 0xFFF8000000000000ull, 0x7FF0000000000000ull };
	const uint16_t f16[] = { 0x0000, 0x8000, 0x0001, 0x8001, 0x03FF, 0x0400, 0x3C00, 0x7C01, 0x7E00, 0xFC00 };
	const uint32_t mxcsrs[] = { 0x1F80, 0x1FC0, 0x9F80, 0x9FC0 };
	struct op { const char* text; int kind; };     // kind: 0 = f32 pairs, 1 = f64 pairs, 2 = f16 source, 3 = f32 source (to half)
	const op ops[] = {
		{ "minss xmm0, xmm1", 0 }, { "maxss xmm0, xmm1", 0 }, { "minps xmm0, xmm1", 0 }, { "maxps xmm0, xmm1", 0 },
		{ "vminss xmm0, xmm0, xmm1", 0 }, { "vmaxps xmm0, xmm0, xmm1", 0 },
		{ "minsd xmm0, xmm1", 1 }, { "maxsd xmm0, xmm1", 1 }, { "minpd xmm0, xmm1", 1 }, { "maxpd xmm0, xmm1", 1 },
		{ "vminpd xmm0, xmm0, xmm1", 1 }, { "vmaxsd xmm0, xmm0, xmm1", 1 },
		{ "vcvtph2ps xmm0, xmm1", 2 }, { "vcvtps2ph xmm0, xmm1, 4", 3 }, { "vcvtps2ph xmm0, xmm1, 0", 3 },
	};
	for ( const op& o : ops )
	{
		char text[ 512 ];
		std::snprintf( text, sizeof( text ), r9_frame, o.text );
		std::vector<uint8_t> code = assemble( text );
		CHECK( !code.empty(), "%s: assembly", o.text );
		if ( code.empty() ) continue;
		int cases = 0, diffs = 0, shown = 0;
		size_t n = o.kind == 0 || o.kind == 3 ? std::size( f32 ) : o.kind == 1 ? std::size( f64 ) : std::size( f16 );
		for ( uint32_t mx : mxcsrs )
			for ( size_t a = 0; a < n; ++a )
				for ( size_t b = 0; b < ( o.kind >= 2 ? 1 : n ); ++b )
				{
					uint8_t in[ 48 ] = {};
					std::memcpy( in, &mx, 4 );
					for ( int lane = 0; lane < 4; ++lane )     // the same pair in every lane
					{
						if ( o.kind == 0 ) { std::memcpy( in + 16 + lane * 4, &f32[ a ], 4 ); std::memcpy( in + 32 + lane * 4, &f32[ b ], 4 ); }
						if ( o.kind == 1 && lane < 2 ) { std::memcpy( in + 16 + lane * 8, &f64[ a ], 8 ); std::memcpy( in + 32 + lane * 8, &f64[ b ], 8 ); }
						if ( o.kind == 2 ) std::memcpy( in + 32 + lane * 2, &f16[ a ], 2 );
						if ( o.kind == 3 ) std::memcpy( in + 32 + lane * 4, &f32[ a ], 4 );
					}
					r9_result hw = r9_native( code, in, host_mx ), uc = r9_unicorn( code, in, host_mx );
					bool same = hw.fault == uc.fault && ( hw.fault >= 0 || ( !std::memcmp( hw.x, uc.x, 16 ) && hw.mxcsr == uc.mxcsr ) );
					++cases;
					if ( same ) continue;
					++diffs;
					if ( shown++ >= 6 ) continue;
					uint64_t av = o.kind == 1 ? f64[ a ] : o.kind == 2 ? f16[ a ] : f32[ a ], bv = o.kind == 1 ? f64[ b ] : o.kind == 0 ? f32[ b ] : 0;
					uint64_t h0, u0;
					std::memcpy( &h0, hw.x, 8 ); std::memcpy( &u0, uc.x, 8 );
					std::printf( "    %-24s mxcsr=%04X a=%llX b=%llX  hw: %s lo=%016llX mxcsr=%04X   uc: %s lo=%016llX mxcsr=%04X\n", o.text, mx,
								 ( unsigned long long ) av, ( unsigned long long ) bv, hw.fault >= 0 ? "fault" : "ok", ( unsigned long long ) h0, hw.mxcsr,
								 uc.fault >= 0 ? "fault" : "ok", ( unsigned long long ) u0, uc.mxcsr );
				}
		std::printf( "    %-24s %4d cases, %d differ\n", o.text, cases, diffs );
		CHECK( diffs == 0, "%s: %d of %d cases differ from hardware", o.text, diffs, cases );
	}
}

// ── R10: MMX <-> x87 aliasing (plan 1.10.5) ─────────────────────────────────────────────────────
//
// SDM Vol1 9.5.2: an MMX instruction (not EMMS) sets TOP=0 and all tags valid, and an MMX register
// write sets bits 79:64 of the aliased x87 register to all ones. CVTPI2PD xmm, m64 makes no transition
// (Vol2 CVTPI2PD); the CVTPI2PS page does not exempt its m64 form, the i5-13600K does (hardware quirk).
// The x87 stack is made deterministic first (all registers 0.0, then 1.0/0.0/pi pushed: TOP=5), the
// op runs, then FNSTENV + FXSAVE capture FCW/FSW/FTW, the abridged tags, ST0-7 (80 bits) and XMM0.
// Thunk ABI: rcx = in { m64 at +0, xmm source at +16 }, rdx = out (64-aligned: fnstenv at +0, fxsave
// at +64), r8 = 64-aligned backup the host x87/SSE state is restored from.

struct r10_result { int fault; uint8_t env[ 28 ]; uint8_t fx[ 512 ]; };

static std::string r10_thunk( const char* op )
{
	std::string s = "fxsave [r8]\nfninit\n";
	for ( int i = 0; i < 8; ++i ) s += "fldz\n";
	s += "fninit\nfld1\nfldz\nfldpi\n";
	s += op;
	s += "\nfnstenv [rdx]\nfxsave [rdx + 64]\nfxrstor [r8]\nret\n";
	return s;
}

static r10_result r10_native( const std::vector<uint8_t>& code, const uint8_t in[ 32 ] )
{
	r10_result r{ -1, {}, {} };
	alignas( 64 ) static uint8_t backup[ 512 ], out[ 64 + 512 ];
	void* mem = VirtualAlloc( nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
	if ( !mem ) return r;
	std::memcpy( mem, code.data(), code.size() );
	FlushInstructionCache( GetCurrentProcess(), mem, code.size() );
	std::memset( out, 0, sizeof( out ) );
	__try
	{
		( ( void( * )( const uint8_t*, uint8_t*, void* ) ) mem )( in, out, backup );
		std::memcpy( r.env, out, 28 );
		std::memcpy( r.fx, out + 64, 512 );
	}
	__except ( EXCEPTION_EXECUTE_HANDLER ) { r.fault = 1; }
	VirtualFree( mem, 0, MEM_RELEASE );
	return r;
}

static r10_result r10_unicorn( const std::vector<uint8_t>& code, const uint8_t in[ 32 ], uint32_t quirks )
{
	r10_result r{ -1, {}, {} };
	const uint64_t R10_CODE = 0x1000, R10_IN = 0x10000, R10_OUTP = 0x11000, R10_BACKUP = 0x12000, R10_STACK = 0x30000;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
	uc_ctl_set_x86_hw_quirks( uc, quirks );
	uc_mem_map( uc, R10_CODE, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, R10_IN, 0x4000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_map( uc, R10_STACK, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, R10_CODE, code.data(), code.size() );
	uc_mem_write( uc, R10_IN, in, 32 );
	uint64_t rcx = R10_IN, rdx = R10_OUTP, r8 = R10_BACKUP, rsp = R10_STACK + 0x8000;
	uc_reg_write( uc, UC_X86_REG_RCX, &rcx ); uc_reg_write( uc, UC_X86_REG_RDX, &rdx );
	uc_reg_write( uc, UC_X86_REG_R8, &r8 );   uc_reg_write( uc, UC_X86_REG_RSP, &rsp );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_err e = uc_emu_start( uc, R10_CODE, R10_CODE + code.size() - 1, 0, 0 );     // stop before the final `ret`
	if ( e ) std::printf( "    uc_emu_start: %s\n", uc_strerror( e ) );
	if ( !log.seen.empty() ) r.fault = int( log.seen[ 0 ] );
	else { uc_mem_read( uc, R10_OUTP, r.env, 28 ); uc_mem_read( uc, R10_OUTP + 64, r.fx, 512 ); }
	uc_close( uc );
	return r;
}

// FCW, FSW, FTW of the FNSTENV image; abridged FTW, ST0-7 (10 bytes each) and XMM0 of the FXSAVE image
static std::string r10_diff( const r10_result& a, const r10_result& b )
{
	char buf[ 160 ];
	if ( a.fault != b.fault ) { std::snprintf( buf, sizeof( buf ), "fault %d vs %d", a.fault, b.fault ); return buf; }
	if ( a.fault >= 0 ) return "";
	auto w = [ & ]( const uint8_t* p, int o ) { return unsigned( p[ o ] | ( p[ o + 1 ] << 8 ) ); };
	for ( int o : { 0, 4, 8 } )
		if ( w( a.env, o ) != w( b.env, o ) )
		{
			std::snprintf( buf, sizeof( buf ), "%s %04X vs %04X", o == 0 ? "fcw" : o == 4 ? "fsw" : "ftw", w( a.env, o ), w( b.env, o ) );
			return buf;
		}
	if ( a.fx[ 4 ] != b.fx[ 4 ] ) { std::snprintf( buf, sizeof( buf ), "abridged ftw %02X vs %02X", a.fx[ 4 ], b.fx[ 4 ] ); return buf; }
	for ( int i = 0; i < 8; ++i )
		if ( std::memcmp( a.fx + 32 + i * 16, b.fx + 32 + i * 16, 10 ) )
		{
			std::snprintf( buf, sizeof( buf ), "st(%d) exp %04X mant %016llX vs exp %04X mant %016llX", i, w( a.fx, 32 + i * 16 + 8 ),
						   *( const unsigned long long* ) ( a.fx + 32 + i * 16 ), w( b.fx, 32 + i * 16 + 8 ), *( const unsigned long long* ) ( b.fx + 32 + i * 16 ) );
			return buf;
		}
	if ( std::memcmp( a.fx + 160, b.fx + 160, 16 ) ) return "xmm0";
	return "";
}

static void test_r10()
{
	std::printf( "R10 MMX <-> x87 aliasing: TOP/tags, ST(i) bits 79:64, CVTPI2Px m64 transition vs hardware\n" );
	struct c { const char* op; bool manual_differs; };     // manual_differs: the SDM text disagrees with the i5-13600K
	const c cases[] = {
		{ "movq mm0, qword ptr [rcx]", false }, { "movd mm0, dword ptr [rcx]", false }, { "paddb mm0, qword ptr [rcx]", false },
		{ "pinsrw mm0, word ptr [rcx], 1", false }, { "movups xmm1, xmmword ptr [rcx + 16]\ncvtps2pi mm0, xmm1", false },
		{ "movups xmm1, xmmword ptr [rcx + 16]\nmovdq2q mm0, xmm1", false }, { "pshufw mm0, qword ptr [rcx], 0x1b", false },
		{ "cvtpi2ps xmm0, qword ptr [rcx]", true }, { "cvtpi2pd xmm0, qword ptr [rcx]", false },
		{ "cvtpi2ps xmm0, mm2", false }, { "cvtpi2pd xmm0, mm2", false }, { "paddb mm0, qword ptr [rcx]\nemms", false },
		{ "movq mm3, qword ptr [rcx]\nmovq mm4, mm3", false }, { "pxor mm5, mm5", false }, { "movq2dq xmm0, mm2", false },
		{ "pmovmskb eax, mm2", false }, { "pabsb mm1, qword ptr [rcx]", false },
	};
	uint8_t in[ 32 ];
	for ( int i = 0; i < 32; ++i ) in[ i ] = uint8_t( 0x11 * ( i + 1 ) );
	const uint32_t hw_quirks = UC_X86_QUIRK_FCOMI_KEEPS_C1 | UC_X86_QUIRK_CVTPI2PS_M64_KEEPS_X87;
	for ( const c& k : cases )
	{
		std::vector<uint8_t> code = assemble( r10_thunk( k.op ) );
		CHECK( !code.empty(), "%s: assembly", k.op );
		if ( code.empty() ) continue;
		r10_result hw = r10_native( code, in ), man = r10_unicorn( code, in, 0 ), q = r10_unicorn( code, in, hw_quirks );
		std::string d_man = r10_diff( hw, man ), d_q = r10_diff( hw, q );
		std::string name = k.op;
		for ( char& ch : name ) if ( ch == '\n' ) ch = ';';
		std::printf( "    %-58s hw ftw=%04X  manual: %-40s quirks: %s\n", name.c_str(), unsigned( hw.env[ 8 ] | ( hw.env[ 9 ] << 8 ) ),
					 d_man.empty() ? "= hw" : ( k.manual_differs ? ( "SDM differs: " + d_man ) : d_man ).c_str(), d_q.empty() ? "= hw" : d_q.c_str() );
		CHECK( d_q.empty(), "%s: Unicorn with hardware quirks differs from hardware (%s)", k.op, d_q.c_str() );
		CHECK( k.manual_differs ? !d_man.empty() : d_man.empty(), "%s: Unicorn default (manual) %s", k.op,
			   k.manual_differs ? "should follow the SDM, not hardware" : ( "differs from hardware: " + d_man ).c_str() );
	}
}

// ── R11: x87 rounding (C1) and stack faults (plan 1.10.6 / 1.10.7) ──────────────────────────────
//
// Every x87 form runs on a stack built natively-identically on both sides: FNINIT, eight FLD m80
// (all physical registers defined, TOP=0), then FLDENV sets FCW (RC/PC/masks), FSW (TOP, C1 preset)
// and the tag word (empty registers). Scenarios: all valid x RC{4} x PC{24,53,64} x C1{0,1}; ST(7)
// empty (a push is legal); all empty with IM masked / unmasked (stack underflow; a push into a full
// stack is the overflow case of the all-valid scenarios). Compared: FCW/FSW/FTW (FNSTENV), ST0-7
// (FXSAVE), the 16-byte memory operand and SF/ZF/AF/PF/CF (LAHF). Only no-wait instructions follow
// the op, so a pending unmasked exception cannot fire. Thunk ABI: rcx = in { fldenv image[28] at +0,
// 8 x m80 at +32, memory operand[16] at +192 }, rdx = out (64-aligned: fnstenv +0, fxsave +64,
// memory +576, AH +592), r8 = 64-aligned host backup.

struct r11_result { int fault; uint8_t env[ 28 ]; uint8_t fx[ 512 ]; uint8_t mem[ 16 ]; uint8_t ah; };

static void r11_put_m80( uint8_t* p, double d )
{
	uint64_t bits;
	std::memcpy( &bits, &d, 8 );
	uint16_t sign = uint16_t( bits >> 63 ) << 15;
	int e = int( ( bits >> 52 ) & 0x7FF );
	uint64_t frac = bits & 0xFFFFFFFFFFFFFull, mant = 0;
	uint16_t se = sign;
	if ( e == 0 && frac == 0 ) { mant = 0; }
	else { mant = ( 1ull << 63 ) | ( frac << 11 ); se |= uint16_t( e - 1023 + 16383 ); }     // normals only in the table
	std::memcpy( p, &mant, 8 );
	std::memcpy( p + 8, &se, 2 );
}

static std::string r11_thunk( const char* op )
{
	std::string s = "fxsave [r8]\nfninit\n";
	for ( int i = 0; i < 8; ++i ) s += "fld tbyte ptr [rcx + " + std::to_string( 32 + i * 16 ) + "]\n";
	s += "fldenv [rcx]\n";
	s += op;
	s += "\nlahf\nmov byte ptr [rdx + 592], ah\nfnstenv [rdx]\nfxsave [rdx + 64]\n"
		 "mov rax, qword ptr [rcx + 192]\nmov qword ptr [rdx + 576], rax\nmov rax, qword ptr [rcx + 200]\nmov qword ptr [rdx + 584], rax\n"
		 "fxrstor [r8]\nret\n";
	return s;
}

static r11_result r11_native( const std::vector<uint8_t>& code, const uint8_t in[ 208 ] )
{
	r11_result r{ -1, {}, {}, {}, 0 };
	alignas( 64 ) static uint8_t backup[ 512 ], out[ 640 ], inbuf[ 256 ];
	std::memcpy( inbuf, in, 208 );
	void* mem = VirtualAlloc( nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
	if ( !mem ) return r;
	std::memcpy( mem, code.data(), code.size() );
	FlushInstructionCache( GetCurrentProcess(), mem, code.size() );
	std::memset( out, 0, sizeof( out ) );
	__try
	{
		( ( void( * )( uint8_t*, uint8_t*, void* ) ) mem )( inbuf, out, backup );
		std::memcpy( r.env, out, 28 ); std::memcpy( r.fx, out + 64, 512 ); std::memcpy( r.mem, out + 576, 16 ); r.ah = out[ 592 ];
	}
	__except ( EXCEPTION_EXECUTE_HANDLER ) { r.fault = 1; }
	VirtualFree( mem, 0, MEM_RELEASE );
	return r;
}

static r11_result r11_unicorn( const std::vector<uint8_t>& code, const uint8_t in[ 208 ], uint32_t quirks )
{
	r11_result r{ -1, {}, {}, {}, 0 };
	const uint64_t R11_CODE = 0x1000, R11_IN = 0x10000, R11_OUTP = 0x11000, R11_BACKUP = 0x12000, R11_STACK = 0x30000;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
	uc_ctl_set_x86_hw_quirks( uc, quirks );
	uc_mem_map( uc, R11_CODE, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, R11_IN, 0x4000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_map( uc, R11_STACK, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, R11_CODE, code.data(), code.size() );
	uc_mem_write( uc, R11_IN, in, 208 );
	uint64_t rcx = R11_IN, rdx = R11_OUTP, r8 = R11_BACKUP, rsp = R11_STACK + 0x8000;
	uc_reg_write( uc, UC_X86_REG_RCX, &rcx ); uc_reg_write( uc, UC_X86_REG_RDX, &rdx );
	uc_reg_write( uc, UC_X86_REG_R8, &r8 );   uc_reg_write( uc, UC_X86_REG_RSP, &rsp );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_err e = uc_emu_start( uc, R11_CODE, R11_CODE + code.size() - 1, 0, 0 );     // stop before the final `ret`
	if ( e ) std::printf( "    uc_emu_start: %s\n", uc_strerror( e ) );
	if ( !log.seen.empty() ) r.fault = int( log.seen[ 0 ] );
	else
	{
		uint8_t out[ 640 ];
		uc_mem_read( uc, R11_OUTP, out, sizeof( out ) );
		std::memcpy( r.env, out, 28 ); std::memcpy( r.fx, out + 64, 512 ); std::memcpy( r.mem, out + 576, 16 ); r.ah = out[ 592 ];
	}
	uc_close( uc );
	return r;
}

static std::string r11_diff( const r11_result& a, const r11_result& b, bool flags )
{
	char buf[ 200 ];
	if ( a.fault != b.fault ) { std::snprintf( buf, sizeof( buf ), "fault %d vs %d", a.fault, b.fault ); return buf; }
	if ( a.fault >= 0 ) return "";
	auto w = [ & ]( const uint8_t* p, int o ) { return unsigned( p[ o ] | ( p[ o + 1 ] << 8 ) ); };
	std::string d;
	for ( int o : { 0, 4, 8 } )
		if ( w( a.env, o ) != w( b.env, o ) )
		{
			std::snprintf( buf, sizeof( buf ), "%s %04X/%04X ", o == 0 ? "fcw" : o == 4 ? "fsw" : "ftw", w( a.env, o ), w( b.env, o ) );
			d += buf;
		}
	for ( int i = 0; i < 8; ++i )
		if ( std::memcmp( a.fx + 32 + i * 16, b.fx + 32 + i * 16, 10 ) )
		{
			std::snprintf( buf, sizeof( buf ), "st%d %04X:%016llX/%04X:%016llX ", i, w( a.fx, 40 + i * 16 ), *( const unsigned long long* ) ( a.fx + 32 + i * 16 ),
						   w( b.fx, 40 + i * 16 ), *( const unsigned long long* ) ( b.fx + 32 + i * 16 ) );
			d += buf;
			break;
		}
	if ( std::memcmp( a.mem, b.mem, 16 ) )
	{
		std::snprintf( buf, sizeof( buf ), "mem %016llX/%016llX ", *( const unsigned long long* ) a.mem, *( const unsigned long long* ) b.mem );
		d += buf;
	}
	if ( flags && ( a.ah & 0xD5 ) != ( b.ah & 0xD5 ) ) { std::snprintf( buf, sizeof( buf ), "ah %02X/%02X ", a.ah, b.ah ); d += buf; }
	return d;
}

static void test_r11()
{
	std::printf( "R11 x87 C1 rounding + stack underflow/overflow: every form x RC x PC x C1 vs hardware\n" );
	const char* ops[] = {
		"fadd st(0), st(1)", "fadd st(1), st(0)", "faddp st(1), st(0)", "fadd dword ptr [rcx + 192]", "fadd qword ptr [rcx + 192]",
		"fiadd dword ptr [rcx + 192]", "fiadd word ptr [rcx + 192]",
		"fsub st(0), st(1)", "fsubp st(1), st(0)", "fsub qword ptr [rcx + 192]", "fsubr st(0), st(1)", "fsubr dword ptr [rcx + 192]",
		"fisub dword ptr [rcx + 192]", "fisubr word ptr [rcx + 192]",
		"fmul st(0), st(1)", "fmul st(2), st(0)", "fmulp st(1), st(0)", "fmul qword ptr [rcx + 192]", "fimul dword ptr [rcx + 192]",
		"fdiv st(0), st(1)", "fdiv st(1), st(0)", "fdivp st(1), st(0)", "fdiv dword ptr [rcx + 192]", "fidiv word ptr [rcx + 192]",
		"fdivr st(0), st(1)", "fdivrp st(1), st(0)", "fdivr qword ptr [rcx + 192]", "fidivr dword ptr [rcx + 192]",
		"fsqrt", "frndint", "fscale", "fxtract", "fprem", "fprem1", "f2xm1", "fyl2x", "fyl2xp1", "fptan", "fpatan", "fsin", "fcos", "fsincos",
		"fld st(1)", "fld dword ptr [rcx + 192]", "fld qword ptr [rcx + 192]", "fld tbyte ptr [rcx + 192]",
		"fild word ptr [rcx + 192]", "fild dword ptr [rcx + 192]", "fild qword ptr [rcx + 192]", "fbld tbyte ptr [rcx + 192]",
		"fld1", "fldz", "fldpi", "fldl2e", "fldl2t", "fldlg2", "fldln2",
		"fst dword ptr [rcx + 192]", "fst qword ptr [rcx + 192]", "fstp tbyte ptr [rcx + 192]", "fst st(2)", "fstp st(2)",
		"fist word ptr [rcx + 192]", "fist dword ptr [rcx + 192]", "fistp qword ptr [rcx + 192]", "fisttp dword ptr [rcx + 192]",
		"fisttp qword ptr [rcx + 192]", "fbstp tbyte ptr [rcx + 192]",
		"fxch st(1)", "fchs", "fabs", "ftst", "fxam",
		"fcom st(1)", "fcomp st(1)", "fcompp", "fucom st(1)", "fucomp st(1)", "fucompp", "fcom dword ptr [rcx + 192]",
		"ficom word ptr [rcx + 192]", "fcomi st(0), st(1)", "fucomip st(0), st(1)",
		"stc\nfcmovb st(0), st(1)", "stc\nfcmovnb st(0), st(1)", "ffree st(1)", "fincstp", "fdecstp",
		// undocumented aliases: D9 D8+i (FSTP1, "fstpnce"), DF D0+i (FSTP8), DF D8+i (FSTP9), DD C8+i (FXCH4),
		// DF C8+i (FXCH7), DC D0+i (FCOM2), DC D8+i (FCOMP3), DE D0+i (FCOMP5), DF C0+i (FFREEP)
		".byte 0xd9, 0xd9", ".byte 0xdf, 0xd1", ".byte 0xdf, 0xd9", ".byte 0xdd, 0xc9", ".byte 0xdf, 0xc9",
		".byte 0xdc, 0xd1", ".byte 0xdc, 0xd9", ".byte 0xde, 0xd1", ".byte 0xdf, 0xc1",
	};
	const double vals[ 8 ] = { 1.0 / 3.0, 2.0, 10.0, -7.5, 0.1, 1e10, 3.0, 0.7 };
	uint8_t in[ 208 ] = {};
	for ( int i = 0; i < 8; ++i ) r11_put_m80( in + 32 + i * 16, vals[ i ] );
	r11_put_m80( in + 192, 1.0 / 3.0 );
	struct scen { uint16_t fcw, fsw, ftw; const char* name; };
	std::vector<scen> scens;
	for ( uint16_t rc = 0; rc < 4; ++rc )
		for ( uint16_t pc : { uint16_t( 0 ), uint16_t( 2 ), uint16_t( 3 ) } )
			for ( uint16_t c1 = 0; c1 < 2; ++c1 )
				scens.push_back( { uint16_t( 0x007F | ( pc << 8 ) | ( rc << 10 ) ), uint16_t( c1 << 9 ), 0x0000, "valid" } );
	scens.push_back( { 0x037F, 0x0000, 0xC000, "st(7) empty" } );
	scens.push_back( { 0x037F, 0x0200, 0xC000, "st(7) empty, C1=1" } );
	scens.push_back( { 0x007F, 0x0000, 0xC000, "st(7) empty, PC24" } );     // PC must not touch loads (SDM 8.1.5.2)
	scens.push_back( { 0x027F, 0x0000, 0xC000, "st(7) empty, PC53" } );
	scens.push_back( { 0x037E, 0x0000, 0x0000, "all valid, IM unmasked" } );
	scens.push_back( { 0x037F, 0x0200, 0xFFFF, "all empty, IM masked" } );
	scens.push_back( { 0x037E, 0x0000, 0xFFFF, "all empty, IM unmasked" } );
	int total = 0, total_diff = 0, forms_diff = 0, staged = 0;
	// Plan 1.10.6d / Phase 5 (D5, own softfloat): the transcendental results are Intel-microcode specific
	// (FSIN/FCOS/FPTAN/FSINCOS still go through host doubles, FYL2XP1 outside its SDM range is "undefined",
	// F2XM1/FYL2X/FPATAN differ by an ulp / in C1). Their value differences are staged; their stack-fault
	// responses (hardware FSW.SF = 1) must match like every other form.
	const char* transcendental[] = { "f2xm1", "fyl2x", "fyl2xp1", "fptan", "fpatan", "fsin", "fcos", "fsincos" };
	char dump_path[ 512 ] = {};
	FILE* dump = nullptr;
	if ( GetEnvironmentVariableA( "UC72_R11_DUMP", dump_path, sizeof( dump_path ) ) ) fopen_s( &dump, dump_path, "w" );     // every difference, for analysis
	for ( const char* op : ops )
	{
		std::vector<uint8_t> code = assemble( r11_thunk( op ) );
		CHECK( !code.empty(), "%s: assembly", op );
		if ( code.empty() ) continue;
		bool flags = std::strstr( op, "fcomi" ) || std::strstr( op, "fucomi" );
		int diffs = 0, shown = 0;
		for ( const scen& sc : scens )
		{
			uint8_t buf[ 208 ];
			std::memcpy( buf, in, 208 );
			std::memset( buf, 0, 28 );
			std::memcpy( buf + 0, &sc.fcw, 2 ); std::memcpy( buf + 4, &sc.fsw, 2 ); std::memcpy( buf + 8, &sc.ftw, 2 );
			r11_result hw = r11_native( code, buf ), uc = r11_unicorn( code, buf, UC_X86_QUIRK_FCOMI_KEEPS_C1 | UC_X86_QUIRK_CVTPI2PS_M64_KEEPS_X87 );
			std::string d = r11_diff( hw, uc, flags );
			++total;
			if ( d.empty() ) continue;
			std::string name = op;
			for ( char& ch : name ) if ( ch == '\n' ) ch = ';';
			bool is_trans = false;
			for ( const char* t : transcendental ) is_trans |= name == t;
			if ( is_trans && hw.fault < 0 && !( hw.env[ 4 ] & 0x40 ) )
			{
				++staged;
				if ( dump ) std::fprintf( dump, "%s|%s|%04X|%04X|%04X|STAGED %s\n", name.c_str(), sc.name, sc.fcw, sc.fsw, sc.ftw, d.c_str() );
				continue;
			}
			++diffs; ++total_diff;
			if ( dump ) std::fprintf( dump, "%s|%s|%04X|%04X|%04X|%s\n", name.c_str(), sc.name, sc.fcw, sc.fsw, sc.ftw, d.c_str() );
			if ( shown++ < 3 )
				std::printf( "    %-30s %-22s fcw=%04X fsw=%04X ftw=%04X  hw/uc: %s\n", name.c_str(), sc.name, sc.fcw, sc.fsw, sc.ftw, d.c_str() );
		}
		if ( diffs ) ++forms_diff;
	}
	if ( dump ) std::fclose( dump );
	std::printf( "    %d forms x %zu scenarios = %d cases, %d differ in %d forms; %d transcendental value differences staged (plan 1.10.6d)\n",
				 int( std::size( ops ) ), scens.size(), total, total_diff, forms_diff, staged );
	CHECK( total_diff == 0, "%d of %d x87 cases differ from hardware", total_diff, total );
}

int main()
{
	std::setvbuf( stdout, nullptr, _IONBF, 0 );     // a crash must not swallow buffered output
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
	std::printf( "\n" );
	test_r8();
	std::printf( "\n" );
	test_r9();
	std::printf( "\n" );
	test_r10();
	std::printf( "\n" );
	test_r11();
	std::printf( "\n%s: %d failure(s)\n", g_failures ? "FAILED" : "SUCCESS", g_failures );
	return g_failures ? 1 : 0;
}
