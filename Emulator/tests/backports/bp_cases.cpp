// Backport cases, one group per area. Filled in per commit as each backport is analysed:
// a case is written first, run against the unpatched branch, and the backport is ported only
// where the case shows the bug (otherwise the commit is recorded as not applicable, with the
// passing case as proof).
#include "bp_harness.hpp"
#include <deque>

namespace bp
{
	// distinct, non-trivial 128-bit inputs for xmm0..xmm7 and the scratch block
	static void sha_state( state& s )
	{
		uint64_t x = 0x243F6A8885A308D3ull;    // pi digits as a simple deterministic stream
		auto next = [ & ] { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return x; };
		for ( int i = 0; i < 8; ++i )
		{
			uint64_t q[ 2 ] = { next(), next() };
			s.set_xmm( i, q );
		}
		for ( size_t i = 0; i < 64; i += 8 ) { uint64_t v = next(); std::memcpy( s.scratch + i, &v, 8 ); }
	}

	struct cmpcc_vec
	{
		const char* cas; const char* mn; int width; std::vector<uint8_t> bytes;
		uint64_t mem0, rcx, rdx, rcx_after, flags, mem_after;
	};
	static const cmpcc_vec k_cmpcc[] = {
#include "bp_cmpccxadd.inc"
	};

	static bool fault_is( const result& r, int vec, std::string& why )
	{
		if ( r.faulted && r.vector == vec ) return true;
		why = r.faulted ? "vector " + std::to_string( r.vector ) + ", expected " + std::to_string( vec ) : "no fault, expected vector " + std::to_string( vec );
		return false;
	}

