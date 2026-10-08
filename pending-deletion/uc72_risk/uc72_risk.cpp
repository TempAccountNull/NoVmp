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
//   R12 state after uc_open = SDM RESET state (FCW/FSW/FTW, MXCSR, DR6/DR7).
//   R13 FPREM/FPREM1 quotient bits and remainders over random operands vs hardware.
//   R14 pending unmasked x87 exception: which instructions take #MF first, vs hardware.
//   R15 CR0.TS/EM, CR4.OSFXSR on x87/MMX/MMX-state SSE forms and the CVTPI2PS m64 #MF, vs the SDM.
//   R16 branches to a non-canonical target fault on the branch (RIP, RSP), vs hardware and the SDM.
//   R17 x87 transcendentals (special / random / exponent-sweep operands, every mask and RC) vs hardware.
//   R18 x87 arithmetic, compares, loads and stores (every form, special operands, unmasked exceptions,
//       FSW preset) vs hardware, bit-exact: the SDM Vol1 8.5 responses.
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
#include <map>
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

// plan 1.10.2c (ledger U50): Unicorn raises pending x87 exceptions (#MF) on waiting instructions.
#define R7_PENDING_MF 1

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
	uint64_t cr0 = 0;
	uc_reg_read( uc, UC_X86_REG_CR0, &cr0 );
	cr0 |= 0x20;     // CR0.NE: native #MF (Windows), not FERR#
	uc_reg_write( uc, UC_X86_REG_CR0, &cr0 );
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
		// the thunk must not change RSP/RBP (a fault inside it is unwound as a leaf function), so the
		// stack reference is [rsp + rcx]; an SS override on a non-stack register is ignored (SDM 3.3.7.1)
		{ "mov rax, ss:[rcx]     0x0000800000030000", "mov rax, qword ptr ss:[rcx]\nret\n", 0x0000800000030000ull, 13 },
		{ "mov rax, [rsp + rcx]  rcx=8000000000000000", "mov rax, qword ptr [rsp + rcx]\nret\n", 0x8000000000000000ull, 12 },
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
		if ( k.sdm_vector != 14 ) CHECK( v == k.sdm_vector, "%s: Unicorn vector %d, SDM %d", k.what, v, k.sdm_vector );
	}

	// Unicorn only (a non-canonical RSP/RBP cannot be run natively): SDM Vol1 3.3.7.1 - implied stack
	// references and RSP/RBP bases -> #SS(0); FS/GS override on them -> #GP(0); CS/DS/ES/SS overrides ignored.
	std::printf( "    -- Unicorn vs SDM (bad = 8000000000001000) --\n" );
	struct u { const char* text; bool bad_rsp, bad_rbp, bad_rcx, bad_r13; int sdm_vector; };
	const u ucases[] = {
		{ "push rax", true, false, false, false, 12 },
		{ "call l1\nl1:", true, false, false, false, 12 },
		{ "ret", true, false, false, false, 12 },
		{ "pushfq", true, false, false, false, 12 },
		{ "leave", false, true, false, false, 12 },
		{ "mov rax, qword ptr [rbp + 8]", false, true, false, false, 12 },
		{ "fxsave [rbp]", false, true, false, false, 12 },
		{ "vmovdqu ymm0, ymmword ptr [rsp + rcx]", false, false, true, false, 12 },
		{ "mov rax, qword ptr ds:[rsp + rcx]", false, false, true, false, 12 },
		{ "mov rax, qword ptr fs:[rsp + rcx]", false, false, true, false, 13 },
		{ "push qword ptr [rsp + rcx]", false, false, true, false, 12 },
		{ "push qword ptr [rcx]", false, false, true, false, 13 },
		{ "pop qword ptr [rcx]", false, false, true, false, 13 },
		{ "call qword ptr [rcx]", false, false, true, false, 13 },
		{ "mov rax, qword ptr [r13]", false, false, false, true, 13 },
		{ "mov rax, qword ptr [rcx]", false, false, true, false, 13 },
	};
	for ( const u& k : ucases )
	{
		std::vector<uint8_t> code = assemble( k.text );
		CHECK( !code.empty(), "%s: assembly", k.text );
		if ( code.empty() ) continue;
		const uint64_t bad = 0x8000000000001000ull;
		uc_engine* uc = nullptr;
		uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
		uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
		uc_mem_map( uc, 0x1000, 0x1000, UC_PROT_ALL );
		uc_mem_map( uc, 0x100000, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
		uc_mem_write( uc, 0x1000, code.data(), code.size() );
		uint64_t good = 0x108000, rsp = k.bad_rsp ? bad : good, rbp = k.bad_rbp ? bad : good,
				 rcx = k.bad_rcx ? ( std::strstr( k.text, "rsp + rcx" ) ? 0x8000000000000000ull : bad ) : good, r13 = k.bad_r13 ? bad : good;
		uint64_t pushed = 0x1000;     // ret / pop targets stay mapped
		uc_mem_write( uc, good, &pushed, 8 );
		uc_reg_write( uc, UC_X86_REG_RSP, &rsp ); uc_reg_write( uc, UC_X86_REG_RBP, &rbp );
		uc_reg_write( uc, UC_X86_REG_RCX, &rcx ); uc_reg_write( uc, UC_X86_REG_R13, &r13 );
		intr_log log;
		uc_hook h;
		uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
		uc_emu_start( uc, 0x1000, 0x1000 + code.size(), 0, 1 );
		uc_close( uc );
		int v = log.seen.empty() ? -1 : int( log.seen[ 0 ] );
		std::string name = k.text;
		for ( char& ch : name ) if ( ch == '\n' ) ch = ';';
		std::printf( "    %-40s SDM #%s  uc %s\n", name.c_str(), k.sdm_vector == 12 ? "SS" : "GP",
					 v == 12 ? "#SS" : v == 13 ? "#GP" : ( "vector " + std::to_string( v ) ).c_str() );
		CHECK( v == k.sdm_vector, "%s: Unicorn vector %d, SDM %d", name.c_str(), v, k.sdm_vector );
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
	// known EFLAGS before the op: AH (LAHF) must not depend on the caller's last compare
	s += "xor eax, eax\n";
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

// every documented i5-13600K deviation from the SDM, for the hardware comparisons
static constexpr uint32_t X87_HW_QUIRKS = UC_X86_QUIRK_FCOMI_KEEPS_C1 | UC_X86_QUIRK_CVTPI2PS_M64_KEEPS_X87 |
                                         UC_X86_QUIRK_FYL2XP1_BELOW_M1 | UC_X86_QUIRK_X87_CMP_UNMASKED_IA_SETS_CC;

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
			r11_result hw = r11_native( code, buf ), uc = r11_unicorn( code, buf, X87_HW_QUIRKS );
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

// ── R12: RESET state (plan 1.10.6e) ─────────────────────────────────────────────────────────────
//
// SDM Vol3 Table 11-1 (power-up / RESET): FCW 0040H, FSW 0000H, FTW 5555H (all valid +0.0),
// MXCSR 1F80H, DR6 FFFF0FF0H, DR7 00000400H. FNSTENV's full tag word reports 5555H for the zeros.

static void test_r12()
{
	std::printf( "R12 Unicorn state after uc_open vs SDM Vol3 Table 11-1 (RESET)\n" );
	std::vector<uint8_t> code = assemble( "fnstenv [rax]\nstmxcsr dword ptr [rax + 32]\n" );
	CHECK( !code.empty(), "assembly" );
	if ( code.empty() ) return;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uint64_t fpcw = 0, fpsw = 0, mxcsr = 0, dr6 = 0, dr7 = 0;
	uc_reg_read( uc, UC_X86_REG_FPCW, &fpcw );
	uc_reg_read( uc, UC_X86_REG_FPSW, &fpsw );
	uc_reg_read( uc, UC_X86_REG_MXCSR, &mxcsr );
	uc_reg_read( uc, UC_X86_REG_DR6, &dr6 );
	uc_reg_read( uc, UC_X86_REG_DR7, &dr7 );
	uc_mem_map( uc, 0x1000, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, 0x2000, 0x1000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, 0x1000, code.data(), code.size() );
	uint64_t rax = 0x2000;
	uc_reg_write( uc, UC_X86_REG_RAX, &rax );
	uc_err e = uc_emu_start( uc, 0x1000, 0x1000 + code.size(), 0, 0 );
	uint8_t env[ 36 ] = {};
	uc_mem_read( uc, 0x2000, env, sizeof( env ) );
	uc_close( uc );
	unsigned ftw = env[ 8 ] | ( env[ 9 ] << 8 ), mx2 = env[ 32 ] | ( env[ 33 ] << 8 );
	std::printf( "    FCW=%04llX FSW=%04llX MXCSR=%04llX DR6=%08llX DR7=%08llX  FNSTENV FTW=%04X STMXCSR=%04X (%s)\n",
				 ( unsigned long long ) fpcw, ( unsigned long long ) fpsw, ( unsigned long long ) mxcsr, ( unsigned long long ) dr6,
				 ( unsigned long long ) dr7, ftw, mx2, e ? uc_strerror( e ) : "ok" );
	CHECK( fpcw == 0x0040, "FCW %04llX, SDM 0040H", ( unsigned long long ) fpcw );
	CHECK( ( fpsw & 0xFFFF ) == 0, "FSW %04llX, SDM 0000H", ( unsigned long long ) fpsw );
	CHECK( mxcsr == 0x1F80 && mx2 == 0x1F80, "MXCSR %04llX / %04X, SDM 1F80H", ( unsigned long long ) mxcsr, mx2 );
	CHECK( dr6 == 0xFFFF0FF0 && dr7 == 0x400, "DR6/DR7 %llX/%llX", ( unsigned long long ) dr6, ( unsigned long long ) dr7 );
	CHECK( ftw == 0x5555, "FTW %04X, SDM 5555H", ftw );
}

// ── R13: FPREM / FPREM1 over random operands (plan 1.10.6f) ────────────────────────────────────
//
// ST0 = dividend, ST1 = divisor, random 80-bit encodings: exponent differences 0..90 (beyond 63 the
// result is a partial remainder, C2 = 1), both signs, plus zero / denormal / infinity operands. The
// quotient bits Q2,Q1,Q0 go to C0,C3,C1 (SDM Vol2 FPREM/FPREM1). Same thunk and compare as R11.

static void test_r13()
{
	std::printf( "R13 FPREM/FPREM1 quotient bits and remainders over random operands vs hardware\n" );
	rng r{ 0xF00DFACE12345678ull };
	auto put = [ & ]( uint8_t* p, uint16_t se, uint64_t mant ) { std::memcpy( p, &mant, 8 ); std::memcpy( p + 8, &se, 2 ); };
	int total = 0, diffs = 0;
	for ( const char* op : { "fprem", "fprem1" } )
	{
		std::vector<uint8_t> code = assemble( r11_thunk( op ) );
		CHECK( !code.empty(), "%s: assembly", op );
		if ( code.empty() ) continue;
		int shown = 0, n_op = 0, d_op = 0;
		for ( int k = 0; k < 600; ++k )
		{
			uint8_t in[ 208 ] = {};
			for ( int i = 0; i < 8; ++i ) put( in + 32 + i * 16, 0x3FFF, 0x8000000000000000ull );     // 1.0 elsewhere
			uint16_t e1 = uint16_t( 0x3FFF + int( r.below( 40 ) ) - 20 );                               // divisor
			uint16_t e0 = uint16_t( e1 + int( r.below( 91 ) ) - ( k % 4 == 0 ? 10 : 0 ) );              // dividend
			uint64_t m0 = ( 1ull << 63 ) | ( uint64_t( r.next() ) << 32 ) | r.next(), m1 = ( 1ull << 63 ) | ( uint64_t( r.next() ) << 32 ) | r.next();
			if ( k % 3 == 0 ) m1 &= ~0xFFFFFFFFull;                                                    // short divisors too
			uint16_t s0 = r.below( 2 ) ? 0x8000 : 0, s1 = r.below( 2 ) ? 0x8000 : 0;
			switch ( k % 50 )
			{
				case 1: e0 = 0; m0 = 0; break;                                          // 0 rem x
				case 2: e1 = 0; m1 = 0; break;                                          // x rem 0  -> #IA
				case 3: e0 = 0x7FFF; m0 = 1ull << 63; break;                           // inf rem x -> #IA
				case 4: e1 = 0x7FFF; m1 = 1ull << 63; break;                           // x rem inf
				case 5: e0 = 0; m0 >>= 5; break;                                        // denormal dividend
				case 6: e1 = 0; m1 >>= 7; break;                                        // denormal divisor
				default: break;
			}
			put( in + 32 + 7 * 16, uint16_t( s0 | e0 ), m0 );     // ST0 (last pushed)
			put( in + 32 + 6 * 16, uint16_t( s1 | e1 ), m1 );     // ST1
			uint16_t fcw = 0x037F, fsw = 0, ftw = 0;
			std::memcpy( in + 0, &fcw, 2 ); std::memcpy( in + 4, &fsw, 2 ); std::memcpy( in + 8, &ftw, 2 );
			r11_result hw = r11_native( code, in ), uc = r11_unicorn( code, in, 0 );
			std::string d = r11_diff( hw, uc, false );
			++total; ++n_op;
			if ( d.empty() ) continue;
			++diffs; ++d_op;
			if ( shown++ < 8 )
				std::printf( "    %-6s st0=%04X:%016llX st1=%04X:%016llX  hw/uc: %s\n", op, unsigned( s0 | e0 ), ( unsigned long long ) m0,
							 unsigned( s1 | e1 ), ( unsigned long long ) m1, d.c_str() );
		}
		std::printf( "    %-6s %d cases, %d differ\n", op, n_op, d_op );
	}
	CHECK( diffs == 0, "%d of %d FPREM/FPREM1 cases differ from hardware", diffs, total );
}

// ── R14: pending x87 exceptions -> #MF on waiting instructions (plan 1.10.2c) ──────────────────
//
// FLDENV with FCW.IM = 0 and FSW.IE = 1 leaves FSW.ES = 1 (an unmasked exception pending). The next
// "waiting" x87 instruction, WAIT/FWAIT and any MMX instruction raise #MF before they execute; the
// FN* control instructions, FXSAVE/FXRSTOR and XSAVE do not (SDM Vol1 8.3.12/8.7.1/9.6.7, Vol3
// Interrupt 16). Unicorn needs CR0.NE = 1 (Windows runs with it) for #MF instead of FERR#/IRQ13.
// Thunk ABI: rcx = in { fldenv image at +0, memory operand at +64, scratch at +128 }, rdx = out
// (u32 marker, written only if the op completed), r8 = 64-aligned host backup, r9 = 64-aligned scratch.

static std::string r14_thunk( const char* op )
{
	return std::string( "mov r10, rdx\nfxsave [r8]\nfninit\nfldenv [rcx]\n" ) + op +
		   "\nmov dword ptr [r10], 0x600D\nfnclex\nfxrstor [r8]\nret\n";
}

static int r14_native( const std::vector<uint8_t>& code, const std::vector<uint8_t>& restore, uint8_t* in )
{
	alignas( 64 ) static uint8_t backup[ 4096 ], scratch[ 4096 ];
	uint32_t out = 0;
	int fault = -1;
	void* mem = VirtualAlloc( nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
	if ( !mem ) return -2;
	std::memcpy( mem, code.data(), code.size() );
	std::memcpy( ( uint8_t* ) mem + 0x1000, restore.data(), restore.size() );
	FlushInstructionCache( GetCurrentProcess(), mem, 0x2000 );
	__try { ( ( void( * )( uint8_t*, uint32_t*, void*, void* ) ) mem )( in, &out, backup, scratch ); }
	__except ( EXCEPTION_EXECUTE_HANDLER )
	{
		DWORD c = GetExceptionCode();
		fault = ( c >= 0xC000008D && c <= 0xC0000093 ) || c == 0xC00002B4 || c == 0xC00002B5 ? 16 : c == EXCEPTION_ILLEGAL_INSTRUCTION ? 6 : 13;
		( ( void( * )( void* ) ) ( ( uint8_t* ) mem + 0x1000 ) )( backup );     // fnclex; fxrstor [rcx]
	}
	VirtualFree( mem, 0, MEM_RELEASE );
	return fault >= 0 ? fault : out == 0x600D ? -1 : -3;
}

static int r14_unicorn( const std::vector<uint8_t>& code, const uint8_t* in, uint32_t host_mx )
{
	const uint64_t R14_CODE = 0x1000, R14_IN = 0x10000, R14_OUTP = 0x11000, R14_BACKUP = 0x12000, R14_SCRATCH = 0x14000, R14_STACK = 0x30000;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
	uc_ctl_set_x86_hw_quirks( uc, X87_HW_QUIRKS );
	uc_mem_map( uc, R14_CODE, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, R14_IN, 0x8000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_map( uc, R14_STACK, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, R14_CODE, code.data(), code.size() );
	uc_mem_write( uc, R14_IN, in, 256 );
	uint64_t cr0 = 0;
	uc_reg_read( uc, UC_X86_REG_CR0, &cr0 );
	cr0 |= 0x20;     // CR0.NE: native #MF
	uc_reg_write( uc, UC_X86_REG_CR0, &cr0 );
	uint64_t rcx = R14_IN, rdx = R14_OUTP, r8 = R14_BACKUP, r9 = R14_SCRATCH, rsp = R14_STACK + 0x8000, mx = host_mx, xcr0 = 0x7, rax = 0;
	uc_reg_write( uc, UC_X86_REG_RCX, &rcx ); uc_reg_write( uc, UC_X86_REG_RDX, &rdx );
	uc_reg_write( uc, UC_X86_REG_R8, &r8 );   uc_reg_write( uc, UC_X86_REG_R9, &r9 );
	uc_reg_write( uc, UC_X86_REG_RSP, &rsp ); uc_reg_write( uc, UC_X86_REG_MXCSR, &mx );
	uc_reg_write( uc, UC_X86_REG_XCR0, &xcr0 ); uc_reg_write( uc, UC_X86_REG_RAX, &rax );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_err e = uc_emu_start( uc, R14_CODE, R14_CODE + code.size() - 1, 0, 0 );     // stop before the final `ret`
	uint32_t out = 0;
	uc_mem_read( uc, R14_OUTP, &out, 4 );
	uc_close( uc );
	if ( !log.seen.empty() ) return int( log.seen[ 0 ] );
	if ( e ) return -4;
	return out == 0x600D ? -1 : -3;
}

static void test_r14()
{
	std::printf( "R14 pending unmasked x87 exception: which instructions take #MF first (vs hardware)\n" );
	const char* ops[] = {
		// x87 waiting forms (expected #MF)
		"fld1", "fadd st(0), st(1)", "fld dword ptr [rcx + 64]", "fst dword ptr [rcx + 64]", "fxch st(1)", "fchs", "fabs",
		"fxam", "ftst", "fnop", "ffree st(1)", "fincstp", "fdecstp", "fldcw word ptr [rcx + 64]", "fldenv [rcx]",
		"frstor [rcx + 128]", "fcomi st(0), st(1)", "stc\nfcmovb st(0), st(1)", "fwait", "fsin", "fprem", "fbld tbyte ptr [rcx + 64]",
		// no-wait forms
		"fnstenv [rcx + 128]", "fnstcw word ptr [rcx + 64]", "fnsave [rcx + 128]", "fnstsw word ptr [rcx + 64]", "fnstsw ax",
		"fnclex", "fninit", "fxsave [r9]", "fxrstor [r8]", "mov eax, 1\nxor edx, edx\nxsave [r9]",
		".byte 0xdb, 0xe0", ".byte 0xdb, 0xe1", ".byte 0xdb, 0xe4",     // FNENI, FNDISI, FNSETPM (SDM silent)
		// MMX and MMX-state SSE forms
		"movq mm0, mm1", "paddb mm0, mm1", "emms", "movd mm0, eax", "movd eax, mm0", "pshufw mm0, mm1, 0", "pinsrw mm0, eax, 1",
		"pmovmskb eax, mm0", "movntq qword ptr [rcx + 64], mm0", "cvtpi2ps xmm0, mm1", "cvtpi2ps xmm0, qword ptr [rcx + 64]",
		"cvtpi2pd xmm0, mm1", "cvtpi2pd xmm0, qword ptr [rcx + 64]", "cvtps2pi mm0, xmm1", "cvttpd2pi mm0, xmm1",
		"movq2dq xmm0, mm1", "movdq2q mm0, xmm1", "pabsb mm0, mm1", "pshufb mm0, qword ptr [rcx + 64]",
		// SSE (expected: no #MF)
		"addps xmm0, xmm1", "movq xmm0, xmm1",
	};
	uint8_t in[ 256 ] = {};
	const uint16_t fcw = 0x037E, fsw = 0x0001;     // IM unmasked, IE set -> ES = 1
	std::memcpy( in + 0, &fcw, 2 ); std::memcpy( in + 4, &fsw, 2 );
	uint16_t ftw = 0xFFFF;
	std::memcpy( in + 8, &ftw, 2 );
	std::vector<uint8_t> restore = assemble( "fnclex\nfxrstor [rcx]\nret\n" );
	uint32_t host_mx = _mm_getcsr();
	int diffs = 0, staged = 0;
	auto name_of = [ & ]( int v ) { return v == 16 ? std::string( "#MF" ) : v == -1 ? std::string( "runs" ) : "v" + std::to_string( v ); };
	for ( const char* op : ops )
	{
		std::vector<uint8_t> code = assemble( r14_thunk( op ) );
		CHECK( !code.empty(), "%s: assembly", op );
		if ( code.empty() || restore.empty() ) continue;
		uint8_t buf[ 256 ];
		std::memcpy( buf, in, sizeof( buf ) );
		int hw = r14_native( code, restore, buf );
		std::memcpy( buf, in, sizeof( buf ) );
		int uc = r14_unicorn( code, buf, host_mx );
		std::string name = op;
		for ( char& ch : name ) if ( ch == '\n' ) ch = ';';
		bool same = hw == uc;
		std::printf( "    %-40s hw: %-5s uc: %-5s %s\n", name.c_str(), name_of( hw ).c_str(), name_of( uc ).c_str(), same ? "ok" : "DIFFERS" );
		if ( !same ) ++diffs;
	}
	CHECK( diffs == 0, "%d instructions differ in pending-#MF behaviour", diffs );
	( void ) staged;
}

// ── R15: CR0.TS / CR0.EM / CR4.OSFXSR on x87, MMX and MMX-state SSE forms (plan 1.10.2c) ──────
//
// Unicorn only (user mode cannot set CR0/CR4 natively), expectations from the SDM Vol2 exception
// tables: x87 with TS or EM -> #NM; MMX and the MMX-state SSE forms with TS -> #NM, with EM -> #UD;
// the SSE side (CVT*PI*, MOVQ2DQ, MOVDQ2Q) with OSFXSR = 0 -> #UD, EMMS unaffected. Also the
// manual-default #MF of CVTPI2PS xmm, m64 (no hardware quirk).

static int r15_run( const char* text, uint64_t cr0_set, uint64_t cr0_clr, uint64_t cr4_clr, bool pending_mf, uint32_t quirks )
{
	std::vector<uint8_t> code = assemble( text );
	if ( code.empty() ) return -9;
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
	uc_ctl_set_x86_hw_quirks( uc, quirks );
	uc_mem_map( uc, 0x1000, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, 0x10000, 0x1000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, 0x1000, code.data(), code.size() );
	if ( pending_mf )
	{
		// FCW: IM unmasked, FSW: IE set -> ES = 1, through FLDCW/FLDENV-free register writes
		uint64_t fcw = 0x037E, fsw = 0x0081;
		uc_reg_write( uc, UC_X86_REG_FPCW, &fcw );
		uc_reg_write( uc, UC_X86_REG_FPSW, &fsw );
	}
	uint64_t cr0 = 0, cr4 = 0, rcx = 0x10000;
	uc_reg_read( uc, UC_X86_REG_CR0, &cr0 );
	uc_reg_read( uc, UC_X86_REG_CR4, &cr4 );
	cr0 = ( cr0 | cr0_set | 0x20 ) & ~cr0_clr;
	cr4 &= ~cr4_clr;
	uc_reg_write( uc, UC_X86_REG_CR0, &cr0 );
	uc_reg_write( uc, UC_X86_REG_CR4, &cr4 );
	uc_reg_write( uc, UC_X86_REG_RCX, &rcx );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_err e = uc_emu_start( uc, 0x1000, 0x1000 + code.size(), 0, 1 );
	uc_close( uc );
	if ( !log.seen.empty() ) return int( log.seen[ 0 ] );
	if ( e == UC_ERR_INSN_INVALID ) return 6;
	return e ? -2 : -1;
}

static void test_r15()
{
	std::printf( "R15 CR0.TS/EM, CR4.OSFXSR and CVTPI2PS m64 #MF (manual default): Unicorn vs SDM\n" );
	const uint64_t TS = 0x8, EM = 0x4, MP = 0x2, OSFXSR = 1ull << 9;
	struct c { const char* text; uint64_t set, clr, cr4clr; bool mf; uint32_t quirks; int expect; const char* why; };
	const c cases[] = {
		{ "fadd st(0), st(1)", TS, 0, 0, false, 0, 7, "x87, CR0.TS" },
		{ "fadd st(0), st(1)", EM, 0, 0, false, 0, 7, "x87, CR0.EM" },
		{ "paddb mm0, mm1", TS, 0, 0, false, 0, 7, "MMX, CR0.TS" },
		{ "paddb mm0, mm1", EM, 0, 0, false, 0, 6, "MMX, CR0.EM" },
		{ "emms", TS, 0, 0, false, 0, 7, "EMMS, CR0.TS" },
		{ "emms", EM, 0, 0, false, 0, 6, "EMMS, CR0.EM" },
		{ "emms", 0, 0, OSFXSR, false, 0, -1, "EMMS, OSFXSR=0 runs" },
		{ "cvtpi2ps xmm0, mm1", TS, 0, 0, false, 0, 7, "CVTPI2PS, CR0.TS" },
		{ "cvtpi2ps xmm0, mm1", EM, 0, 0, false, 0, 6, "CVTPI2PS, CR0.EM" },
		{ "cvtpi2ps xmm0, mm1", 0, 0, OSFXSR, false, 0, 6, "CVTPI2PS, OSFXSR=0" },
		{ "cvtps2pi mm0, xmm1", TS, 0, 0, false, 0, 7, "CVTPS2PI, CR0.TS" },
		{ "cvttpd2pi mm0, xmm1", EM, 0, 0, false, 0, 6, "CVTTPD2PI, CR0.EM" },
		{ "movq2dq xmm0, mm1", TS, 0, 0, false, 0, 7, "MOVQ2DQ, CR0.TS" },
		{ "movdq2q mm0, xmm1", 0, 0, OSFXSR, false, 0, 6, "MOVDQ2Q, OSFXSR=0" },
		{ "fwait", TS | MP, 0, 0, false, 0, 7, "WAIT, CR0.MP+TS" },
		{ "fwait", TS, 0, 0, false, 0, -1, "WAIT, CR0.TS only runs" },
		{ "cvtpi2ps xmm0, qword ptr [rcx]", 0, 0, 0, true, 0, 16, "CVTPI2PS m64, pending, manual: #MF" },
		{ "cvtpi2ps xmm0, qword ptr [rcx]", 0, 0, 0, true, 2, -1, "CVTPI2PS m64, pending, hw quirk: runs" },
		{ "cvtpi2pd xmm0, qword ptr [rcx]", 0, 0, 0, true, 0, -1, "CVTPI2PD m64, pending: runs (SDM)" },
	};
	for ( const c& k : cases )
	{
		int v = r15_run( k.text, k.set, k.clr, k.cr4clr, k.mf, k.quirks );
		auto nm = [ & ]( int x ) { return x == -1 ? std::string( "runs" ) : x == 6 ? std::string( "#UD" ) : x == 7 ? std::string( "#NM" ) :
										  x == 16 ? std::string( "#MF" ) : "v" + std::to_string( x ); };
		std::printf( "    %-36s %-44s SDM %-5s uc %-5s %s\n", k.text, k.why, nm( k.expect ).c_str(), nm( v ).c_str(), v == k.expect ? "ok" : "WRONG" );
		CHECK( v == k.expect, "%s (%s): got %d, SDM %d", k.text, k.why, v, k.expect );
	}
}

// ── R16: branches to a non-canonical target (plan 1.11) ─────────────────────────────────────────
//
// SDM Vol2 JMP/CALL/RET: "IF tempRIP is not canonical THEN #GP(0)" before the push / RIP update, so
// the fault is on the branch (RIP = branch, RSP unchanged). Native forms keep RSP valid (leaf unwind);
// RET and direct branches across the canonical boundary are Unicorn-only (SDM expectation).

struct r16_native_result { bool faulted; DWORD code; ULONG_PTR info0, info1; int64_t rip_off, rsp_delta; };

static r16_native_result r16_native( const std::vector<uint8_t>& code, uint64_t rcx )
{
	r16_native_result r{ false, 0, 0, 0, -1, 0 };
	void* mem = VirtualAlloc( nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
	if ( !mem ) return r;
	std::memcpy( mem, code.data(), code.size() );
	FlushInstructionCache( GetCurrentProcess(), mem, code.size() );
	EXCEPTION_RECORD rec{};
	CONTEXT ctx{};
	uint64_t rsp_at_entry = 0;
	__try
	{
		// rdx receives RSP at thunk entry (first instruction: mov [rdx], rsp)
		( ( void( * )( uint64_t, uint64_t* ) ) mem )( rcx, &rsp_at_entry );
	}
	__except ( rec = *GetExceptionInformation()->ExceptionRecord, ctx = *GetExceptionInformation()->ContextRecord, EXCEPTION_EXECUTE_HANDLER )
	{
		r.faulted = true;
		r.code = rec.ExceptionCode;
		r.info0 = rec.NumberParameters > 0 ? rec.ExceptionInformation[ 0 ] : 0;
		r.info1 = rec.NumberParameters > 1 ? rec.ExceptionInformation[ 1 ] : 0;
		r.rip_off = int64_t( ctx.Rip ) - int64_t( uintptr_t( mem ) );
		r.rsp_delta = int64_t( ctx.Rsp ) - int64_t( rsp_at_entry );
	}
	VirtualFree( mem, 0, MEM_RELEASE );
	return r;
}

static int r16_unicorn( const std::vector<uint8_t>& code, uint64_t base, uint64_t rcx, uint64_t stack_value,
						int64_t& rip_off, int64_t& rsp_delta )
{
	uc_engine* uc = nullptr;
	uc_open( UC_ARCH_X86, UC_MODE_64, &uc );
	uc_ctl_set_cpu_model( uc, UC_CPU_X86_MAX );
	uc_mem_map( uc, base, 0x1000, UC_PROT_ALL );
	uc_mem_map( uc, 0x100000, 0x10000, UC_PROT_READ | UC_PROT_WRITE );
	uc_mem_write( uc, base, code.data(), code.size() );
	uint64_t rsp = 0x108000, rdx = 0x10F000;
	uc_mem_write( uc, rsp, &stack_value, 8 );
	uc_mem_write( uc, 0x101000, &stack_value, 8 );     // [rcx] for jmp qword ptr [rcx]
	uc_reg_write( uc, UC_X86_REG_RSP, &rsp ); uc_reg_write( uc, UC_X86_REG_RCX, &rcx ); uc_reg_write( uc, UC_X86_REG_RDX, &rdx );
	intr_log log;
	uc_hook h;
	uc_hook_add( uc, &h, UC_HOOK_INTR, ( void* ) on_intr, &log, 1, 0 );
	uc_emu_start( uc, base, base + code.size(), 0, 8 );
	uint64_t rip = 0, rsp_now = 0;
	uc_reg_read( uc, UC_X86_REG_RIP, &rip );
	uc_reg_read( uc, UC_X86_REG_RSP, &rsp_now );
	uc_close( uc );
	rip_off = int64_t( rip - base );
	rsp_delta = int64_t( rsp_now - rsp );
	return log.seen.empty() ? -1 : int( log.seen[ 0 ] );
}

static void test_r16()
{
	std::printf( "R16 branch to a non-canonical target: #GP(0) on the branch, nothing pushed/popped\n" );
	const uint64_t bad = 0x0000800000000000ull;
	struct n { const char* what; const char* text; uint64_t rcx; };
	const n natives[] = {
		{ "jmp rcx", "mov [rdx], rsp\njmp rcx\n", bad },
		{ "call rcx", "mov [rdx], rsp\ncall rcx\n", bad },
		{ "jmp qword ptr [rcx]", "mov [rdx], rsp\nmov rax, 0x0000800000000000\nmov [rsp - 16], rax\nlea rcx, [rsp - 16]\njmp qword ptr [rcx]\n", 0 },
	};
	for ( const n& k : natives )
	{
		std::vector<uint8_t> code = assemble( k.text );
		CHECK( !code.empty(), "%s: assembly", k.what );
		if ( code.empty() ) continue;
		r16_native_result hw = r16_native( code, k.rcx );
		int64_t rip_off = 0, rsp_delta = 0;
		uint64_t ucrcx = std::strstr( k.text, "lea rcx" ) ? 0 : k.rcx;
		int v = r16_unicorn( code, 0x1000, ucrcx, bad, rip_off, rsp_delta );
		// the branch is the last instruction: its offset = code size - its length
		std::printf( "    %-22s hw: %s code=%08lX info=%llX/%llX rip+%lld rsp%+lld   uc: vector %d rip+%lld rsp%+lld\n", k.what,
					 hw.faulted ? "fault" : "ok   ", hw.code, ( unsigned long long ) hw.info0, ( unsigned long long ) hw.info1,
					 ( long long ) hw.rip_off, ( long long ) hw.rsp_delta, v, ( long long ) rip_off, ( long long ) rsp_delta );
		CHECK( hw.faulted, "%s: hardware did not fault", k.what );
		CHECK( v == 13, "%s: Unicorn vector %d, SDM #GP(0)", k.what, v );
		CHECK( rip_off == hw.rip_off, "%s: fault RIP offset uc %lld hw %lld", k.what, ( long long ) rip_off, ( long long ) hw.rip_off );
		CHECK( rsp_delta == hw.rsp_delta, "%s: RSP delta uc %lld hw %lld", k.what, ( long long ) rsp_delta, ( long long ) hw.rsp_delta );
	}
	std::printf( "    -- Unicorn vs SDM --\n" );
	struct u { const char* what; const char* text; uint64_t base; int64_t expect_rip; int64_t expect_rsp; };
	const uint64_t top = 0x00007FFFFFFFF000ull;
	const u ucases[] = {
		{ "ret ([rsp] = 0x800000000000)", "ret\n", 0x1000, 0, 0 },
		{ "ret 16", "ret 16\n", 0x1000, 0, 0 },
		{ "push rcx; ret", "push rcx\nret\n", 0x1000, 1, -8 },
		{ "jmp rel32 across 2^47", "jmp 0x00007FFFFFFFF000 + 0x2000\n", top, 0, 0 },
		{ "call rel32 across 2^47", "call 0x00007FFFFFFFF000 + 0x2000\n", top, 0, 0 },
		{ "jz rel32 taken across 2^47", "xor eax, eax\njz 0x00007FFFFFFFF000 + 0x2000\n", top, 2, 0 },
		{ "jnz rel32 not taken", "xor eax, eax\njnz 0x00007FFFFFFFF000 + 0x2000\nnop\n", top, -1, 0 },
	};
	for ( const u& k : ucases )
	{
		ks_engine* ks = nullptr;
		ks_open( KS_ARCH_X86, KS_MODE_64, &ks );
		unsigned char* enc = nullptr;
		size_t size = 0, count = 0;
		std::vector<uint8_t> code;
		if ( ks_asm( ks, k.text, k.base, &enc, &size, &count ) == KS_ERR_OK ) code.assign( enc, enc + size );
		if ( enc ) ks_free( enc );
		ks_close( ks );
		CHECK( !code.empty(), "%s: assembly", k.what );
		if ( code.empty() ) continue;
		int64_t rip_off = 0, rsp_delta = 0;
		int v = r16_unicorn( code, k.base, bad, bad, rip_off, rsp_delta );
		bool expect_fault = k.expect_rip >= 0;
		std::printf( "    %-30s uc: vector %-3d rip+%lld rsp%+lld   SDM: %s\n", k.what, v, ( long long ) rip_off, ( long long ) rsp_delta,
					 expect_fault ? "#GP(0) on the branch" : "falls through" );
		if ( expect_fault )
		{
			CHECK( v == 13, "%s: vector %d", k.what, v );
			CHECK( rip_off == k.expect_rip, "%s: RIP offset %lld, expected %lld", k.what, ( long long ) rip_off, ( long long ) k.expect_rip );
			CHECK( rsp_delta == k.expect_rsp, "%s: RSP delta %lld, expected %lld", k.what, ( long long ) rsp_delta, ( long long ) k.expect_rsp );
		}
		else
			CHECK( v < 0, "%s: faulted (vector %d)", k.what, v );
	}
}

// ── x87 transcendental ground truth (plan 1.12) ─────────────────────────────────────────────────
//
// `emu-uc72-risk --x87-dump <csv> [--uc]`: runs F2XM1, FYL2X, FYL2XP1, FPTAN, FPATAN, FSIN, FCOS and
// FSINCOS on a structured + random operand set in all four rounding modes, natively (self-generated
// thunk only) or with --uc in Unicorn, and writes one CSV row per case:
//   op,fcw,st0_in,st1_in,fsw,st0_out,st1_out      (80-bit values as SEXP:MANTISSA hex)
// Uses the R11 thunk: 8 x FLD m80 then FLDENV, so ST0 = slot 7 and ST1 = slot 6 of the input.

static void x87_dump( const char* path, bool use_uc )
{
	FILE* f = nullptr;
	if ( fopen_s( &f, path, "w" ) || !f ) { std::printf( "cannot open %s\n", path ); return; }
	std::fprintf( f, "op,fcw,st0_in,st1_in,fsw,st0_out,st1_out\n" );
	const char* ops[] = { "f2xm1", "fyl2x", "fyl2xp1", "fptan", "fpatan", "fsin", "fcos", "fsincos" };
	rng r{ 0x5EED5EED12345678ull };
	auto put = [ & ]( uint8_t* p, uint16_t se, uint64_t m ) { std::memcpy( p, &m, 8 ); std::memcpy( p + 8, &se, 2 ); };
	auto rnd_mant = [ & ]() { return ( 1ull << 63 ) | ( uint64_t( r.next() ) << 32 ) | r.next(); };
	long rows = 0;
	for ( int oi = 0; oi < int( std::size( ops ) ); ++oi )
	{
		const char* op = ops[ oi ];
		std::vector<uint8_t> code = assemble( r11_thunk( op ) );
		if ( code.empty() ) { std::printf( "assembly failed: %s\n", op ); continue; }
		for ( int k = 0; k < 4000; ++k )
		{
			// operand choice per function (exponents unbiased; 0x3FFF = 2^0)
			uint16_t s0 = r.below( 2 ) ? 0x8000 : 0, s1 = r.below( 2 ) ? 0x8000 : 0;
			int e0 = 0, e1 = 0;
			uint64_t m0 = rnd_mant(), m1 = rnd_mant();
			switch ( oi )
			{
				case 0: e0 = -int( r.below( 70 ) ) - 1; if ( k % 8 == 0 ) e0 = 0, m0 = ( 1ull << 63 ) | ( m0 >> 10 ); break;   // |x| < 1
				case 1: s0 = 0; e0 = int( r.below( 120 ) ) - 60; e1 = int( r.below( 40 ) ) - 20; break;                      // x > 0
				case 2: e0 = -int( r.below( 66 ) ) - 2; e1 = int( r.below( 40 ) ) - 20; break;                               // |x| < 0.25
				case 3: case 5: case 6: case 7: e0 = int( r.below( 90 ) ) - 66; if ( k % 5 == 0 ) e0 = int( r.below( 4 ) ); break;
				case 4: e0 = int( r.below( 80 ) ) - 40; e1 = int( r.below( 80 ) ) - 40; break;                               // y = ST1, x = ST0
			}
			uint8_t in[ 208 ] = {};
			for ( int i = 0; i < 8; ++i ) put( in + 32 + i * 16, 0x3FFF, 1ull << 63 );
			put( in + 32 + 7 * 16, uint16_t( s0 | ( 0x3FFF + e0 ) ), m0 );     // ST0
			put( in + 32 + 6 * 16, uint16_t( s1 | ( 0x3FFF + e1 ) ), m1 );     // ST1
			for ( uint16_t rc = 0; rc < 4; ++rc )
			{
				// ST(7) empty so FPTAN/FSINCOS can push; all exceptions masked; PC = 64 bits
				uint16_t fcw = uint16_t( 0x037F | ( rc << 10 ) ), fsw = 0, ftw = 0xC000;
				std::memcpy( in + 0, &fcw, 2 ); std::memcpy( in + 4, &fsw, 2 ); std::memcpy( in + 8, &ftw, 2 );
				r11_result res = use_uc ? r11_unicorn( code, in, X87_HW_QUIRKS )
										: r11_native( code, in );
				if ( res.fault >= 0 ) continue;
				uint16_t fsw_out = uint16_t( res.env[ 4 ] | ( res.env[ 5 ] << 8 ) );
				int top = ( fsw_out >> 11 ) & 7;
				// FXSAVE stores ST(i) in stack order at +32 + 16*i
				uint64_t o0, o1; uint16_t x0, x1;
				std::memcpy( &o0, res.fx + 32, 8 ); std::memcpy( &x0, res.fx + 40, 2 );
				std::memcpy( &o1, res.fx + 48, 8 ); std::memcpy( &x1, res.fx + 56, 2 );
				( void ) top;
				std::fprintf( f, "%s,%04X,%04X:%016llX,%04X:%016llX,%04X,%04X:%016llX,%04X:%016llX\n", op, fcw, unsigned( s0 | ( 0x3FFF + e0 ) ),
							  ( unsigned long long ) m0, unsigned( s1 | ( 0x3FFF + e1 ) ), ( unsigned long long ) m1, fsw_out, x0,
							  ( unsigned long long ) o0, x1, ( unsigned long long ) o1 );
				++rows;
			}
		}
	}
	std::fclose( f );
	std::printf( "x87-dump: %ld rows -> %s (%s)\n", rows, path, use_uc ? "Unicorn" : "hardware" );
}

// `emu-uc72-risk --x87-probe <op> <st0 SEXP:MANT> <st1 SEXP:MANT> <step> <count> <csv>`: ST0 = st0 + k*step
// (mantissa units), k = 0..count-1, all four rounding modes, natively; same CSV format as --x87-dump.
static void x87_probe( const char* op, const char* st0, const char* st1, long long step, long count, const char* path )
{
	FILE* f = nullptr;
	if ( fopen_s( &f, path, "w" ) || !f ) { std::printf( "cannot open %s\n", path ); return; }
	std::fprintf( f, "op,fcw,st0_in,st1_in,fsw,st0_out,st1_out\n" );
	std::vector<uint8_t> code = assemble( r11_thunk( op ) );
	if ( code.empty() ) { std::printf( "assembly failed: %s\n", op ); std::fclose( f ); return; }
	unsigned se0 = 0, se1 = 0;
	unsigned long long m0 = 0, m1 = 0;
	std::sscanf( st0, "%x:%llx", &se0, &m0 );
	std::sscanf( st1, "%x:%llx", &se1, &m1 );
	auto put = [ & ]( uint8_t* p, uint16_t se, uint64_t m ) { std::memcpy( p, &m, 8 ); std::memcpy( p + 8, &se, 2 ); };
	for ( long k = 0; k < count; ++k )
	{
		uint64_t mk = m0 + uint64_t( k ) * uint64_t( step );
		uint8_t in[ 208 ] = {};
		for ( int i = 0; i < 8; ++i ) put( in + 32 + i * 16, 0x3FFF, 1ull << 63 );
		put( in + 32 + 7 * 16, uint16_t( se0 ), mk );
		put( in + 32 + 6 * 16, uint16_t( se1 ), m1 );
		for ( uint16_t rc = 0; rc < 4; ++rc )
		{
			uint16_t fcw = uint16_t( 0x037F | ( rc << 10 ) ), fsw = 0, ftw = 0xC000;
			std::memcpy( in + 0, &fcw, 2 ); std::memcpy( in + 4, &fsw, 2 ); std::memcpy( in + 8, &ftw, 2 );
			r11_result res = r11_native( code, in );
			if ( res.fault >= 0 ) continue;
			uint64_t o0, o1; uint16_t x0, x1;
			std::memcpy( &o0, res.fx + 32, 8 ); std::memcpy( &x0, res.fx + 40, 2 );
			std::memcpy( &o1, res.fx + 48, 8 ); std::memcpy( &x1, res.fx + 56, 2 );
			std::fprintf( f, "%s,%04X,%04X:%016llX,%04X:%016llX,%04X,%04X:%016llX,%04X:%016llX\n", op, fcw, se0, ( unsigned long long ) mk,
						  se1, ( unsigned long long ) m1, unsigned( res.env[ 4 ] | ( res.env[ 5 ] << 8 ) ), x0, ( unsigned long long ) o0, x1,
						  ( unsigned long long ) o1 );
		}
	}
	std::fclose( f );
	std::printf( "x87-probe: %ld points -> %s\n", count, path );
}

// `emu-uc72-risk --x87-special <csv> [--uc]`: every transcendental over special / edge operands (both operand
// slots for the two-operand forms), all four rounding modes, IM masked and unmasked. Same CSV format.
static void x87_special( const char* path, bool use_uc )
{
	FILE* f = nullptr;
	if ( fopen_s( &f, path, "w" ) || !f ) { std::printf( "cannot open %s\n", path ); return; }
	std::fprintf( f, "op,fcw,st0_in,st1_in,fsw,st0_out,st1_out\n" );
	struct v80 { uint16_t se; uint64_t m; };
	const v80 vals[] = {
		{ 0x0000, 0 }, { 0x8000, 0 },                                        // +-0
		{ 0x0000, 0x0000000000000001ull }, { 0x8000, 0x4000000000000000ull }, // denormals
		{ 0x0001, 0x8000000000000000ull },                                   // smallest normal
		{ 0x3FBF, 0x8000000000000000ull }, { 0xBFC0, 0xC000000000000000ull }, // tiny 2^-64, -1.5*2^-63
		{ 0x3FFE, 0x8000000000000000ull }, { 0xBFFE, 0x8000000000000000ull }, // +-0.5
		{ 0x3FFF, 0x8000000000000000ull }, { 0xBFFF, 0x8000000000000000ull }, // +-1
		{ 0x3FFE, 0xFFFFFFFFFFFFFFFFull }, { 0x3FFF, 0x8000000000000001ull }, // 1-ulp, 1+ulp
		{ 0x3FFF, 0xC000000000000000ull }, { 0xBFFF, 0xC000000000000000ull }, // +-1.5
		{ 0x3FFD, 0x95F619980C4336F7ull }, { 0xBFFD, 0x95F619980C4336F7ull }, // +-(1 - sqrt(2)/2)
		{ 0x3FFD, 0x95F619980C4336F8ull }, { 0xBFFD, 0x95F619980C4336F8ull }, // just beyond
		{ 0x4000, 0xC90FDAA22168C235ull }, { 0x3FFF, 0xC90FDAA22168C235ull }, // ~pi, ~pi/2
		{ 0x403D, 0xFFFFFFFFFFFFFFFFull }, { 0x403E, 0x8000000000000000ull }, // 2^63 - 1 ulp, 2^63
		{ 0xC03E, 0x8000000000000000ull }, { 0x403F, 0x8000000000000000ull }, // -2^63, 2^64
		{ 0x7FFE, 0xFFFFFFFFFFFFFFFFull },                                   // max
		{ 0x7FFF, 0x8000000000000000ull }, { 0xFFFF, 0x8000000000000000ull }, // +-inf
		{ 0x7FFF, 0xC000000000000000ull }, { 0x7FFF, 0xA000000000000000ull }, // QNaN, SNaN
		{ 0x4002, 0xA000000000000000ull }, { 0x3FFB, 0xCCCCCCCCCCCCCCCDull }, // 10, 0.1
	};
	const char* ops[] = { "f2xm1", "fyl2x", "fyl2xp1", "fptan", "fpatan", "fsin", "fcos", "fsincos" };
	auto put = [ & ]( uint8_t* p, uint16_t se, uint64_t m ) { std::memcpy( p, &m, 8 ); std::memcpy( p + 8, &se, 2 ); };
	long rows = 0;
	for ( const char* op : ops )
	{
		bool two = !std::strcmp( op, "fyl2x" ) || !std::strcmp( op, "fyl2xp1" ) || !std::strcmp( op, "fpatan" );
		std::vector<uint8_t> code = assemble( r11_thunk( op ) );
		if ( code.empty() ) continue;
		for ( const v80& a : vals )
			for ( size_t j = 0; j < ( two ? std::size( vals ) : 1 ); ++j )
			{
				const v80 b = two ? vals[ j ] : v80{ 0x3FFF, 0x8000000000000000ull };
				uint8_t in[ 208 ] = {};
				for ( int i = 0; i < 8; ++i ) put( in + 32 + i * 16, 0x3FFF, 1ull << 63 );
				put( in + 32 + 7 * 16, a.se, a.m );
				put( in + 32 + 6 * 16, b.se, b.m );
				for ( uint16_t im = 0; im < 2; ++im )
					for ( uint16_t rc = 0; rc < 4; ++rc )
					{
						uint16_t fcw = uint16_t( ( im ? 0x037F : 0x037E ) | ( rc << 10 ) ), fsw = 0, ftw = 0xC000;
						std::memcpy( in + 0, &fcw, 2 ); std::memcpy( in + 4, &fsw, 2 ); std::memcpy( in + 8, &ftw, 2 );
						r11_result res = use_uc ? r11_unicorn( code, in, X87_HW_QUIRKS )
												: r11_native( code, in );
						if ( res.fault >= 0 ) continue;
						uint64_t o0, o1; uint16_t x0, x1;
						std::memcpy( &o0, res.fx + 32, 8 ); std::memcpy( &x0, res.fx + 40, 2 );
						std::memcpy( &o1, res.fx + 48, 8 ); std::memcpy( &x1, res.fx + 56, 2 );
						std::fprintf( f, "%s,%04X,%04X:%016llX,%04X:%016llX,%04X,%04X:%016llX,%04X:%016llX\n", op, fcw, a.se, ( unsigned long long ) a.m,
									  b.se, ( unsigned long long ) b.m, unsigned( res.env[ 4 ] | ( res.env[ 5 ] << 8 ) ), x0, ( unsigned long long ) o0, x1,
									  ( unsigned long long ) o1 );
						++rows;
					}
			}
	}
	std::fclose( f );
	std::printf( "x87-special: %ld rows -> %s (%s)\n", rows, path, use_uc ? "Unicorn" : "hardware" );
}

// `emu-uc72-risk --x87-list <in.txt> <out.csv> [--uc]`: one case per input line "op fcw st0 st1"
// (SEXP:MANT hex), natively or in Unicorn; same CSV format as --x87-dump.
static void x87_list( const char* inpath, const char* path, bool use_uc )
{
	FILE* in = nullptr;
	FILE* f = nullptr;
	if ( fopen_s( &in, inpath, "r" ) || !in ) { std::printf( "cannot open %s\n", inpath ); return; }
	if ( fopen_s( &f, path, "w" ) || !f ) { std::printf( "cannot open %s\n", path ); std::fclose( in ); return; }
	std::fprintf( f, "op,fcw,st0_in,st1_in,fsw,st0_out,st1_out\n" );
	auto put = [ & ]( uint8_t* p, uint16_t se, uint64_t m ) { std::memcpy( p, &m, 8 ); std::memcpy( p + 8, &se, 2 ); };
	std::string cur_op;
	std::vector<uint8_t> code;
	char op[ 32 ];
	unsigned fcw = 0, se0 = 0, se1 = 0;
	unsigned long long m0 = 0, m1 = 0;
	long rows = 0;
	while ( std::fscanf( in, "%31s %x %x:%llx %x:%llx", op, &fcw, &se0, &m0, &se1, &m1 ) == 6 )
	{
		if ( cur_op != op ) { cur_op = op; code = assemble( r11_thunk( op ) ); }
		if ( code.empty() ) continue;
		uint8_t buf[ 208 ] = {};
		for ( int i = 0; i < 8; ++i ) put( buf + 32 + i * 16, 0x3FFF, 1ull << 63 );
		put( buf + 32 + 7 * 16, uint16_t( se0 ), m0 );
		put( buf + 32 + 6 * 16, uint16_t( se1 ), m1 );
		uint16_t fc = uint16_t( fcw ), fsw = 0, ftw = 0xC000;
		std::memcpy( buf + 0, &fc, 2 ); std::memcpy( buf + 4, &fsw, 2 ); std::memcpy( buf + 8, &ftw, 2 );
		r11_result res = use_uc ? r11_unicorn( code, buf, X87_HW_QUIRKS ) : r11_native( code, buf );
		if ( res.fault >= 0 ) continue;
		uint64_t o0, o1; uint16_t x0, x1;
		std::memcpy( &o0, res.fx + 32, 8 ); std::memcpy( &x0, res.fx + 40, 2 );
		std::memcpy( &o1, res.fx + 48, 8 ); std::memcpy( &x1, res.fx + 56, 2 );
		std::fprintf( f, "%s,%04X,%04X:%016llX,%04X:%016llX,%04X,%04X:%016llX,%04X:%016llX\n", op, fcw, se0, m0, se1, m1,
					  unsigned( res.env[ 4 ] | ( res.env[ 5 ] << 8 ) ), x0, ( unsigned long long ) o0, x1, ( unsigned long long ) o1 );
		++rows;
	}
	std::fclose( in );
	std::fclose( f );
	std::printf( "x87-list: %ld rows -> %s (%s)\n", rows, path, use_uc ? "Unicorn" : "hardware" );
}

// `emu-uc72-risk --x87-list2 <in.tsv> <out.csv> [--uc]`: one case per line, tab separated:
//   instruction text, fcw, fsw, st0, st1, memory operand (16 bytes as 32 hex digits, byte 0 first)
// The memory operand sits at [rcx + 192] (m16..m80). Output: the inputs, then fsw, ftw, st0, st1,
// the memory operand after the instruction and AH (LAHF: SF ZF AF PF CF) - FST/FIST/FBSTP, FCOMI.
static void x87_list2( const char* inpath, const char* path, bool use_uc )
{
	FILE* in = nullptr;
	FILE* f = nullptr;
	if ( fopen_s( &in, inpath, "r" ) || !in ) { std::printf( "cannot open %s\n", inpath ); return; }
	if ( fopen_s( &f, path, "w" ) || !f ) { std::printf( "cannot open %s\n", path ); std::fclose( in ); return; }
	std::fprintf( f, "op,fcw,fsw_in,st0_in,st1_in,mem_in,fsw,ftw,st0_out,st1_out,mem_out,ah\n" );
	auto put = [ & ]( uint8_t* p, uint16_t se, uint64_t m ) { std::memcpy( p, &m, 8 ); std::memcpy( p + 8, &se, 2 ); };
	std::string cur_op;
	std::vector<uint8_t> code;
	char line[ 512 ];
	long rows = 0;
	while ( std::fgets( line, sizeof( line ), in ) )
	{
		char* ctx = nullptr;
		const char* fld[ 6 ] = {};
		int n = 0;
		for ( char* tok = strtok_s( line, "\t\r\n", &ctx ); tok && n < 6; tok = strtok_s( nullptr, "\t\r\n", &ctx ) ) fld[ n++ ] = tok;
		if ( n != 6 ) continue;
		unsigned fcw = 0, fsw = 0, se0 = 0, se1 = 0;
		unsigned long long m0 = 0, m1 = 0;
		std::sscanf( fld[ 1 ], "%x", &fcw );
		std::sscanf( fld[ 2 ], "%x", &fsw );
		std::sscanf( fld[ 3 ], "%x:%llx", &se0, &m0 );
		std::sscanf( fld[ 4 ], "%x:%llx", &se1, &m1 );
		if ( cur_op != fld[ 0 ] ) { cur_op = fld[ 0 ]; code = assemble( r11_thunk( fld[ 0 ] ) ); }
		if ( code.empty() ) { std::printf( "assembly failed: %s\n", fld[ 0 ] ); continue; }
		uint8_t buf[ 208 ] = {};
		for ( int i = 0; i < 8; ++i ) put( buf + 32 + i * 16, 0x3FFF, 1ull << 63 );
		put( buf + 32 + 7 * 16, uint16_t( se0 ), m0 );
		put( buf + 32 + 6 * 16, uint16_t( se1 ), m1 );
		for ( int i = 0; i < 16 && fld[ 5 ][ 2 * i ] && fld[ 5 ][ 2 * i + 1 ]; ++i )
		{
			unsigned v = 0;
			std::sscanf( fld[ 5 ] + 2 * i, "%2x", &v );
			buf[ 192 + i ] = uint8_t( v );
		}
		uint16_t fc = uint16_t( fcw ), fs = uint16_t( fsw ), ftw = 0xC000;     // ST(7) empty: pushes possible
		std::memcpy( buf + 0, &fc, 2 ); std::memcpy( buf + 4, &fs, 2 ); std::memcpy( buf + 8, &ftw, 2 );
		r11_result res = use_uc ? r11_unicorn( code, buf, X87_HW_QUIRKS ) : r11_native( code, buf );
		if ( res.fault >= 0 )
		{
			std::fprintf( f, "%s,%04X,%04X,%04X:%016llX,%04X:%016llX,%s,fault %d,,,,,\n", fld[ 0 ], fcw, fsw, se0, m0, se1, m1, fld[ 5 ], res.fault );
			++rows;
			continue;
		}
		uint64_t o0, o1; uint16_t x0, x1;
		std::memcpy( &o0, res.fx + 32, 8 ); std::memcpy( &x0, res.fx + 40, 2 );
		std::memcpy( &o1, res.fx + 48, 8 ); std::memcpy( &x1, res.fx + 56, 2 );
		char mem[ 33 ] = {};
		for ( int i = 0; i < 16; ++i ) std::snprintf( mem + 2 * i, 3, "%02X", res.mem[ i ] );
		std::fprintf( f, "%s,%04X,%04X,%04X:%016llX,%04X:%016llX,%s,%04X,%04X,%04X:%016llX,%04X:%016llX,%s,%02X\n", fld[ 0 ], fcw, fsw, se0, m0,
					  se1, m1, fld[ 5 ], unsigned( res.env[ 4 ] | ( res.env[ 5 ] << 8 ) ), unsigned( res.env[ 8 ] | ( res.env[ 9 ] << 8 ) ), x0,
					  ( unsigned long long ) o0, x1, ( unsigned long long ) o1, mem, res.ah );
		++rows;
	}
	std::fclose( in );
	std::fclose( f );
	std::printf( "x87-list2: %ld rows -> %s (%s)\n", rows, path, use_uc ? "Unicorn" : "hardware" );
}

// ── R17: x87 transcendentals vs hardware (plan 1.12) ─────────────────────────────────────────
//
// F2XM1, FYL2X, FYL2XP1, FPTAN, FPATAN, FSIN, FCOS and FSINCOS over three operand sets, natively
// (the R11 thunk) and in Unicorn with the hardware quirks:
//   special  edge operands (zeros, denormals, a pseudo-denormal, tiny, +-1, the FYL2XP1 domain
//            border, ~pi, 2^63, 2^64, +-max, infinities, NaNs) in both operand slots; IM masked and
//            unmasked x 4 RC, then DE/ZE/OE/UE/PE unmasked one at a time (OE/UE/PE in every RC) and
//            everything unmasked;
//   random   1000 operands per function (the --x87-dump distribution) x 4 RC;
//   sweep    every 97th exponent, both signs, significands 1.0 / all-ones / ~pi, x 4 RC.
// Required: bit-exact, the FSW (C1 included) and both results (ledger U56: the microcode models).

struct x87_case { const char* op; uint16_t fcw, se0, se1; uint64_t m0, m1; };

static const char* const X87_TRANS_OPS[] = { "f2xm1", "fyl2x", "fyl2xp1", "fptan", "fpatan", "fsin", "fcos", "fsincos" };

static bool x87_two_operand( const char* op )
{
	return !std::strcmp( op, "fyl2x" ) || !std::strcmp( op, "fyl2xp1" ) || !std::strcmp( op, "fpatan" );
}

static std::vector<x87_case> x87_special_cases()
{
	struct v80 { uint16_t se; uint64_t m; };
	const v80 vals[] = {
		{ 0x0000, 0 }, { 0x8000, 0 }, { 0x0000, 0x0000000000000001ull }, { 0x8000, 0x4000000000000000ull },
		{ 0x0000, 0x8000000000000000ull }, { 0x0001, 0x8000000000000000ull }, { 0x3FBF, 0x8000000000000000ull },
		{ 0x3FC0, 0x8000000000000000ull }, { 0xBFC0, 0xC000000000000000ull }, { 0x3FFE, 0x8000000000000000ull },
		{ 0xBFFE, 0x8000000000000000ull }, { 0x3FFF, 0x8000000000000000ull }, { 0xBFFF, 0x8000000000000000ull },
		{ 0x3FFE, 0xFFFFFFFFFFFFFFFFull }, { 0x3FFF, 0x8000000000000001ull }, { 0x3FFF, 0xC000000000000000ull },
		{ 0xBFFF, 0xC000000000000000ull }, { 0x3FFD, 0x95F619980C4336F7ull }, { 0xBFFD, 0x95F619980C4336F7ull },
		{ 0x3FFD, 0x95F619980C4336F8ull }, { 0xBFFD, 0x95F619980C4336F8ull }, { 0x4000, 0xC90FDAA22168C235ull },
		{ 0x3FFF, 0xC90FDAA22168C235ull }, { 0x403D, 0xFFFFFFFFFFFFFFFFull }, { 0x403E, 0x8000000000000000ull },
		{ 0xC03E, 0x8000000000000000ull }, { 0x403F, 0x8000000000000000ull }, { 0x7FFE, 0xFFFFFFFFFFFFFFFFull },
		{ 0xFFFE, 0xFFFFFFFFFFFFFFFFull }, { 0x7FFF, 0x8000000000000000ull }, { 0xFFFF, 0x8000000000000000ull },
		{ 0x7FFF, 0xC000000000000000ull }, { 0x7FFF, 0xA000000000000000ull }, { 0x4002, 0xA000000000000000ull },
		{ 0x3FFB, 0xCCCCCCCCCCCCCCCDull },
	};
	std::vector<uint16_t> fcws;
	for ( uint16_t rc = 0; rc < 4; ++rc ) fcws.push_back( uint16_t( 0x037E | ( rc << 10 ) ) ), fcws.push_back( uint16_t( 0x037F | ( rc << 10 ) ) );
	fcws.push_back( 0x037D ); fcws.push_back( 0x037B );
	for ( uint16_t rc = 0; rc < 4; ++rc )
		for ( uint16_t m : { 0x0377, 0x036F, 0x035F } ) fcws.push_back( uint16_t( m | ( rc << 10 ) ) );
	fcws.push_back( 0x0340 ); fcws.push_back( 0x0B40 );
	std::vector<x87_case> out;
	for ( const char* op : X87_TRANS_OPS )
		for ( const v80& a : vals )
			for ( size_t j = 0; j < ( x87_two_operand( op ) ? std::size( vals ) : 1 ); ++j )
			{
				const v80 b = x87_two_operand( op ) ? vals[ j ] : v80{ 0x3FFF, 0x8000000000000000ull };
				for ( uint16_t fcw : fcws ) out.push_back( { op, fcw, a.se, b.se, a.m, b.m } );
			}
	return out;
}

static std::vector<x87_case> x87_random_cases( int per_op, uint64_t seed )
{
	std::vector<x87_case> out;
	rng r{ seed };
	auto rnd_mant = [ & ]() { return ( 1ull << 63 ) | ( uint64_t( r.next() ) << 32 ) | r.next(); };
	for ( int oi = 0; oi < int( std::size( X87_TRANS_OPS ) ); ++oi )
		for ( int k = 0; k < per_op; ++k )
		{
			// operand choice per function (exponents unbiased; 0 = 2^0), as --x87-dump
			uint16_t s0 = r.below( 2 ) ? 0x8000 : 0, s1 = r.below( 2 ) ? 0x8000 : 0;
			int e0 = 0, e1 = 0;
			uint64_t m0 = rnd_mant(), m1 = rnd_mant();
			switch ( oi )
			{
				case 0: e0 = -int( r.below( 70 ) ) - 1; if ( k % 8 == 0 ) e0 = 0, m0 = ( 1ull << 63 ) | ( m0 >> 10 ); break;   // |x| < 1
				case 1: s0 = 0; e0 = int( r.below( 120 ) ) - 60; e1 = int( r.below( 40 ) ) - 20; break;                      // x > 0
				case 2: e0 = -int( r.below( 66 ) ) - 2; e1 = int( r.below( 40 ) ) - 20; break;                               // |x| < 0.25
				case 3: case 5: case 6: case 7: e0 = int( r.below( 90 ) ) - 66; if ( k % 5 == 0 ) e0 = int( r.below( 4 ) ); break;
				case 4: e0 = int( r.below( 80 ) ) - 40; e1 = int( r.below( 80 ) ) - 40; break;                               // y = ST1, x = ST0
			}
			for ( uint16_t rc = 0; rc < 4; ++rc )
				out.push_back( { X87_TRANS_OPS[ oi ], uint16_t( 0x037F | ( rc << 10 ) ), uint16_t( s0 | ( 0x3FFF + e0 ) ),
								 uint16_t( s1 | ( 0x3FFF + e1 ) ), m0, m1 } );
		}
	return out;
}

static std::vector<x87_case> x87_sweep_cases()
{
	std::vector<x87_case> out;
	const uint64_t mants[] = { 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull, 0xC90FDAA22168C235ull };
	rng r{ 0x5EEDC0DE87654321ull };
	for ( const char* op : X87_TRANS_OPS )
		for ( uint16_t e = 0x0001; e < 0x4040; e = uint16_t( e + 97 ) )
			for ( uint16_t sg : { uint16_t( 0 ), uint16_t( 0x8000 ) } )
				for ( uint64_t m : mants )
				{
					// the other operand of the two-operand forms: a fresh value in [1, 2) per point
					uint64_t pm = ( 1ull << 63 ) | ( uint64_t( r.next() ) << 32 ) | r.next();
					for ( uint16_t rc = 0; rc < 4; ++rc )
					{
						uint16_t fcw = uint16_t( 0x037F | ( rc << 10 ) );
						if ( !std::strcmp( op, "f2xm1" ) && e >= 0x3FFF ) continue;          // documented |x| < 1
						if ( !std::strcmp( op, "fyl2x" ) && sg ) continue;                    // x > 0
						if ( !std::strcmp( op, "fyl2xp1" ) && e >= 0x3FFD ) continue;        // documented |x| < 1 - sqrt(2)/2
						if ( x87_two_operand( op ) )
						{
							out.push_back( { op, fcw, uint16_t( sg | e ), 0x3FFF, m, pm } );     // x = ST0 swept
							out.push_back( { op, fcw, 0x3FFF, uint16_t( sg | e ), pm, m } );     // y = ST1 swept
						}
						else
							out.push_back( { op, fcw, uint16_t( sg | e ), 0x3FFF, m, 0x8000000000000000ull } );
					}
				}
	return out;
}

// |a - b| <= 1 ulp (same sign, finite), or bit-identical
static bool x87_within_1ulp( uint16_t a_se, uint64_t a_m, uint16_t b_se, uint64_t b_m )
{
	if ( a_se == b_se && a_m == b_m ) return true;
	if ( ( a_se ^ b_se ) & 0x8000 ) return false;
	int ea = a_se & 0x7FFF, eb = b_se & 0x7FFF;
	if ( ea == 0x7FFF || eb == 0x7FFF ) return false;
	if ( ea == 0 ) ea = 1;                                  // denormal: same scale as the smallest normal
	if ( eb == 0 ) eb = 1;
	if ( ea == eb ) return ( a_m > b_m ? a_m - b_m : b_m - a_m ) == 1;
	if ( ea + 1 == eb ) return a_m == ~0ull && b_m == 0x8000000000000000ull;
	if ( eb + 1 == ea ) return b_m == ~0ull && a_m == 0x8000000000000000ull;
	return false;
}

static void test_r17()
{
	std::printf( "R17 x87 transcendentals vs hardware: bit-exact (FSW incl. C1, results)\n" );
	struct set_t { const char* name; std::vector<x87_case> cases; double floor; };
	set_t sets[] = {
		{ "special", x87_special_cases(), 0.0 },
		{ "random", x87_random_cases( 1000, 0x5EED5EED12345678ull ), 0.0 },
		{ "sweep", x87_sweep_cases(), 0.0 },
	};
	auto put = [ & ]( uint8_t* p, uint16_t se, uint64_t m ) { std::memcpy( p, &m, 8 ); std::memcpy( p + 8, &se, 2 ); };
	for ( int si = 0; si < 3; ++si )
	{
		const set_t& st = sets[ si ];
		struct stat_t { long n = 0, exact = 0, fsw = 0, ulp = 0; } stat[ 8 ];
		std::string cur;
		std::vector<uint8_t> code;
		int oi = 0;
		for ( const x87_case& c : st.cases )
		{
			if ( cur != c.op )
			{
				cur = c.op;
				code = assemble( r11_thunk( c.op ) );
				CHECK( !code.empty(), "%s: assembly", c.op );
				for ( oi = 0; std::strcmp( X87_TRANS_OPS[ oi ], c.op ); ++oi ) {}
			}
			if ( code.empty() ) continue;
			uint8_t in[ 208 ] = {};
			for ( int i = 0; i < 8; ++i ) put( in + 32 + i * 16, 0x3FFF, 1ull << 63 );
			put( in + 32 + 7 * 16, c.se0, c.m0 );       // ST0
			put( in + 32 + 6 * 16, c.se1, c.m1 );       // ST1
			uint16_t fsw = 0, ftw = 0xC000;             // ST(7) empty so FPTAN/FSINCOS can push
			std::memcpy( in + 0, &c.fcw, 2 ); std::memcpy( in + 4, &fsw, 2 ); std::memcpy( in + 8, &ftw, 2 );
			r11_result hw = r11_native( code, in ), uc = r11_unicorn( code, in, X87_HW_QUIRKS );
			if ( hw.fault >= 0 || uc.fault >= 0 )
			{
				CHECK( hw.fault == uc.fault, "%s fcw=%04X x=%04X:%016llX y=%04X:%016llX: fault hw %d uc %d", c.op, c.fcw, c.se0,
					   ( unsigned long long ) c.m0, c.se1, ( unsigned long long ) c.m1, hw.fault, uc.fault );
				continue;
			}
			stat_t& s = stat[ oi ];
			++s.n;
			uint16_t fh = uint16_t( hw.env[ 4 ] | ( hw.env[ 5 ] << 8 ) ), fu = uint16_t( uc.env[ 4 ] | ( uc.env[ 5 ] << 8 ) );
			uint64_t h0, h1, u0, u1; uint16_t hx0, hx1, ux0, ux1;
			std::memcpy( &h0, hw.fx + 32, 8 ); std::memcpy( &hx0, hw.fx + 40, 2 ); std::memcpy( &h1, hw.fx + 48, 8 ); std::memcpy( &hx1, hw.fx + 56, 2 );
			std::memcpy( &u0, uc.fx + 32, 8 ); std::memcpy( &ux0, uc.fx + 40, 2 ); std::memcpy( &u1, uc.fx + 48, 8 ); std::memcpy( &ux1, uc.fx + 56, 2 );
			bool fsw_ok = ( ( fh ^ fu ) & ~0x0200 ) == 0;
			bool ulp_ok = x87_within_1ulp( hx0, h0, ux0, u0 ) && x87_within_1ulp( hx1, h1, ux1, u1 );
			if ( fh == fu && h0 == u0 && hx0 == ux0 && h1 == u1 && hx1 == ux1 ) ++s.exact;
			else if ( s.n - s.exact <= 3 )
				std::printf( "    FAIL: %s fcw=%04X x=%04X:%016llX y=%04X:%016llX: hw fsw %04X %04X:%016llX %04X:%016llX | uc fsw %04X %04X:%016llX %04X:%016llX\n",
							 c.op, c.fcw, c.se0, ( unsigned long long ) c.m0, c.se1, ( unsigned long long ) c.m1, fh, hx0, ( unsigned long long ) h0,
							 hx1, ( unsigned long long ) h1, fu, ux0, ( unsigned long long ) u0, ux1, ( unsigned long long ) u1 );
			if ( !fsw_ok && s.fsw++ < 3 )
				std::printf( "    FAIL: %s fcw=%04X x=%04X:%016llX y=%04X:%016llX: fsw hw %04X uc %04X\n", c.op, c.fcw, c.se0,
							 ( unsigned long long ) c.m0, c.se1, ( unsigned long long ) c.m1, fh, fu );
			if ( !ulp_ok && s.ulp++ < 3 )
				std::printf( "    FAIL: %s fcw=%04X x=%04X:%016llX y=%04X:%016llX: hw %04X:%016llX %04X:%016llX uc %04X:%016llX %04X:%016llX\n",
							 c.op, c.fcw, c.se0, ( unsigned long long ) c.m0, c.se1, ( unsigned long long ) c.m1, hx0, ( unsigned long long ) h0,
							 hx1, ( unsigned long long ) h1, ux0, ( unsigned long long ) u0, ux1, ( unsigned long long ) u1 );
		}
		std::printf( "    %-8s %zu cases\n", st.name, st.cases.size() );
		for ( int i = 0; i < 8; ++i )
		{
			const stat_t& s = stat[ i ];
			if ( !s.n ) continue;
			double share = double( s.exact ) / double( s.n );
			std::printf( "      %-8s n=%-6ld bit-exact %6.2f%%  FSW-mismatch(not C1) %ld  >1ulp %ld\n", X87_TRANS_OPS[ i ], s.n,
						 100.0 * share, s.fsw, s.ulp );
			CHECK( s.exact == s.n, "%s/%s: %ld of %ld cases not bit-exact", st.name, X87_TRANS_OPS[ i ], s.n - s.exact, s.n );
		}
	}
}

// ── R18: x87 arithmetic, loads, stores, compares vs hardware (plan 1.13) ──────────────────────
//
// Every x87 arithmetic/compare/load/store/stack form (register, popping, m16/m32/m64/m80 and
// integer operands) over special operands (zeros, denormals, pseudo-denormal, unnormal, NaNs,
// infinities, range and rounding edges), with FCW = all masked in two RC/PC settings and
// IE/DE/ZE/OE/UE unmasked (one at a time and all), FSW = 0 or C0-C3 preset, natively and in
// Unicorn. Required bit-exact: FSW, FTW, ST0, ST1, the memory operand and EFLAGS (FCOMI); the
// SDM Vol1 8.5 responses (unmasked #IA/#D/#Z: nothing stored, no pop; unmasked #O/#U: biased
// register result or no memory store) included. The transcendentals ride along with FSW preset,
// bit-exact as well (ledger U56).

struct x87_case2 { std::string op; uint16_t fcw, fsw, se0, se1; uint64_t m0, m1; uint8_t mem[ 16 ]; bool tolerant; };

static std::vector<x87_case2> x87_r18_cases()
{
	struct v80 { uint16_t se; uint64_t m; };
	const v80 st[] = {
		{ 0x0000, 0 }, { 0x8000, 0 }, { 0x0000, 1 }, { 0x8000, 0x4000000000000000ull }, { 0x0000, 0x8000000000000000ull },
		{ 0x0001, 0x8000000000000000ull }, { 0x3FBF, 0x8000000000000000ull }, { 0xBFC0, 0xC000000000000000ull },
		{ 0x3FFE, 0x8000000000000000ull }, { 0x3FFF, 0x8000000000000000ull }, { 0xBFFF, 0x8000000000000000ull },
		{ 0x3FFE, 0xFFFFFFFFFFFFFFFFull }, { 0x3FFF, 0x8000000000000001ull }, { 0x3FFF, 0xC000000000000000ull },
		{ 0x4000, 0xC90FDAA22168C235ull }, { 0x403D, 0xFFFFFFFFFFFFFFFFull }, { 0x403E, 0x8000000000000000ull },
		{ 0x7FFE, 0xFFFFFFFFFFFFFFFFull }, { 0xFFFE, 0xFFFFFFFFFFFFFFFFull }, { 0x7FFF, 0x8000000000000000ull },
		{ 0xFFFF, 0x8000000000000000ull }, { 0x7FFF, 0xC000000000000000ull }, { 0x7FFF, 0xA000000000000000ull },
		{ 0x3FFB, 0xCCCCCCCCCCCCCCCDull }, { 0x3FFF, 0x0000000000000000ull }, { 0x40D0, 0x9000000000000000ull },
		{ 0x0000, 0x7FFFFFFFFFFFFFFFull }, { 0x7FFE, 0x8000000000000001ull },
		// store-range edges: beyond single/double max, single/double denormal range, 2^15/2^31/2^63, 10^18
		{ 0x407F, 0xFFFFFF8000000000ull }, { 0x43FE, 0xFFFFFFFFFFFFF800ull }, { 0x3F81, 0x8000000000000000ull },
		{ 0x3C01, 0x8000000000000000ull }, { 0x3F6A, 0x8000000000000000ull }, { 0x3BCD, 0x8000000000000000ull },
		{ 0x400E, 0x8000000000000000ull }, { 0xC00E, 0x8000000000000000ull }, { 0x401E, 0x8000000000000000ull },
		{ 0x403C, 0xDE0B6B3A763FFFF0ull }, { 0x403C, 0xDE0B6B3A76400000ull }, { 0xBFFE, 0xC000000000000000ull },
	};
	const v80 st1[] = {
		{ 0x0000, 0 }, { 0x0000, 1 }, { 0x0001, 0x8000000000000000ull }, { 0x3FFF, 0x8000000000000000ull },
		{ 0xBFFF, 0xC000000000000000ull }, { 0x7FFE, 0xFFFFFFFFFFFFFFFFull }, { 0x7FFF, 0x8000000000000000ull },
		{ 0xFFFF, 0x8000000000000000ull }, { 0x7FFF, 0xC000000000000000ull }, { 0x7FFF, 0xA000000000000000ull },
		{ 0x3FFF, 0x0000000000000000ull }, { 0x3FBF, 0x8000000000000000ull },
	};
	const uint16_t fcws[] = { 0x037F, 0x0F7F, 0x007F, 0x0A7F, 0x037E, 0x037D, 0x037B, 0x0377, 0x036F, 0x0340 };
	const uint32_t m32[] = { 0, 0x80000000u, 1, 0x007FFFFFu, 0x3F800000u, 0x7F7FFFFFu, 0x7F800000u, 0x7FC00000u, 0x7FA00000u, 0x3EAAAAABu };
	const uint64_t m64[] = { 0, 1, 0x000FFFFFFFFFFFFFull, 0x3FF0000000000000ull, 0x7FF0000000000000ull, 0x7FF4000000000000ull };
	const int64_t ints[] = { 0, 1, -1, 32767, -32768, 2147483647, -2147483647 - 1 };
	std::vector<x87_case2> out;
	auto add = [ & ]( const std::string& op, const v80& a, const v80& b, const uint8_t* mem, size_t mlen, bool tol ) {
		for ( uint16_t fcw : fcws )
			for ( uint16_t fsw : { uint16_t( 0 ), uint16_t( 0x4700 ) } )
			{
				x87_case2 c{ op, fcw, fsw, a.se, b.se, a.m, b.m, {}, tol };
				std::memset( c.mem, 0xAA, 16 );
				if ( mem ) std::memcpy( c.mem, mem, mlen );
				out.push_back( c );
			}
	};
	const v80 one{ 0x3FFF, 0x8000000000000000ull };
	for ( const char* op : { "fadd st(0), st(1)", "fsub st(0), st(1)", "fsubr st(0), st(1)", "fmul st(0), st(1)", "fdiv st(0), st(1)",
							 "fdivr st(0), st(1)", "faddp st(1), st(0)", "fsubp st(1), st(0)", "fsubrp st(1), st(0)", "fmulp st(1), st(0)",
							 "fdivp st(1), st(0)", "fdivrp st(1), st(0)", "fscale", "fprem", "fprem1", "fcom st(1)", "fcomp st(1)", "fcompp",
							 "fucom st(1)", "fucompp", "fcomi st(0), st(1)", "fcomip st(0), st(1)", "fucomip st(0), st(1)", "fxch st(1)" } )
		for ( const v80& a : st )
			for ( const v80& b : st1 ) add( op, a, b, nullptr, 0, false );
	for ( const char* op : { "fyl2x", "fyl2xp1", "fpatan" } )
		for ( const v80& a : st )
			for ( const v80& b : st1 ) add( op, a, b, nullptr, 0, true );
	for ( const char* op : { "fsqrt", "fxtract", "frndint", "fabs", "fchs", "ftst", "fxam", "fincstp", "fdecstp", "ffree st(1)", "fld st(1)",
							 "fst st(1)", "fstp st(1)", "fld1", "fldz", "fldpi", "fldl2e", "fldl2t", "fldlg2", "fldln2" } )
		for ( const v80& a : st ) add( op, a, one, nullptr, 0, false );
	for ( const char* op : { "f2xm1", "fptan", "fsin", "fcos", "fsincos" } )
		for ( const v80& a : st ) add( op, a, one, nullptr, 0, true );
	for ( const char* o : { "fadd", "fmul", "fsub", "fsubr", "fdiv", "fdivr", "fcom", "fcomp" } )
		for ( const v80& a : st )
		{
			for ( uint32_t v : m32 ) add( std::string( o ) + " dword ptr [rcx + 192]", a, one, ( const uint8_t* ) &v, 4, false );
			for ( uint64_t v : m64 ) add( std::string( o ) + " qword ptr [rcx + 192]", a, one, ( const uint8_t* ) &v, 8, false );
		}
	for ( const char* o : { "fiadd", "fimul", "fisub", "fisubr", "fidiv", "fidivr", "ficom", "ficomp" } )
		for ( size_t ai = 0; ai < std::size( st ); ai += 2 )
			for ( int64_t v : ints )
			{
				int16_t w = int16_t( v );
				int32_t d = int32_t( v );
				add( std::string( o ) + " word ptr [rcx + 192]", st[ ai ], one, ( const uint8_t* ) &w, 2, false );
				add( std::string( o ) + " dword ptr [rcx + 192]", st[ ai ], one, ( const uint8_t* ) &d, 4, false );
			}
	for ( uint32_t v : m32 ) add( "fld dword ptr [rcx + 192]", one, one, ( const uint8_t* ) &v, 4, false );
	for ( uint64_t v : m64 ) add( "fld qword ptr [rcx + 192]", one, one, ( const uint8_t* ) &v, 8, false );
	for ( const v80& a : st )
	{
		uint8_t t[ 10 ];
		std::memcpy( t, &a.m, 8 ); std::memcpy( t + 8, &a.se, 2 );
		add( "fld tbyte ptr [rcx + 192]", one, one, t, 10, false );
	}
	for ( int64_t v : ints )
	{
		int16_t w = int16_t( v );
		int32_t d = int32_t( v );
		add( "fild word ptr [rcx + 192]", one, one, ( const uint8_t* ) &w, 2, false );
		add( "fild dword ptr [rcx + 192]", one, one, ( const uint8_t* ) &d, 4, false );
		add( "fild qword ptr [rcx + 192]", one, one, ( const uint8_t* ) &v, 8, false );
	}
	for ( const char* op : { "fst dword ptr [rcx + 192]", "fstp dword ptr [rcx + 192]", "fst qword ptr [rcx + 192]", "fstp qword ptr [rcx + 192]",
							 "fstp tbyte ptr [rcx + 192]", "fist word ptr [rcx + 192]", "fistp word ptr [rcx + 192]", "fist dword ptr [rcx + 192]",
							 "fistp dword ptr [rcx + 192]", "fistp qword ptr [rcx + 192]", "fisttp word ptr [rcx + 192]",
							 "fisttp dword ptr [rcx + 192]", "fisttp qword ptr [rcx + 192]", "fbstp tbyte ptr [rcx + 192]" } )
		for ( const v80& a : st ) add( op, a, one, nullptr, 0, false );
	return out;
}

static void test_r18()
{
	std::printf( "R18 x87 arithmetic / compares / loads / stores vs hardware: bit-exact (SDM Vol1 8.5 responses)\n" );
	std::vector<x87_case2> cases = x87_r18_cases();
	auto put = [ & ]( uint8_t* p, uint16_t se, uint64_t m ) { std::memcpy( p, &m, 8 ); std::memcpy( p + 8, &se, 2 ); };
	struct stat_t { long n = 0, bad = 0; };
	std::map<std::string, stat_t> stats;
	std::map<std::string, std::vector<uint8_t>> codes;
	long total = 0, failed = 0;
	for ( const x87_case2& c : cases )
	{
		auto ci = codes.find( c.op );
		if ( ci == codes.end() )
		{
			ci = codes.emplace( c.op, assemble( r11_thunk( c.op.c_str() ) ) ).first;
			CHECK( !ci->second.empty(), "%s: assembly", c.op.c_str() );
		}
		const std::vector<uint8_t>& code = ci->second;
		if ( code.empty() ) continue;
		uint8_t in[ 208 ] = {};
		for ( int i = 0; i < 8; ++i ) put( in + 32 + i * 16, 0x3FFF, 1ull << 63 );
		put( in + 32 + 7 * 16, c.se0, c.m0 );
		put( in + 32 + 6 * 16, c.se1, c.m1 );
		std::memcpy( in + 192, c.mem, 16 );
		uint16_t ftw = 0xC000;                          // ST(7) empty: pushes possible
		std::memcpy( in + 0, &c.fcw, 2 ); std::memcpy( in + 4, &c.fsw, 2 ); std::memcpy( in + 8, &ftw, 2 );
		r11_result hw = r11_native( code, in ), uc = r11_unicorn( code, in, X87_HW_QUIRKS );
		stat_t& s = stats[ c.op ];
		++s.n;
		++total;
		bool ok;
		if ( hw.fault >= 0 || uc.fault >= 0 )
			ok = hw.fault == uc.fault;
		else
		{
			uint16_t fh = uint16_t( hw.env[ 4 ] | ( hw.env[ 5 ] << 8 ) ), fu = uint16_t( uc.env[ 4 ] | ( uc.env[ 5 ] << 8 ) );
			bool same_tw = hw.env[ 8 ] == uc.env[ 8 ] && hw.env[ 9 ] == uc.env[ 9 ];
			bool same_mem = !std::memcmp( hw.mem, uc.mem, 16 ) && hw.ah == uc.ah;
			ok = fh == fu && same_tw && same_mem && !std::memcmp( hw.fx + 32, uc.fx + 32, 10 ) && !std::memcmp( hw.fx + 48, uc.fx + 48, 10 );
		}
		if ( !ok )
		{
			++failed;
			if ( s.bad++ < 2 )
				std::printf( "    FAIL: %s fcw=%04X fsw=%04X x=%04X:%016llX y=%04X:%016llX: hw fsw=%04X ftw=%02X%02X | uc fsw=%04X ftw=%02X%02X\n",
							 c.op.c_str(), c.fcw, c.fsw, c.se0, ( unsigned long long ) c.m0, c.se1, ( unsigned long long ) c.m1,
							 unsigned( hw.env[ 4 ] | ( hw.env[ 5 ] << 8 ) ), hw.env[ 9 ], hw.env[ 8 ], unsigned( uc.env[ 4 ] | ( uc.env[ 5 ] << 8 ) ),
							 uc.env[ 9 ], uc.env[ 8 ] );
		}
	}
	long clean = 0;
	for ( auto& kv : stats ) clean += kv.second.bad == 0;
	std::printf( "    %ld cases, %zu instruction forms: %ld bit-exact forms, %ld mismatching cases\n", total, stats.size(), clean, failed );
	for ( auto& kv : stats )
		if ( kv.second.bad ) std::printf( "      %-34s %ld / %ld\n", kv.first.c_str(), kv.second.bad, kv.second.n );
	CHECK( failed == 0, "%ld x87 cases differ from the hardware", failed );
}

// Logical processors of the P-cores (highest efficiency class) or E-cores (class 0) of a hybrid CPU
// (i5-13600K: 6 Raptor Cove P-cores with 2 threads each, 8 Gracemont E-cores).
static DWORD_PTR x87_core_mask( bool performance )
{
	DWORD len = 0;
	GetLogicalProcessorInformationEx( RelationProcessorCore, nullptr, &len );
	std::vector<uint8_t> buf( len );
	if ( !GetLogicalProcessorInformationEx( RelationProcessorCore, ( PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX ) buf.data(), &len ) ) return 0;
	BYTE maxc = 0;
	for ( DWORD off = 0; off < len; )
	{
		auto* p = ( PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX ) ( buf.data() + off );
		maxc = p->Processor.EfficiencyClass > maxc ? p->Processor.EfficiencyClass : maxc;
		off += p->Size;
	}
	DWORD_PTR mask = 0;
	for ( DWORD off = 0; off < len; )
	{
		auto* p = ( PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX ) ( buf.data() + off );
		if ( p->Processor.EfficiencyClass == ( performance ? maxc : 0 ) && p->Processor.GroupMask[ 0 ].Group == 0 )
			mask |= p->Processor.GroupMask[ 0 ].Mask;
		off += p->Size;
	}
	return mask;
}

int main( int argc, char** argv )
{
	std::setvbuf( stdout, nullptr, _IONBF, 0 );     // a crash must not swallow buffered output
	// `--cpu-map`: per logical processor, CPUID.1A core type (0x40 Core = P, 0x20 Atom = E) and the
	// hypervisor bit (CPUID.1:ECX[31])
	if ( argc >= 2 && std::strcmp( argv[ 1 ], "--cpu-map" ) == 0 )
	{
		DWORD n = GetActiveProcessorCount( 0 );
		for ( DWORD i = 0; i < n && i < 64; ++i )
		{
			SetThreadAffinityMask( GetCurrentThread(), DWORD_PTR( 1 ) << i );
			Sleep( 1 );
			int r1[ 4 ], r1a[ 4 ], r0[ 4 ];
			__cpuid( r0, 0 );
			__cpuid( r1, 1 );
			__cpuidex( r1a, 0x1A, 0 );
			unsigned type = unsigned( r1a[ 0 ] ) >> 24;
			std::printf( "LP %2lu: max leaf %#x  CPUID.1 EAX=%08X ECX.hypervisor=%d  CPUID.1A EAX=%08X core type %#x (%s)\n", i, r0[ 0 ],
						 unsigned( r1[ 0 ] ), ( unsigned( r1[ 2 ] ) >> 31 ) & 1, unsigned( r1a[ 0 ] ), type,
						 type == 0x40 ? "P-core" : type == 0x20 ? "E-core" : "not reported" );
		}
		return 0;
	}
	// `--core P|E` (first argument): pin the native reference runs to the P-cores or the E-cores
	if ( argc >= 3 && std::strcmp( argv[ 1 ], "--core" ) == 0 )
	{
		bool perf = argv[ 2 ][ 0 ] == 'P' || argv[ 2 ][ 0 ] == 'p';
		// or an explicit affinity mask: `--core 0x4`
		DWORD_PTR mask = argv[ 2 ][ 0 ] == '0' ? DWORD_PTR( std::strtoull( argv[ 2 ], nullptr, 16 ) ) : x87_core_mask( perf );
		DWORD_PTR prev = mask ? SetThreadAffinityMask( GetCurrentThread(), mask ) : 0;
		std::printf( "pinned to %s: affinity mask %llX (%s)\n", argv[ 2 ][ 0 ] == '0' ? "the given mask" : perf ? "the P-cores" : "the E-cores",
					 ( unsigned long long ) mask, prev ? "ok" : "FAILED" );
		if ( !prev ) return 2;
		argc -= 2;
		argv += 2;
	}
	if ( argc >= 3 && std::strcmp( argv[ 1 ], "--x87-dump" ) == 0 )
	{
		x87_dump( argv[ 2 ], argc >= 4 && std::strcmp( argv[ 3 ], "--uc" ) == 0 );
		return 0;
	}
	if ( argc >= 4 && std::strcmp( argv[ 1 ], "--x87-list" ) == 0 )
	{
		x87_list( argv[ 2 ], argv[ 3 ], argc >= 5 && std::strcmp( argv[ 4 ], "--uc" ) == 0 );
		return 0;
	}
	if ( argc >= 4 && std::strcmp( argv[ 1 ], "--x87-list2" ) == 0 )
	{
		x87_list2( argv[ 2 ], argv[ 3 ], argc >= 5 && std::strcmp( argv[ 4 ], "--uc" ) == 0 );
		return 0;
	}
	if ( argc >= 3 && std::strcmp( argv[ 1 ], "--x87-special" ) == 0 )
	{
		x87_special( argv[ 2 ], argc >= 4 && std::strcmp( argv[ 3 ], "--uc" ) == 0 );
		return 0;
	}
	if ( argc >= 8 && std::strcmp( argv[ 1 ], "--x87-probe" ) == 0 )
	{
		x87_probe( argv[ 2 ], argv[ 3 ], argv[ 4 ], std::atoll( argv[ 5 ] ), std::atol( argv[ 6 ] ), argv[ 7 ] );
		return 0;
	}
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
	std::printf( "\n" );
	test_r12();
	std::printf( "\n" );
	test_r13();
	std::printf( "\n" );
	test_r14();
	std::printf( "\n" );
	test_r15();
	std::printf( "\n" );
	test_r16();
	std::printf( "\n" );
	test_r17();
	std::printf( "\n" );
	test_r18();
	std::printf( "\n%s: %d failure(s)\n", g_failures ? "FAILED" : "SUCCESS", g_failures );
	return g_failures ? 1 : 0;
}
