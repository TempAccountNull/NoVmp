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
	// ── AVX / SSE decoding (agent analysis: scratchpad\bp\avx.md) ─────────────────────────────
	static void set_dwords( state& s, int reg, std::initializer_list<uint32_t> d )   // fills from bit 0 upwards (ymm)
	{
		uint32_t v[ 8 ] = {};
		int i = 0;
		for ( uint32_t x : d ) v[ i++ ] = x;
		std::memcpy( s.fx + 160 + reg * 16, v, 16 );
		std::memcpy( s.ymmh[ reg ], v + 4, 16 );
	}
	static void set_qwords( state& s, int reg, std::initializer_list<uint64_t> q )
	{
		uint64_t v[ 4 ] = {};
		int i = 0;
		for ( uint64_t x : q ) v[ i++ ] = x;
		std::memcpy( s.fx + 160 + reg * 16, v, 16 );
		std::memcpy( s.ymmh[ reg ], v + 2, 16 );
	}
	static test_case hwb( const char* commit, const char* name, std::vector<uint8_t> b, std::function<void( state& )> init = {}, uc_cpu_x86 model = UC_CPU_X86_HASWELL )
	{
		test_case t{ commit, name, "" };
		t.bytes = std::move( b );
		t.init = std::move( init );
		t.model = model;
		return t;
	}

	std::vector<test_case> avx_cases()
	{
		std::vector<test_case> v;

		// ac63755b20: VSIB index register 4 (xmm4/ymm4) is a real index, not "no index"
		auto vsib_init = [ ]( state& s ) {
			s.gpr[ RSI ] = SCRATCH;
			for ( int i = 0; i < 8; ++i ) { uint32_t d = 0x11111111u * uint32_t( i + 1 ); std::memcpy( s.scratch + i * 4, &d, 4 ); }
			for ( int i = 0; i < 8; ++i ) { uint64_t q = 0x0101010101010101ull * uint64_t( i + 1 ); std::memcpy( s.scratch + 0x40 + i * 8, &q, 8 ); }
			set_dwords( s, 0, { 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF } );
			set_dwords( s, 1, { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u } );
			set_dwords( s, 4, { 3, 1, 2, 0, 7, 6, 5, 4 } );
			set_dwords( s, 12, { 7, 6, 5, 4, 3, 2, 1, 0 } );
		};
		v.push_back( hwb( "ac63755b20", "vpgatherdd xmm0, [rsi+xmm4*4], xmm1", { 0xC4, 0xE2, 0x71, 0x90, 0x04, 0xA6 }, vsib_init ) );
		v.push_back( hwb( "ac63755b20", "vpgatherdd ymm0, [rsi+ymm4*4], ymm1", { 0xC4, 0xE2, 0x75, 0x90, 0x04, 0xA6 }, vsib_init ) );
		v.push_back( hwb( "ac63755b20", "vpgatherqq ymm0, [rsi+ymm4*8], ymm1", { 0xC4, 0xE2, 0xF5, 0x91, 0x04, 0xE6 },
						  [ vsib_init ]( state& s ) { vsib_init( s ); s.gpr[ RSI ] = SCRATCH + 0x40; set_qwords( s, 4, { 3, 0, 2, 1 } ); } ) );
		v.push_back( hwb( "ac63755b20", "vpgatherdd dest == index (xmm4) -> #UD", { 0xC4, 0xE2, 0x71, 0x90, 0x24, 0xA6 }, vsib_init ) );
		v.push_back( hwb( "ac63755b20", "vpgatherdd mask == index (xmm4) -> #UD", { 0xC4, 0xE2, 0x59, 0x90, 0x04, 0xA6 }, vsib_init ) );
		v.push_back( hwb( "ac63755b20", "vpgatherdd ymm0, [rsi+ymm12*4], ymm1 (control)", { 0xC4, 0xA2, 0x75, 0x90, 0x04, 0xA6 }, vsib_init ) );
		v.push_back( hwb( "ac63755b20", "vpgatherdd xmm0 partial mask", { 0xC4, 0xE2, 0x71, 0x90, 0x04, 0xA6 },
						  [ vsib_init ]( state& s ) { vsib_init( s ); set_dwords( s, 1, { ~0u, 0, ~0u, 0 } ); } ) );

		// 5e3572ef2e: VSIB addresses are masked to the address size (addr32 wraps at 4 GiB)
		{
			std::vector<uint8_t> low( 0x1000 );
			for ( int i = 0; i < 0x1000; ++i ) low[ i ] = uint8_t( 0xA0 + ( i & 0x3F ) );
			auto m_init = [ ]( state& s ) {
				s.gpr[ RSI ] = 0x00000000F0000000ull;
				set_dwords( s, 1, { ~0u, ~0u, ~0u, ~0u } );
				set_dwords( s, 2, { 0x04400000, 0x04400001, 0x04400002, 0x04400003 } );
			};
			test_case t = hwb( "5e3572ef2e", "addr32 vpgatherdd xmm0, [esi+xmm2*4] wraps to 0x01000000", { 0x67, 0xC4, 0xE2, 0x71, 0x90, 0x04, 0x96 }, m_init );
			t.extra_mem = { { 0x01000000, low } };
			v.push_back( t );
			test_case t2 = hwb( "5e3572ef2e", "addr32 vpgatherdd with negative indices (control)", { 0x67, 0xC4, 0xE2, 0x71, 0x90, 0x04, 0x96 },
								[ ]( state& s ) { s.gpr[ RSI ] = 0x01000010; set_dwords( s, 1, { ~0u, ~0u, ~0u, ~0u } ); set_dwords( s, 2, { 0xFFFFFFFC, 0xFFFFFFFD, 0xFFFFFFFE, 0xFFFFFFFF } ); } );
			t2.extra_mem = { { 0x01000000, low } };
			v.push_back( t2 );
			test_case t3 = hwb( "5e3572ef2e", "addr32 vpgatherqq ymm0, [esi+ymm2*8] wraps to 0x01000000", { 0x67, 0xC4, 0xE2, 0xF5, 0x91, 0x04, 0xD6 },
								[ ]( state& s ) { s.gpr[ RSI ] = 0xF0000000ull; set_dwords( s, 1, { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u } ); set_qwords( s, 2, { 0x02200000, 0x02200001, 0x02200002, 0x02200003 } ); } );
			t3.extra_mem = { { 0x01000000, low } };
			v.push_back( t3 );
		}

		// 2eb8d97343: VEX.L=1 forms of VMOVQ/VMOVD/VMOVLPD are #UD
		auto l_init = [ ]( state& s ) { s.gpr[ RAX ] = 0x1122334455667788ull; s.gpr[ RSI ] = SCRATCH; set_dwords( s, 0, { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u } ); };
		struct { const char* n; std::vector<uint8_t> b; } lcases[] = {
			{ "VEX.L=1 vmovq xmm0, xmm1 (F3 0F 7E) -> #UD", { 0xC5, 0xFE, 0x7E, 0xC1 } },
			{ "VEX.L=1 vmovq xmm0, [rsi] -> #UD", { 0xC5, 0xFE, 0x7E, 0x06 } },
			{ "VEX.L=1 vmovq xmm1, xmm0 (66 0F D6) -> #UD", { 0xC5, 0xFD, 0xD6, 0xC1 } },
			{ "VEX.L=1 vmovd xmm0, eax -> #UD", { 0xC5, 0xFD, 0x6E, 0xC0 } },
			{ "VEX.L=1 vmovq xmm0, rax -> #UD", { 0xC4, 0xE1, 0xFD, 0x6E, 0xC0 } },
			{ "VEX.L=1 66 0F 12 reg -> #UD", { 0xC5, 0xFD, 0x12, 0xC1 } },
			{ "vmovq xmm0, xmm1 (L=0 control)", { 0xC5, 0xFA, 0x7E, 0xC1 } },
			{ "vmovq xmm1, xmm0 (L=0 control)", { 0xC5, 0xF9, 0xD6, 0xC1 } },
			{ "vmovd xmm0, eax (L=0 control)", { 0xC5, 0xF9, 0x6E, 0xC0 } },
			{ "vmovq xmm0, rax (L=0 control)", { 0xC4, 0xE1, 0xF9, 0x6E, 0xC0 } },
			{ "vmovlpd reg-reg (C5 F9 12 C1) -> #UD on hardware", { 0xC5, 0xF9, 0x12, 0xC1 } },
			{ "movlpd reg-reg (66 0F 12 C1) -> #UD on hardware", { 0x66, 0x0F, 0x12, 0xC1 } },
			{ "movhpd reg-reg (66 0F 16 C1) -> #UD on hardware", { 0x66, 0x0F, 0x16, 0xC1 } },
			{ "vmovhpd reg-reg (C5 F1 16 C2) -> #UD on hardware", { 0xC5, 0xF1, 0x16, 0xC2 } },
			{ "movhlps xmm0, xmm1 (0F 12 C1, control)", { 0x0F, 0x12, 0xC1 } },
			{ "movlhps xmm0, xmm1 (0F 16 C1, control)", { 0x0F, 0x16, 0xC1 } },
		};
		for ( auto& c : lcases ) v.push_back( hwb( "2eb8d97343", c.n, c.b, l_init ) );

		// d66532600f: dpps/dppd (rename only) - regression
		auto dp_init = [ ]( state& s ) {
			float a[ 4 ] = { 1, 2, 3, 4 }, b[ 4 ] = { 5, 6, 7, 8 };
			std::memcpy( s.fx + 160, a, 16 ); std::memcpy( s.fx + 176, b, 16 );
			std::memcpy( s.fx + 192, b, 16 ); std::memcpy( s.ymmh[ 2 ], b, 16 ); std::memcpy( s.ymmh[ 1 ], a, 16 );
		};
		v.push_back( hwb( "d66532600f", "dpps xmm0, xmm1, 0xF1", { 0x66, 0x0F, 0x3A, 0x40, 0xC1, 0xF1 }, dp_init ) );
		v.push_back( hwb( "d66532600f", "vdpps ymm0, ymm1, ymm2, 0x71", { 0xC4, 0xE3, 0x75, 0x40, 0xC2, 0x71 }, dp_init ) );
		v.push_back( hwb( "d66532600f", "dppd xmm0, xmm1, 0x31", { 0x66, 0x0F, 0x3A, 0x41, 0xC1, 0x31 },
						  [ ]( state& s ) { double a[ 2 ] = { 1, 2 }, b[ 2 ] = { 3, 4 }; std::memcpy( s.fx + 160, a, 16 ); std::memcpy( s.fx + 176, b, 16 ); } ) );
		v.push_back( hwb( "d66532600f", "vdppd ymm (L=1) -> #UD", { 0xC4, 0xE3, 0x75, 0x41, 0xC2, 0x31 }, dp_init ) );

		// 73dd6e4a36: legacy SSE m128 operands must be 16-byte aligned (#GP), except the unaligned forms
		struct { const char* n; std::vector<uint8_t> b; } al[] = {
			{ "andps", { 0x0F, 0x54, 0x06 } }, { "unpcklps", { 0x0F, 0x14, 0x06 } }, { "shufps", { 0x0F, 0xC6, 0x06, 0x1B } },
			{ "cvtdq2ps", { 0x0F, 0x5B, 0x06 } }, { "addsubpd", { 0x66, 0x0F, 0xD0, 0x06 } }, { "haddpd", { 0x66, 0x0F, 0x7C, 0x06 } },
			{ "movsldup", { 0xF3, 0x0F, 0x12, 0x06 } }, { "ptest", { 0x66, 0x0F, 0x38, 0x17, 0x06 } }, { "blendvps", { 0x66, 0x0F, 0x38, 0x14, 0x16 } },
			{ "phminposuw", { 0x66, 0x0F, 0x38, 0x41, 0x06 } }, { "aesenc", { 0x66, 0x0F, 0x38, 0xDC, 0x06 } }, { "roundps", { 0x66, 0x0F, 0x3A, 0x08, 0x06, 0x00 } },
			{ "dpps", { 0x66, 0x0F, 0x3A, 0x40, 0x06, 0xFF } }, { "pclmulqdq", { 0x66, 0x0F, 0x3A, 0x44, 0x06, 0x00 } },
			// controls that always fault when misaligned
			{ "addps", { 0x0F, 0x58, 0x06 } }, { "pshufb", { 0x66, 0x0F, 0x38, 0x00, 0x06 } }, { "movaps load", { 0x0F, 0x28, 0x06 } },
			{ "movaps store", { 0x0F, 0x29, 0x06 } }, { "movdqa", { 0x66, 0x0F, 0x6F, 0x06 } }, { "movntdqa", { 0x66, 0x0F, 0x38, 0x2A, 0x06 } },
			{ "vmovaps", { 0xC5, 0xF8, 0x28, 0x06 } },
			// must never fault
			{ "movups store", { 0x0F, 0x11, 0x06 } }, { "movupd store", { 0x66, 0x0F, 0x11, 0x06 } }, { "movups load", { 0x0F, 0x10, 0x06 } },
			{ "movupd load", { 0x66, 0x0F, 0x10, 0x06 } }, { "movdqu load", { 0xF3, 0x0F, 0x6F, 0x06 } }, { "movdqu store", { 0xF3, 0x0F, 0x7F, 0x06 } },
			{ "lddqu", { 0xF2, 0x0F, 0xF0, 0x06 } }, { "pcmpistri", { 0x66, 0x0F, 0x3A, 0x63, 0x06, 0x0C } }, { "pcmpestri", { 0x66, 0x0F, 0x3A, 0x61, 0x06, 0x0C } },
			{ "addss", { 0xF3, 0x0F, 0x58, 0x06 } }, { "addsd", { 0xF2, 0x0F, 0x58, 0x06 } }, { "movsd", { 0xF2, 0x0F, 0x10, 0x06 } },
			{ "movq", { 0xF3, 0x0F, 0x7E, 0x06 } }, { "movlps", { 0x0F, 0x12, 0x06 } }, { "cvtps2pd", { 0x0F, 0x5A, 0x06 } },
			{ "comiss", { 0x0F, 0x2F, 0x06 } }, { "vandps", { 0xC5, 0xF0, 0x54, 0x06 } }, { "vaddps ymm", { 0xC5, 0xF4, 0x58, 0x06 } },
			{ "vptest", { 0xC4, 0xE2, 0x79, 0x17, 0x06 } }, { "vdpps", { 0xC4, 0xE3, 0x71, 0x40, 0x06, 0xFF } },
		};
		static std::deque<std::string> al_names;
		for ( auto& c : al )
			for ( int off : { 0, 8 } )
			{
				al_names.push_back( std::string( c.n ) + ( off ? " [rsi] misaligned (+8)" : " [rsi] aligned" ) );
				v.push_back( hwb( "73dd6e4a36", al_names.back().c_str(), c.b,
								  [ off ]( state& s ) { s.gpr[ RSI ] = SCRATCH + off; s.gpr[ RAX ] = 16; s.gpr[ RDX ] = 16; } ) );
			}
		v.push_back( hwb( "73dd6e4a36", "maskmovdqu to a misaligned rdi", { 0x66, 0x0F, 0xF7, 0xC1 },
						  [ ]( state& s ) { s.gpr[ RDI ] = SCRATCH + 8; uint8_t m[ 16 ]; for ( int i = 0; i < 16; ++i ) m[ i ] = ( i & 1 ) ? 0x00 : 0x80; s.set_xmm( 1, m ); } ) );

		// ce0ee66044: EXTRQ_i (AMD SSE4A; the host is Intel) - AMD APM vectors in Unicorn on EPYC
		auto ex_init = [ ]( state& s ) {
			uint64_t x0[ 2 ] = { 0xFEDCBA9876543210ull, 0x1111111111111111ull }, x1[ 2 ] = { 0x0123456789ABCDEFull, 0x2222222222222222ull };
			s.set_xmm( 0, x0 ); s.set_xmm( 1, x1 );
		};
		auto q0 = [ ]( const result& r, int reg ) { uint64_t q; std::memcpy( &q, r.s.xmm( reg ), 8 ); return q; };
		auto ex = [ & ]( const char* n, std::vector<uint8_t> b, std::function<bool( const result&, std::string& )> e, std::function<void( state& )> extra = {} ) {
			test_case t = hwb( "ce0ee66044", n, std::move( b ), [ ex_init, extra ]( state& s ) { ex_init( s ); if ( extra ) extra( s ); }, UC_CPU_X86_EPYC );
			t.hardware = false;
			t.expect = std::move( e );
			v.push_back( t );
		};
		auto want_q0 = [ q0 ]( int reg, uint64_t want, int other, uint64_t other_want ) {
			return [ = ]( const result& r, std::string& why ) {
				if ( r.faulted ) { why = "faulted, vector " + std::to_string( r.vector ); return false; }
				if ( q0( r, reg ) != want ) { why = "xmm" + std::to_string( reg ) + ".q0 " + hx( q0( r, reg ) ) + ", expected " + hx( want ); return false; }
				if ( q0( r, other ) != other_want ) { why = "xmm" + std::to_string( other ) + ".q0 changed: " + hx( q0( r, other ) ); return false; }
				return true;
			};
		};
		auto want_ud = [ ]( const result& r, std::string& why ) { if ( r.faulted && r.vector == 6 ) return true; why = r.faulted ? "vector " + std::to_string( r.vector ) : "executed, expected #UD"; return false; };
		ex( "extrq xmm1, 16, 8 (66 0F 78 C1 10 08)", { 0x66, 0x0F, 0x78, 0xC1, 0x10, 0x08 }, want_q0( 1, 0xABCD, 0, 0xFEDCBA9876543210ull ) );
		ex( "extrq xmm1, 60, 4 (66 0F 78 C1 3C 04)", { 0x66, 0x0F, 0x78, 0xC1, 0x3C, 0x04 }, want_q0( 1, 0x00123456789ABCDEull, 0, 0xFEDCBA9876543210ull ) );
		ex( "extrq /1 (66 0F 78 C9) -> #UD", { 0x66, 0x0F, 0x78, 0xC9, 0x10, 0x08 }, want_ud );
		ex( "extrq memory form (mod=01) -> #UD", { 0x66, 0x0F, 0x78, 0x40, 0x10, 0x08 }, want_ud, [ ]( state& s ) { s.gpr[ RAX ] = SCRATCH; } );
		ex( "insertq xmm0, xmm1, 16, 8 (control)", { 0xF2, 0x0F, 0x78, 0xC1, 0x10, 0x08 }, want_q0( 0, 0xFEDCBA9876CDEF10ull, 1, 0x0123456789ABCDEFull ) );
		ex( "extrq xmm0, xmm1 register form (control)", { 0x66, 0x0F, 0x79, 0xC1 }, want_q0( 0, 0x5432, 1, 0x0123456789AB0810ull ),
			[ ]( state& s ) { uint64_t x1[ 2 ] = { 0x0123456789AB0810ull, 0x2222222222222222ull }; s.set_xmm( 1, x1 ); } );
		ex( "F3 66 0F 79 C1 -> #UD", { 0xF3, 0x66, 0x0F, 0x79, 0xC1 }, want_ud );
		ex( "F3 0F 78 C1 -> #UD", { 0xF3, 0x0F, 0x78, 0xC1, 0x10, 0x08 }, want_ud );
		{
			test_case t = hwb( "ce0ee66044", "extrq on the Intel host -> #UD (harness sanity)", { 0x66, 0x0F, 0x78, 0xC1, 0x10, 0x08 }, ex_init );
			v.push_back( t );   // hardware compare: Haswell model lacks SSE4A -> #UD on both
		}

		// 2b55e479e6 (already in 7.2.22): VCOMI/COMI read only 4/8 bytes - no fault at a page end
		{
			std::vector<uint8_t> page( 0x1000, 0x3F );
			struct { const char* n; std::vector<uint8_t> b; uint64_t rsi; } vc[] = {
				{ "vcomiss xmm0, [page_end-4]", { 0xC5, 0xF8, 0x2F, 0x06 }, 0x02000FFC },
				{ "comisd xmm0, [page_end-8]", { 0x66, 0x0F, 0x2F, 0x06 }, 0x02000FF8 },
				{ "ucomiss xmm0, [page_end-4]", { 0x0F, 0x2E, 0x06 }, 0x02000FFC },
			};
			for ( auto& c : vc )
			{
				uint64_t rsi = c.rsi;
				test_case t = hwb( "2b55e479e6", c.n, c.b, [ rsi ]( state& s ) { s.gpr[ RSI ] = rsi; } );
				t.extra_mem = { { 0x02000000, page } };
				v.push_back( t );
			}
		}

		// e000687f12: VEX.W must match for W0/W1-only instructions; legacy 66 0F 38 0C..0F are #UD
		auto w_init = [ ]( state& s ) { s.gpr[ RSI ] = SCRATCH; set_dwords( s, 1, { 1, 2, 3, 4, 5, 6, 7, 8 } ); set_dwords( s, 2, { 9, 10, 11, 12, 13, 14, 15, 16 } ); };
		struct { const char* n; std::vector<uint8_t> b; } wc[] = {
			{ "vpermq W0 -> #UD", { 0xC4, 0xE3, 0x7D, 0x00, 0xC1, 0x1B } }, { "vpermq W1 (control)", { 0xC4, 0xE3, 0xFD, 0x00, 0xC1, 0x1B } },
			{ "vpblendd W1 -> #UD", { 0xC4, 0xE3, 0xF1, 0x02, 0xC2, 0x05 } }, { "vpblendd W0 (control)", { 0xC4, 0xE3, 0x71, 0x02, 0xC2, 0x05 } },
			{ "vpbroadcastd W1 -> #UD", { 0xC4, 0xE2, 0xF9, 0x58, 0xC1 } }, { "vpbroadcastd W0 (control)", { 0xC4, 0xE2, 0x79, 0x58, 0xC1 } },
			{ "vblendvps W1 -> #UD", { 0xC4, 0xE3, 0xF1, 0x4A, 0xC2, 0x30 } }, { "vmaskmovps W1 -> #UD", { 0xC4, 0xE2, 0xF1, 0x2C, 0x06 } },
			{ "vpermilps W1 -> #UD", { 0xC4, 0xE2, 0xF1, 0x0C, 0xC2 } }, { "vtestps W1 -> #UD", { 0xC4, 0xE2, 0xF9, 0x0E, 0xC1 } },
			{ "vcvtph2ps W1 -> #UD", { 0xC4, 0xE2, 0xF9, 0x13, 0xC1 } }, { "legacy 66 0F 38 0C (no VEX) -> #UD", { 0x66, 0x0F, 0x38, 0x0C, 0xC1 } },
			{ "legacy 66 0F 38 0E (no VEX) -> #UD", { 0x66, 0x0F, 0x38, 0x0E, 0xC1 } }, { "vpsrlvd W0 (control)", { 0xC4, 0xE2, 0x71, 0x45, 0xC2 } },
			{ "vpsrlvq W1 (control)", { 0xC4, 0xE2, 0xF1, 0x45, 0xC2 } },
		};
		for ( auto& c : wc ) v.push_back( hwb( "e000687f12", c.n, c.b, w_init ) );
		return v;
	}
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