	std::vector<test_case> isa_cases()
	{
		std::vector<test_case> v;
		const uc_cpu_x86 MAX = UC_CPU_X86_MAX;   // QEMU "max": every feature the TCG translator implements

		// UC_CPU_X86_MAX must enumerate the backported features (CPUID.7.0:EBX SHA[29] RDSEED[18],
		// ECX RDPID[22]; CPUID.7.1:EAX CMPCCXADD[7]) besides the 7.2 ones (AVX2[5] BMI1[3] BMI2[8] ADX[19])
		{
			test_case t{ "UC_CPU_X86_MAX", "CPUID leaf 7 enumerates SHA/RDSEED/RDPID/CMPccXADD + AVX2/BMI/ADX",
						 "mov eax, 7\nxor ecx, ecx\ncpuid\nmov r8, rbx\nmov r9, rcx\nmov eax, 7\nmov ecx, 1\ncpuid\nmov r10, rax\n" };
			t.hardware = false;
			t.model = MAX;
			t.expect = [ ]( const result& r, std::string& why ) {
				uint32_t ebx = uint32_t( r.s.gpr[ R8 ] ), ecx = uint32_t( r.s.gpr[ R9 ] ), eax1 = uint32_t( r.s.gpr[ R10 ] );
				struct { const char* n; uint32_t reg; int bit; } need[] = {
					{ "AVX2", ebx, 5 }, { "BMI1", ebx, 3 }, { "BMI2", ebx, 8 }, { "ADX", ebx, 19 },
					{ "RDSEED", ebx, 18 }, { "SHA", ebx, 29 }, { "RDPID", ecx, 22 }, { "CMPCCXADD", eax1, 7 } };
				for ( auto& n : need )
					if ( !( ( n.reg >> n.bit ) & 1 ) ) { why = std::string( n.n ) + " missing (leaf7 ebx=" + hx( ebx ) + " ecx=" + hx( ecx ) + " 7.1.eax=" + hx( eax1 ) + ")"; return false; }
				return true;
			};
			v.push_back( t );
		}

		// e582b629f0 target/i386: implement SHA instructions (host 13600K has SHA-NI)
		struct { const char* name; const char* text; } sha[] = {
			{ "sha1rnds4 imm 0..3", "sha1rnds4 xmm1, xmm2, 0\nsha1rnds4 xmm3, xmm4, 1\nsha1rnds4 xmm5, xmm6, 2\nsha1rnds4 xmm7, xmm1, 3\n" },
			{ "sha1nexte", "sha1nexte xmm1, xmm2\nsha1nexte xmm3, xmm1\n" },
			{ "sha1msg1", "sha1msg1 xmm1, xmm2\nsha1msg1 xmm4, xmm4\n" },
			{ "sha1msg2", "sha1msg2 xmm1, xmm2\nsha1msg2 xmm5, xmm3\n" },
			{ "sha256rnds2 (implicit xmm0)", "sha256rnds2 xmm1, xmm2, xmm0\nsha256rnds2 xmm3, xmm4, xmm0\n" },
			{ "sha256msg1", "sha256msg1 xmm1, xmm2\nsha256msg1 xmm6, xmm6\n" },
			{ "sha256msg2", "sha256msg2 xmm1, xmm2\nsha256msg2 xmm7, xmm3\n" },
		};
		for ( auto& c : sha )
		{
			test_case t{ "e582b629f0", c.name, c.text };
			t.init = sha_state;
			t.model = MAX;
			v.push_back( t );
		}
		{
			test_case t{ "e582b629f0", "SHA memory operands", "" };
			t.asm_text = "mov rax, " + hx( SCRATCH ) + "\nsha1msg1 xmm1, xmmword ptr [rax]\nsha256msg2 xmm2, xmmword ptr [rax + 16]\n"
						 "sha1rnds4 xmm3, xmmword ptr [rax + 32], 2\nsha256rnds2 xmm4, xmmword ptr [rax + 48], xmm0\n";
			t.init = sha_state;
			t.model = MAX;
			v.push_back( t );
		}
		{
			test_case t{ "e582b629f0", "SHA misaligned memory operand -> #GP", "" };
			t.asm_text = "mov rax, " + hx( SCRATCH + 8 ) + "\nsha1msg1 xmm1, xmmword ptr [rax]\n";
			t.init = sha_state;
			t.model = MAX;
			v.push_back( t );
		}
		{
			test_case t{ "e582b629f0", "SHA-256 round/schedule chain (16 ops)", "" };
			std::string s;
			for ( int i = 0; i < 4; ++i )
				s += "sha256msg1 xmm4, xmm5\nsha256msg2 xmm4, xmm6\nsha256rnds2 xmm2, xmm1, xmm0\nsha256rnds2 xmm1, xmm2, xmm0\n";
			t.asm_text = s;
			t.init = sha_state;
			t.model = MAX;
			v.push_back( t );
		}
		{
			test_case t{ "e582b629f0", "SHA with a 66 prefix -> #UD (no legacy prefixes allowed)", "" };
			t.bytes = { 0x66, 0x0F, 0x38, 0xC9, 0xCA };           // 66 sha1msg1 xmm1, xmm2
			t.init = sha_state;
			t.model = MAX;
			v.push_back( t );
		}

		// 691925e5a3 (+ f9e0dbae78) RDSEED: retry until CF=1; compare flags and register width
		auto seed_loop = [ ]( const char* reg ) { return std::string( "retry_" ) + reg + ":\nrdseed " + reg + "\njnc retry_" + reg + "\n"; };
		{
			test_case t{ "691925e5a3", "rdseed eax / ax / rax: CF=1, other flags 0, operand width", "" };
			t.asm_text = seed_loop( "eax" ) + "mov r8, rax\n" + seed_loop( "bx" ) + seed_loop( "rcx" );
			t.init = [ ]( state& s ) { s.gpr[ RAX ] = ~0ull; s.gpr[ RBX ] = ~0ull; s.rflags = 0x8D7; };
			t.model = MAX;
			t.compare = C_ALL & ~C_GPR;
			t.check_each = [ ]( const result& r, std::string& why ) {
				if ( r.s.gpr[ R8 ] >> 32 ) { why = "rdseed eax left upper bits: " + hx( r.s.gpr[ R8 ] ); return false; }
				if ( ( r.s.gpr[ RBX ] >> 16 ) != 0xFFFFFFFFFFFFull ) { why = "rdseed bx clobbered bits 63:16: " + hx( r.s.gpr[ RBX ] ); return false; }
				if ( ( r.s.rflags & 0x8D5 ) != 0x1 ) { why = "flags " + hx( r.s.rflags & 0x8D5 ) + ", expected CF only"; return false; }
				return true;
			};
			v.push_back( t );
		}
		{
			test_case t{ "691925e5a3", "rdseed on a model without RDSEED (Haswell) -> #UD", "rdseed eax\n" };
			t.hardware = false;
			t.model = UC_CPU_X86_HASWELL;
			t.expect = [ ]( const result& r, std::string& why ) { return fault_is( r, 6, why ); };
			v.push_back( t );
		}

		// 6750485bf4 (+ f2c04bede3) RDPID: TSC_AUX into the full register; 66/REX.W ignored
		{
			test_case t{ "6750485bf4", "rdpid rax / 66 rdpid: full-register write (host TSC_AUX = processor id)", "" };
			t.bytes = { 0xF3, 0x0F, 0xC7, 0xF8, 0x49, 0x89, 0xC0, 0x48, 0x83, 0xC8, 0xFF, 0x66, 0xF3, 0x0F, 0xC7, 0xF8 };   // rdpid rax; mov r8,rax; or rax,-1; 66 rdpid rax
			t.init = [ ]( state& s ) { s.gpr[ RAX ] = ~0ull; };
			t.model = MAX;
			t.compare = C_ALL & ~C_GPR & ~C_FLAGS;
			t.check_each = [ ]( const result& r, std::string& why ) {
				if ( r.s.gpr[ R8 ] > 0xFFF || r.s.gpr[ RAX ] > 0xFFF ) { why = "upper bits not cleared: r8 " + hx( r.s.gpr[ R8 ] ) + " rax " + hx( r.s.gpr[ RAX ] ); return false; }
				if ( r.s.gpr[ R8 ] != r.s.gpr[ RAX ] ) { why = "66 form differs: " + hx( r.s.gpr[ RAX ] ) + " vs " + hx( r.s.gpr[ R8 ] ); return false; }
				return true;
			};
			v.push_back( t );
		}
		{
			test_case t{ "6750485bf4", "rdpid returns TSC_AUX (0x12345)", "" };
			t.bytes = { 0xF3, 0x0F, 0xC7, 0xF8 };
			t.init = [ ]( state& s ) { s.gpr[ RAX ] = ~0ull; };
			t.hardware = false;
			t.model = MAX;
			t.uc_setup = [ ]( uc_engine* uc ) { uc_x86_msr m{ 0xC0000103, 0x12345 }; uc_reg_write( uc, UC_X86_REG_MSR, &m ); };
			t.expect = [ ]( const result& r, std::string& why ) {
				if ( r.faulted ) { why = "faulted, vector " + std::to_string( r.vector ); return false; }
				if ( r.s.gpr[ RAX ] == 0x12345 ) return true;
				why = "rax " + hx( r.s.gpr[ RAX ] ); return false;
			};
			v.push_back( t );
		}
		{
			test_case t{ "6750485bf4", "rdpid with a memory operand -> #UD", "" };
			t.bytes = { 0xF3, 0x0F, 0xC7, 0x38 };                 // F3 0F C7 /7 with mod=00
			t.init = [ ]( state& s ) { s.gpr[ RAX ] = SCRATCH; };
			t.model = MAX;
			v.push_back( t );
		}

		// 405c7c0708 (+ a9ce107fd0) CMPccXADD: host 13600K lacks it -> SDM vectors (96)
		for ( const auto& c : k_cmpcc )
		{
			static std::deque<std::string> names;   // stable addresses: test_case keeps name as const char*
			names.push_back( std::string( "case " ) + c.cas + " " + c.mn + ( c.width == 64 ? " qword" : " dword" ) );
			test_case t{ "405c7c0708", names.back().c_str(), "" };
			t.bytes = c.bytes;
			t.hardware = false;
			t.model = MAX;
			t.init = [ c ]( state& s ) {
				s.gpr[ RAX ] = SCRATCH; s.gpr[ RCX ] = c.rcx; s.gpr[ RDX ] = c.rdx;
				std::memcpy( s.scratch, &c.mem0, c.width / 8 );
			};
			t.expect = [ c ]( const result& r, std::string& why ) {
				if ( r.faulted ) { why = "faulted, vector " + std::to_string( r.vector ); return false; }
				uint64_t mem = 0;
				std::memcpy( &mem, r.s.scratch, c.width / 8 );
				if ( r.s.gpr[ RCX ] != c.rcx_after ) { why = "rcx " + hx( r.s.gpr[ RCX ] ) + ", expected " + hx( c.rcx_after ); return false; }
				if ( ( r.s.rflags & 0x8D5 ) != c.flags ) { why = "flags " + hx( r.s.rflags & 0x8D5 ) + ", expected " + hx( c.flags ); return false; }
				if ( mem != c.mem_after ) { why = "[rax] " + hx( mem ) + ", expected " + hx( c.mem_after ); return false; }
				if ( r.s.gpr[ RDX ] != c.rdx ) { why = "rdx changed"; return false; }
				return true;
			};
			v.push_back( t );
		}
		auto cmpcc_ud = [ & ]( const char* name, std::vector<uint8_t> b, uc_cpu_x86 model ) {
			test_case t{ "405c7c0708", name, "" };
			t.bytes = std::move( b );
			t.hardware = false;
			t.model = model;
			t.init = [ ]( state& s ) { s.gpr[ RAX ] = SCRATCH; };
			t.expect = [ ]( const result& r, std::string& why ) { return fault_is( r, 6, why ); };
			v.push_back( t );
		};
		cmpcc_ud( "cmpzxadd with a register operand (mod=11) -> #UD", { 0xC4, 0xE2, 0x69, 0xE4, 0xC8 }, MAX );
		cmpcc_ud( "cmpzxadd with VEX.L=1 -> #UD", { 0xC4, 0xE2, 0x6D, 0xE4, 0x08 }, MAX );
		cmpcc_ud( "cmpzxadd on a model without CMPccXADD (Haswell) -> #UD", { 0xC4, 0xE2, 0x69, 0xE4, 0x08 }, UC_CPU_X86_HASWELL );
		return v;
	}

	// ── integer flags / decoding (agent analysis: scratchpad\bp\integer.md) ───────────────────
	static test_case raw( const char* commit, const char* name, std::vector<uint8_t> b, std::function<void( state& )> init = {}, uint64_t mask = 0x8D5 )
	{
		test_case t{ commit, name, "" };
		t.bytes = std::move( b );
		t.init = std::move( init );
		t.flag_mask = mask;
		return t;
	}

	std::vector<test_case> integer_cases()
	{
		std::vector<test_case> v;
		const uint64_t TEST_FLAGS = 0x8C5;   // CF PF ZF SF OF (TEST leaves AF undefined)

		// 380b959618: F6 /1 ib and F7 /1 iz execute as TEST
		v.push_back( raw( "380b959618", "F6 /1: test al, 0x0F (al=0xF0 -> ZF=1)", { 0xF6, 0xC8, 0x0F },
						  [ ]( state& s ) { s.gpr[ RAX ] = 0x12345678ABCDEFF0ull; }, TEST_FLAGS ) );
		v.push_back( raw( "380b959618", "REX.W F7 /1: test rax, 0x80000000 (sign-extended imm32)", { 0x48, 0xF7, 0xC8, 0x00, 0x00, 0x00, 0x80 },
						  [ ]( state& s ) { s.gpr[ RAX ] = 0x8000000000000000ull; }, TEST_FLAGS ) );
		v.push_back( raw( "380b959618", "F7 /1: test eax, 0xFFFF", { 0xF7, 0xC8, 0xFF, 0xFF, 0x00, 0x00 },
						  [ ]( state& s ) { s.gpr[ RAX ] = 0x12340000ull; }, TEST_FLAGS ) );
		v.push_back( raw( "380b959618", "F6 /1 memory: test byte [rax], 0x0F", { 0xF6, 0x08, 0x0F },
						  [ ]( state& s ) { s.gpr[ RAX ] = SCRATCH + 3; }, TEST_FLAGS ) );
		v.push_back( raw( "380b959618", "66 F7 /1 memory: test word [rax], 0x8001", { 0x66, 0xF7, 0x08, 0x01, 0x80 },
						  [ ]( state& s ) { s.gpr[ RAX ] = SCRATCH + 6; }, TEST_FLAGS ) );
		{
			// RIP-relative with an immediate after the displacement: the effective address must use
			// the address of the NEXT instruction (rip_offset must include the immediate).
			test_case t{ "380b959618", "F6 /1 rip-relative: test byte [rip+disp], imm8", "" };
			t.gen_bytes = [ ]( uint64_t at ) {
				int32_t disp = int32_t( ( SCRATCH + 5 ) - ( at + 7 ) );
				std::vector<uint8_t> b = { 0xF6, 0x0D };
				for ( int i = 0; i < 4; ++i ) b.push_back( uint8_t( disp >> ( 8 * i ) ) );
				b.push_back( 0x81 );
				return b;
			};
			t.flag_mask = TEST_FLAGS;
			v.push_back( t );
		}

		// 3589cd995b: in 64-bit mode ES/CS/SS/DS are null prefixes and must not replace FS/GS
		{
			test_case t{ "3589cd995b", "gs + es/cs/ss/ds null prefixes keep the GS base", "" };
			auto m = [ ]( std::vector<uint8_t> pfx, uint8_t modrm ) {
				pfx.insert( pfx.end(), { 0x48, 0x8B, modrm, 0x25, 0x30, 0x00, 0x00, 0x00 } );
				return pfx;
			};
			for ( auto& part : { m( { 0x65 }, 0x0C ),           // rcx = gs:[0x30]        (reference)
								  m( { 0x65, 0x26 }, 0x04 ),     // rax = gs: es: [0x30]
								  m( { 0x65, 0x2E }, 0x14 ),     // rdx = gs: cs: [0x30]
								  m( { 0x65, 0x36 }, 0x1C ),     // rbx = gs: ss: [0x30]
								  m( { 0x65, 0x3E }, 0x34 ),     // rsi = gs: ds: [0x30]
								  m( { 0x26, 0x65 }, 0x3C ) } )  // rdi = es: gs: [0x30]
				t.bytes.insert( t.bytes.end(), part.begin(), part.end() );
			// GS base differs between engines (TEB natively): compare everything but the GPRs, and
			// require every load to have read the same GS-relative qword in each engine.
			t.compare = C_ALL & ~C_GPR;
			t.check_each = [ ]( const result& r, std::string& why ) {
				uint64_t ref = r.s.gpr[ RCX ];
				for ( int i : { RAX, RDX, RBX, RSI, RDI } )
					if ( r.s.gpr[ i ] != ref ) { why = std::string( reg_name( i ) ) + " = " + hx( r.s.gpr[ i ] ) + ", gs:[0x30] = " + hx( ref ); return false; }
				if ( r.faulted ) { why = "faulted, vector " + std::to_string( r.vector ); return false; }
				return true;
			};
			t.uc_setup = [ ]( uc_engine* uc ) { uint64_t gs = SCRATCH + 0x100 - 0x30; uc_reg_write( uc, UC_X86_REG_GS_BASE, &gs ); };
			v.push_back( t );
		}

		// 76ad26dd17: MOVBE / CRC32 operand size follows the operand-size attribute
		auto movbe_init = [ ]( state& s ) {
			s.gpr[ RAX ] = 0xAAAAAAAAAAAAAAAAull;
			s.gpr[ RDI ] = SCRATCH;
			const uint8_t b[ 8 ] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
			std::memcpy( s.scratch, b, 8 );
		};
		v.push_back( raw( "76ad26dd17", "movbe ax, [rdi]  (66)", { 0x66, 0x0F, 0x38, 0xF0, 0x07 }, movbe_init ) );
		v.push_back( raw( "76ad26dd17", "movbe eax, [rdi]", { 0x0F, 0x38, 0xF0, 0x07 }, movbe_init ) );
		v.push_back( raw( "76ad26dd17", "movbe rax, [rdi] (REX.W)", { 0x48, 0x0F, 0x38, 0xF0, 0x07 }, movbe_init ) );
		v.push_back( raw( "76ad26dd17", "movbe rax, [rdi] (66 + REX.W: REX.W wins)", { 0x66, 0x48, 0x0F, 0x38, 0xF0, 0x07 }, movbe_init ) );
		v.push_back( raw( "76ad26dd17", "movbe [rdi], ax / eax / rax / 66+REX.W", { 0x66, 0x0F, 0x38, 0xF1, 0x07, 0x0F, 0x38, 0xF1, 0x47, 0x08,
																					   0x48, 0x0F, 0x38, 0xF1, 0x47, 0x10, 0x66, 0x48, 0x0F, 0x38, 0xF1, 0x47, 0x20 },
						  [ ]( state& s ) { s.gpr[ RAX ] = 0x0102030405060708ull; s.gpr[ RDI ] = SCRATCH; } ) );
		auto crc_init = [ ]( state& s ) { s.gpr[ RAX ] = 0xFFFFFFFFull; s.gpr[ RBX ] = 0x12345678ull; };
		auto crc_is = [ ]( uint64_t want ) {
			return [ want ]( const result& r, std::string& why ) { if ( r.s.gpr[ RAX ] == want ) return true; why = "rax " + hx( r.s.gpr[ RAX ] ) + ", expected " + hx( want ); return false; };
		};
		{
			test_case t = raw( "76ad26dd17", "crc32 eax, bx (66) = 0x5B57A229", { 0x66, 0xF2, 0x0F, 0x38, 0xF1, 0xC3 }, crc_init );
			t.check_each = crc_is( 0x5B57A229 );        // also confirms the 16-bit-mode constant below
			v.push_back( t );
		}
		{
			test_case t = raw( "76ad26dd17", "crc32 eax, ebx = 0x4DECE20C", { 0xF2, 0x0F, 0x38, 0xF1, 0xC3 }, crc_init );
			t.check_each = crc_is( 0x4DECE20C );
			v.push_back( t );
		}
		v.push_back( raw( "76ad26dd17", "crc32 rax, rbx (REX.W)", { 0xF2, 0x48, 0x0F, 0x38, 0xF1, 0xC3 }, crc_init ) );
		v.push_back( raw( "76ad26dd17", "crc32 rax, rbx (66 + REX.W: REX.W wins)", { 0x66, 0xF2, 0x48, 0x0F, 0x38, 0xF1, 0xC3 }, crc_init ) );
		v.push_back( raw( "76ad26dd17", "crc32 eax, bl", { 0xF2, 0x0F, 0x38, 0xF0, 0xC3 }, crc_init ) );
		// 16-bit (real) mode: the default operand size is 16, 66 selects 32 (SDM-checked, Unicorn only)
		auto r16 = [ ]( const char* name, std::vector<uint8_t> b, uint32_t want ) {
			test_case t{ "76ad26dd17", name, "" };
			t.bytes = std::move( b );
			t.mode = UC_MODE_16;
			t.hardware = false;
			t.init = [ ]( state& s ) {
				s.gpr[ RAX ] = 0xAAAAAAAAull; s.gpr[ RBX ] = 0x2000;   // [bx] = linear 0x2000 = scratch
				const uint8_t d[ 4 ] = { 0x11, 0x22, 0x33, 0x44 };
				std::memcpy( s.scratch, d, 4 );
			};
			t.expect = [ want ]( const result& r, std::string& why ) {
				if ( r.faulted ) { why = "faulted, vector " + std::to_string( r.vector ); return false; }
				if ( uint32_t( r.s.gpr[ RAX ] ) == want ) return true;
				why = "eax " + hx( uint32_t( r.s.gpr[ RAX ] ) ) + ", expected " + hx( want );
				return false;
			};
			return t;
		};
		v.push_back( r16( "real mode: movbe ax, [bx] -> eax 0xAAAA1122", { 0x0F, 0x38, 0xF0, 0x07 }, 0xAAAA1122 ) );
		v.push_back( r16( "real mode: 66 movbe eax, [bx] -> 0x11223344", { 0x66, 0x0F, 0x38, 0xF0, 0x07 }, 0x11223344 ) );
		{
			test_case t = r16( "real mode: crc32 eax, bx -> 0x5B57A229", { 0xF2, 0x0F, 0x38, 0xF1, 0xC3 }, 0x5B57A229 );
			t.init = [ ]( state& s ) { s.gpr[ RAX ] = 0xFFFFFFFFull; s.gpr[ RBX ] = 0x12345678ull; };
			v.push_back( t );
		}
		{
			test_case t = r16( "real mode: 66 crc32 eax, ebx -> 0x4DECE20C", { 0x66, 0xF2, 0x0F, 0x38, 0xF1, 0xC3 }, 0x4DECE20C );
			t.init = [ ]( state& s ) { s.gpr[ RAX ] = 0xFFFFFFFFull; s.gpr[ RBX ] = 0x12345678ull; };
			v.push_back( t );
		}

		// Regression checks for the commits analysed as NOT-APPLICABLE (prove, don't assume)
		{
			test_case t{ "83a3a20e59", "blsi CF: source 0 -> CF=0, source !=0 -> CF=1", "blsi rax, rbx\nblsi rcx, rdx\n" };
			t.init = [ ]( state& s ) { s.gpr[ RBX ] = 0; s.gpr[ RDX ] = 8; };
			t.flag_mask = 0x8C1;             // CF ZF SF OF (AF PF undefined)
			v.push_back( t );
			test_case t2{ "83a3a20e59", "blsi CF with a non-zero source", "blsi rcx, rdx\n" };
			t2.init = [ ]( state& s ) { s.gpr[ RDX ] = 0x80; };
			t2.flag_mask = 0x8C1;
			v.push_back( t2 );
		}
		v.push_back( { "fe12f7c8a8", "pushfq after std + add, then popfq/cld round trip", "std\nadd rax, rbx\npushfq\ncld\npopfq\npushfq\n" } );
		v.push_back( { "c25f69595a", "sahf keeps OF", "mov al, 0x7f\nadd al, 1\nmov ah, 0xD5\nsahf\n" } );
		for ( const char* rc : { "rcl al, 9", "rcl al, 10", "rcr ax, 17", "rcr ax, 18", "rcl eax, 33", "rcr rax, 65", "rcl bl, 1", "rcr bx, 1" } )
		{
			static std::vector<std::string> keep;
			keep.push_back( std::string( "stc\n" ) + rc + "\n" );
			test_case t{ "e38d0afade", rc, keep.back() };
			t.init = [ ]( state& s ) { s.gpr[ RAX ] = 0x8181818181818181ull; s.gpr[ RBX ] = 0x8001; };
			t.flag_mask = 0x1;               // CF (OF is undefined for counts other than 1)
			v.push_back( t );
		}
		{
			test_case t{ "3afc6539a8", "pop qword/word to memory, pop [rsp]", "" };
			t.asm_text = "push rbx\npop qword ptr [rdi]\npush rcx\npop word ptr [rdi + 16]\npush rcx\npush rdx\npop qword ptr [rsp]\n";
			t.init = [ ]( state& s ) { s.gpr[ RDI ] = SCRATCH; };
			v.push_back( t );
		}
		v.push_back( raw( "336dbe8e99", "REX before 66 is ignored: 48 66 89 C3 = mov bx, ax", { 0x48, 0x66, 0x89, 0xC3 } ) );
		v.push_back( raw( "336dbe8e99", "66 then REX.W: 66 48 89 C3 = mov rbx, rax", { 0x66, 0x48, 0x89, 0xC3 } ) );
		v.push_back( { "7bdcdf1141", "vpinsrb with reg 4 (esp, not ah) under VEX", "vpinsrb xmm1, xmm2, esp, 3\nvpinsrb xmm3, xmm4, ebp, 9\n" } );
		return v;
	}
	std::vector<test_case> avx_cases() { return {}; }
	std::vector<test_case> x87_cases() { return {}; }
	// ── exceptions (agent analysis: scratchpad\bp\exceptions.md) ─────────────────────────────
	// 60efba3c1b / 69cb498c56 / 6dd7d8c649 fix IDT-delivery code that Unicorn never reaches (it hands
	// every exception to UC_HOOK_INTR), so they are recorded as unreachable; the analysis surfaced
	// ICEBP instead, which is reachable and wrong.
	std::vector<test_case> exception_cases()
	{
		std::vector<test_case> v;
		// 73fb7b3c49: ICEBP / INT1 (F1) is a trap-like #DB: RIP after the instruction, no #UD
		{
			test_case t{ "73fb7b3c49", "icebp (F1) -> trap-like #DB, rip after the instruction", "" };
			t.bytes = { 0x48, 0xFF, 0xC0, 0xF1, 0x48, 0xFF, 0xC0 };       // inc rax; icebp; inc rax
			v.push_back( t );
		}
		return v;
	}
}
