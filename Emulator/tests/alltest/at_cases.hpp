// emu-alltest --cases FILE: hand-written snippets with a chosen input state. Two kinds of case:
//   * hardware case  (no "=>"): runs on the host CPU (self-generated snippets only) and on Unicorn
//     UC_CPU_X86_MAX; prints each engine's fault and every field that changed, then the fields
//     where the two engines differ ("hw:" / "uc:" / "uc vs hw:").
//   * expected-value case ("=>", plan 1.15a "SDM vectors"): never runs natively. Only Unicorn runs
//     it and its result is compared to the expectations written after "=>" ("exp:" / "uc:" /
//     "uc vs exp:"). For instructions the host CPU lacks (AVX-512, SHA512, SM3, SM4, AMX, ...) or
//     encodings Keystone/Capstone cannot produce (use ".byte"). Expected values come from the SDM.
//
// One case per line:
//   <asm> [| <inputs>] [=> <expectations>]      or      <asm> [| <inputs>] [=>! <expectations>]
//   <asm>          instructions separated by ';' (Keystone syntax), or raw bytes ".byte 0x0f, 0x0b"
//   <inputs>       assignments separated by spaces (below)
//   <expectations> the same assignments plus at most one fault token; separated by spaces
// Lines starting with '#' and empty lines are skipped.
//
// Assignments (inputs and expectations):
//   rax..r15=V  rflags=V  mxcsr=V  fcw=V  fsw=V  ftw=V (abridged tag byte)       V: C literal (0x.., dec)
//   st0..st7=SEXP:MANT     FXSAVE register slot i (hex), sets the slot's abridged tag bit
//   xmm0..xmm31=HEX        bits 127:0, up to 32 hex digits, memory byte order (low byte first)
//   ymmh0..ymmh31=HEX      bits 255:128 (upper YMM half), up to 32 hex digits, low byte first
//   zmmh0..zmmh31=HEX      bits 511:256 (upper ZMM half), up to 64 hex digits, low byte first
//   zmm0..zmm31=HEX        bits 511:0, up to 128 hex digits, low byte first (fewer digits: only
//                          the low bytes given are set; the rest keep their value)
//   k0..k7=V               AVX-512 opmask registers (64-bit)
//   r16..r31=V             Intel APX extended GPRs (64-bit; needs --apx, Unicorn only like k*)
//   m+OFF=HEXBYTES         operand memory at MEM + OFF (RSI/R14 = MEM + 0x8000, RDI = MEM + 0x9000)
//   cpl=3                  (inputs only, a case option) Unicorn runs the thunk at CPL3 like the host:
//                          Windows x64 GDT (23h code32 DPL3 = compatibility mode, 2Bh data DPL3,
//                          33h code64 DPL3), entered by IRETQ from a CPL0 stub, IA32_EFER.SCE = 1.
//                          A snippet may far-transfer to CS = 23h and back to 33h (heaven's gate);
//                          a fault taken in compatibility mode resumes at the 64-bit epilogue in
//                          both engines. Without it Unicorn runs at CPL0 (no GDT).
// MXCSR is also settable from the snippet itself ("ldmxcsr [rsi] | m+0x8000=801F0000").
// Default input state: GPRs 0 except RSP/RSI/RDI/R14, RFLAGS 0x202, FCW 037F, MXCSR 1F80, all else 0.
//
// ZMM/opmask state (zmm*, zmmh*, k*, xmm16-31, ymmh16-31) exists only in expected-value cases: the
// host i5-13600K has no AVX-512, so the native thunk cannot load it (a hardware case using those
// keys is rejected). Unicorn moves it with uc_reg_write/uc_reg_read (UC_X86_REG_ZMM0-31,
// UC_X86_REG_K0-7) in code hooks at the snippet's first instruction and at the epilogue.
//
// Expectations:
//   fault token   #DE #DB #NMI #BP #OF #BR #UD #NM #DF #TS #NP #SS #GP #PF #MF #AC #MC #XM #VE #CP,
//                 or #N (decimal vector). No fault token = the case must complete without a fault.
//                 U772: an error code may follow in parentheses, "#GP(0)", "#SS(0)", "#CP(2)",
//                 "#GP(0x30)" (C literal): Unicorn's error code (UC_CTL_X86_EXCEPTION) must then
//                 be that value; without one only the vector is compared.
//                 On a fault the state is saved at the faulting instruction (as in hardware cases).
//   "=>"  strict  the expected state is the input state with the listed assignments applied, and
//                 EVERY checked field must match it: whatever is not listed must be unchanged.
//   "=>!" loose   only the listed fields (the exact bytes the assignments write) and the fault
//                 outcome are checked; everything else is ignored (for undefined flags, random
//                 values such as RDRAND, scratch registers, ...).
//   Checked fields (strict): GPRs, RFLAGS, FCW, FSW, abridged FTW, MXCSR, ST0-7 (10 bytes each),
//   XMM/YMM/ZMM0-31, K0-7, the 64 KiB operand memory. Not checked: the x87 last-instruction
//   pointers (FOP/FIP/FDP), MXCSR_MASK, the FNSTENV image, the test stack.
//   Examples:
//     add rax, rcx | rax=0xFFFFFFFFFFFFFFFF rcx=1 => rax=0 rflags=0x257
//     mov qword ptr [rsi+8], rax | rax=0x0807060504030201 => m+0x8008=0102030405060708
//     .byte 0x0f, 0x0b => #UD
//     rdrand rax =>! rflags=0x203
//   An empty expectation ("nop =>") means: no fault, nothing changed.
//
// Options: --expect-only skips every line without "=>". --cpuid FILE / --strict / --no-strict /
// --xcr0 V / --cr0 V configure Unicorn for both kinds of case (U435: a --cpuid profile is strict by
// default, --no-strict writes UC_CTL_X86_CPUID_STRICT = 0).
// Machine state (U540, plan 1.H.2): a hardware case runs Unicorn with this machine's OS-owned
// state: XCR0 = the host's XCR0 (XGETBV ECX = 0, what Windows wrote with XSETBV) and CR0 = 33h
// (Windows x64's CR0 without the bits that cannot act here, see WINDOWS_CR0); an expected-value
// case keeps Unicorn's reset XCR0 / CR0 (the reset XCR0 enables every state component the CPU
// model, the opt-ins and the CPUID profile support). --xcr0 V / --cr0 V (V != 0) override both
// kinds. The effective values are printed first ("machine state: ...").
// --shard K/N (U543) runs only the K-th of N contiguous blocks of the case lines (the N runs together
// are the whole file in order; the case numbers [n] restart in each block).
// U539: Unicorn implements the SDM only, for both kinds of case (no quirk switch; hardware
// deviations are tags, below). --avx512 opts Unicorn in to AVX-512
// (UC_CTL_X86_AVX512 = AVX512F|DQ|BW|VL|CD|IFMA|VPOPCNTDQ|BITALG|VBMI|FP16|VP2INTERSECT|VBMI2|VNNI|BF16, before the engine is
// initialised; reset XCR0 then has 7:5 set) for opmask/EVEX expected-value cases, e.g.
// Emulator\data\cases_opmask.txt. U990: --xeonphi adds the Intel Xeon Phi-only families
// (UC_X86_AVX512_4VNNIW|4FMAPS|ER|PF|PREFETCHWT1, AVX512F implied; --avx512 alone leaves them off)
// for Emulator\data\cases_xeonphi.txt. --amx
// opts in to Intel AMX (UC_CTL_X86_AMX = UC_X86_AMX_ALL) for Emulator\data\cases_amx.txt; the tile
// state itself is not a checked field (the cases store their results to memory). --avx10 N opts in
// to Intel AVX10 version N (UC_CTL_X86_AVX10 = N, 1 or 2; AVX-512 CPUID bits stay off unless
// --avx512 is given too) for Emulator\data\cases_avx10_a.txt. --apx opts in to Intel APX
// (UC_CTL_X86_APX = UC_X86_APX_F; reset XCR0 then has bit 19) for Emulator\data\cases_apx_core.txt:
// the r16..r31 keys move R16-R31 like the ZMM/K state (and are checked fields of a strict case).
// Paired hardware case (U614, APX): "<host asm> ~~ <unicorn asm> | <inputs>" runs the first
// snippet on the host CPU and the second one on Unicorn, then compares the two results as for
// any hardware case - e.g. a legacy instruction on the i5-13600K against its REX2 encoding
// (".byte 0xd5, ...") on Unicorn with --apx (Emulator\data\cases_apx_core_hw.txt).
// Tags (U530, hardware cases only), a trailing comment on the case line:
//   # known deviation: NAME   the i5-13600K deviates from the SDM here and the emulator implements
//                             the SDM; NAME is the docs\quirks.md entry
//   # host state: REASON      the host's state differs from the emulator's (RDRAND values, APIC ID,
//                             CET shadow stacks the host OS leaves off, ...), not an SDM question
// A tagged case that differs is counted under its tag ("KNOWN DEVIATION" / "HOST STATE"), never as
// differing; a tagged case that matches is listed as "not observed" (stale tag, or a CPU that
// sometimes takes the SDM path).
// Output: "[n] SAME|DIFF|KNOWN DEVIATION|HOST STATE <line>", the engines' lines, then
// "cases: N, differing: M, known deviations: K, host state: H" (M = untagged cases of both kinds
// that differ), one line per tag name, and the tagged cases that were not observed; when the file
// has expected-value cases (or --expect-only skipped lines) a second summary line
// "expected-value cases: N, differing: M, errors: E, skipped: S" follows. Exit status 1 when an
// expected-value case differs or cannot be parsed/assembled/run; hardware-case differences never
// fail the run (they are the work list; test.cmd requires "differing: 0" for its hardware files).
#pragma once
#include "at_engine.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <intrin.h>
#include <map>
#include <set>
#include <sstream>
#include <tlhelp32.h>
#include <cwchar>

namespace at
{
	// U540: Windows x64 runs with CR0 = 80050033h (PG|AM|WP|NE|ET|MP|PE). PG cannot be set
	// (Unicorn's memory is a flat map without page tables); WP acts only on supervisor writes
	// through paging. PE|MP|ET|NE = 33h: NE = 1, so a pending unmasked x87 exception raises #MF
	// (not FERR#) like on the host; MP only matters with CR0.TS = 1 (never set here).
	// U834: AM (bit 18) acts since Unicorn raises the alignment-check exception: with RFLAGS.AC = 1
	// at CPL3 (a cpl=3 case) a misaligned data access is #AC on both engines (cases_ac_hw).
	constexpr uint64_t WINDOWS_CR0 = 0x40033;

	// U540: the host's XCR0 (the value the OS wrote with XSETBV); 0 when CPUID.1:ECX.OSXSAVE = 0
	// (XGETBV would #UD, the host has no XCR0)
	static uint64_t host_xcr0()
	{
		int r[ 4 ] = {};
		__cpuid( r, 1 );
		if ( !( r[ 2 ] & ( 1 << 27 ) ) ) return 0;
		return _xgetbv( 0 );
	}

	static void case_default( state& s )
	{
		std::memset( &s, 0, sizeof( s ) );
		s.gpr[ RSP ] = STACK_TOP;
		s.gpr[ RSI ] = MEM_PTR;
		s.gpr[ RDI ] = MEM_DST;
		s.gpr[ R14 ] = MEM_PTR;
		s.rflags = 0x202;
		uint16_t fcw = 0x037F; std::memcpy( s.fx, &fcw, 2 );
		uint32_t mx = 0x1F80, mxmask = 0xFFFF;
		std::memcpy( s.fx + 24, &mx, 4 );
		std::memcpy( s.fx + 28, &mxmask, 4 );
	}

	// mark: optional byte map over 'state' (sizeof( state ) entries), set to 1 for every byte the
	// assignment writes. ext: set when the key needs the Unicorn-only ZMM/K state.
	static bool case_assign( state& s, const std::string& kv, std::string& err, std::vector<uint8_t>* mark = nullptr, bool* ext = nullptr )
	{
		size_t eq = kv.find( '=' );
		if ( eq == std::string::npos ) { err = "no '=' in " + kv; return false; }
		std::string k = kv.substr( 0, eq ), v = kv.substr( eq + 1 );
		auto mk = [ & ]( const void* p, size_t n ) {
			if ( mark ) std::memset( mark->data() + ( ( const uint8_t* ) p - ( const uint8_t* ) &s ), 1, n );
		};
		auto num = [ & ]() { return std::stoull( v, nullptr, 0 ); };
		auto hexbytes = [ & ]( uint8_t* dst, size_t max ) -> size_t {
			size_t n = 0;
			for ( size_t i = 0; i + 1 < v.size() && n < max; i += 2 ) dst[ n++ ] = uint8_t( std::stoul( v.substr( i, 2 ), nullptr, 16 ) );
			mk( dst, n );
			return n;
		};
		// strict hex for the new (ZMM-era) keys: even digit count, at most max bytes
		auto hexstrict = [ & ]( uint8_t* dst, size_t max ) -> bool {
			if ( v.empty() || v.size() % 2 || v.size() > 2 * max || v.find_first_not_of( "0123456789abcdefABCDEF" ) != std::string::npos )
			{
				err = k + " needs 1.." + std::to_string( max ) + " bytes as hex digits (even count), got \"" + v + "\"";
				return false;
			}
			hexbytes( dst, max );
			return true;
		};
		// "<prefix>N" with N in [0, lim)
		auto index = [ & ]( const char* prefix, int lim ) -> int {
			size_t pl = std::strlen( prefix );
			if ( k.size() <= pl || k.compare( 0, pl, prefix ) || k.find_first_not_of( "0123456789", pl ) != std::string::npos ) return -1;
			int i = std::stoi( k.substr( pl ) );
			return i < lim ? i : -2;
		};
		for ( int i = 0; i < 16; i++ )
			if ( k == reg_name( i ) ) { s.gpr[ i ] = num(); mk( &s.gpr[ i ], 8 ); return true; }
		// U614: Intel APX R16-R31 (Unicorn-only state, like k*)
		if ( k.size() == 3 && k[ 0 ] == 'r' && std::isdigit( ( unsigned char ) k[ 1 ] ) && std::isdigit( ( unsigned char ) k[ 2 ] ) )
		{
			int i = std::stoi( k.substr( 1 ) );
			if ( i >= 16 && i <= 31 )
			{
				s.egpr[ i - 16 ] = num(); mk( &s.egpr[ i - 16 ], 8 );
				if ( ext ) *ext = true;
				return true;
			}
		}
		if ( k == "rflags" ) { s.rflags = num(); mk( &s.rflags, 8 ); return true; }
		if ( k == "mxcsr" ) { uint32_t x = uint32_t( num() ); std::memcpy( s.fx + 24, &x, 4 ); mk( s.fx + 24, 4 ); return true; }
		if ( k == "fcw" ) { uint16_t x = uint16_t( num() ); std::memcpy( s.fx, &x, 2 ); mk( s.fx, 2 ); return true; }
		if ( k == "fsw" ) { uint16_t x = uint16_t( num() ); std::memcpy( s.fx + 2, &x, 2 ); mk( s.fx + 2, 2 ); return true; }
		if ( k == "ftw" ) { s.fx[ 4 ] = uint8_t( num() ); mk( s.fx + 4, 1 ); return true; }
		if ( k.size() == 3 && k[ 0 ] == 's' && k[ 1 ] == 't' && k[ 2 ] >= '0' && k[ 2 ] <= '7' )
		{
			int i = k[ 2 ] - '0';
			size_t c = v.find( ':' );
			if ( c == std::string::npos ) { err = "st needs SEXP:MANT"; return false; }
			uint16_t se = uint16_t( std::stoul( v.substr( 0, c ), nullptr, 16 ) );
			uint64_t m = std::stoull( v.substr( c + 1 ), nullptr, 16 );
			std::memcpy( s.fx + 32 + 16 * i, &m, 8 ); std::memcpy( s.fx + 40 + 16 * i, &se, 2 );
			s.fx[ 4 ] |= uint8_t( 1u << i );
			mk( s.fx + 32 + 16 * i, 10 ); mk( s.fx + 4, 1 );
			return true;
		}
		if ( k.size() == 2 && k[ 0 ] == 'k' && k[ 1 ] >= '0' && k[ 1 ] <= '7' )
		{
			int i = k[ 1 ] - '0';
			s.k[ i ] = num(); mk( &s.k[ i ], 8 );
			if ( ext ) *ext = true;
			return true;
		}
		if ( k.rfind( "zmmh", 0 ) == 0 )
		{
			int i = index( "zmmh", 32 );
			if ( i < 0 ) { err = "bad zmmh (zmmh0..zmmh31)"; return false; }
			if ( ext ) *ext = true;
			return hexstrict( i < 16 ? s.zmmh[ i ] : s.zmmx[ i - 16 ] + 32, 32 );
		}
		if ( k.rfind( "zmm", 0 ) == 0 )
		{
			int i = index( "zmm", 32 );
			if ( i < 0 ) { err = "bad zmm (zmm0..zmm31)"; return false; }
			if ( ext ) *ext = true;
			if ( i >= 16 ) return hexstrict( s.zmmx[ i - 16 ], 64 );
			// ZMM0-15 live in three places of the image: XMM (FXSAVE), YMMH, ZMMH
			uint8_t z[ 64 ];
			std::memcpy( z, s.xmm( i ), 16 ); std::memcpy( z + 16, s.ymmh[ i ], 16 ); std::memcpy( z + 32, s.zmmh[ i ], 32 );
			std::vector<uint8_t>* m = mark;
			mark = nullptr;
			if ( !hexstrict( z, 64 ) ) return false;
			mark = m;
			std::memcpy( s.fx + 160 + 16 * i, z, 16 ); std::memcpy( s.ymmh[ i ], z + 16, 16 ); std::memcpy( s.zmmh[ i ], z + 32, 32 );
			size_t n = v.size() / 2;
			mk( s.fx + 160 + 16 * i, std::min<size_t>( n, 16 ) );
			if ( n > 16 ) mk( s.ymmh[ i ], std::min<size_t>( n - 16, 16 ) );
			if ( n > 32 ) mk( s.zmmh[ i ], n - 32 );
			return true;
		}
		if ( k.rfind( "ymmh", 0 ) == 0 )
		{
			int i = std::stoi( k.substr( 4 ) );
			if ( i < 0 || i > 31 ) { err = "bad ymmh"; return false; }
			if ( i >= 16 ) { if ( ext ) *ext = true; return hexstrict( s.zmmx[ i - 16 ] + 16, 16 ); }
			hexbytes( s.ymmh[ i ], 16 );
			return true;
		}
		if ( k.rfind( "xmm", 0 ) == 0 )
		{
			int i = std::stoi( k.substr( 3 ) );
			if ( i < 0 || i > 31 ) { err = "bad xmm"; return false; }
			if ( i >= 16 ) { if ( ext ) *ext = true; return hexstrict( s.zmmx[ i - 16 ], 16 ); }
			hexbytes( s.fx + 160 + 16 * i, 16 );
			return true;
		}
		if ( k.rfind( "m+", 0 ) == 0 )
		{
			uint64_t off = std::stoull( k.substr( 2 ), nullptr, 0 );
			if ( off >= MEM_SIZE ) { err = "m+ offset beyond MEM"; return false; }
			hexbytes( s.mem + off, MEM_SIZE - off );
			return true;
		}
		err = "unknown key " + k;
		return false;
	}

	// every field of 'o' that differs from 'i' (or, for an engine comparison, between two outputs)
	static std::string case_fields( const state& i, const state& o )
	{
		std::ostringstream os;
		auto h = [ & ]( uint64_t v ) { return hx( v ); };
		auto hexs = [ & ]( const uint8_t* p, int n ) {
			char b[ 4 ];
			for ( int k = 0; k < n; k++ ) { std::snprintf( b, sizeof( b ), "%02X", p[ k ] ); os << b; }
		};
		for ( int r = 0; r < 16; r++ )
			if ( i.gpr[ r ] != o.gpr[ r ] ) os << " " << reg_name( r ) << "=" << h( o.gpr[ r ] );
		if ( i.rflags != o.rflags ) os << " rflags=" << h( o.rflags );
		if ( i.mxcsr() != o.mxcsr() ) os << " mxcsr=" << h( o.mxcsr() );
		if ( i.fcw() != o.fcw() ) os << " fcw=" << h( o.fcw() );
		if ( i.fsw() != o.fsw() ) os << " fsw=" << h( o.fsw() );
		if ( i.ftw_abridged() != o.ftw_abridged() ) os << " ftw=" << h( o.ftw_abridged() );
		for ( int r = 0; r < 8; r++ )
			if ( std::memcmp( i.st( r ), o.st( r ), 10 ) )
			{
				uint64_t m; uint16_t se;
				std::memcpy( &m, o.st( r ), 8 ); std::memcpy( &se, o.st( r ) + 8, 2 );
				char b[ 48 ]; std::snprintf( b, sizeof( b ), " st%d=%04X:%016llX", r, se, ( unsigned long long ) m );
				os << b;
			}
		for ( int r = 0; r < 16; r++ )
			if ( std::memcmp( i.xmm( r ), o.xmm( r ), 16 ) ) { os << " xmm" << r << "="; hexs( o.xmm( r ), 16 ); }
		for ( int r = 0; r < 16; r++ )
			if ( std::memcmp( i.ymmh[ r ], o.ymmh[ r ], 16 ) ) { os << " ymmh" << r << "="; hexs( o.ymmh[ r ], 16 ); }
		// AVX-512 state (expected-value cases only; zero on both sides of a hardware case)
		for ( int r = 0; r < 16; r++ )
			if ( std::memcmp( i.zmmh[ r ], o.zmmh[ r ], 32 ) ) { os << " zmmh" << r << "="; hexs( o.zmmh[ r ], 32 ); }
		for ( int r = 0; r < 16; r++ )
			if ( std::memcmp( i.zmmx[ r ], o.zmmx[ r ], 64 ) ) { os << " zmm" << ( r + 16 ) << "="; hexs( o.zmmx[ r ], 64 ); }
		for ( int r = 0; r < 8; r++ )
			if ( i.k[ r ] != o.k[ r ] ) os << " k" << r << "=" << h( o.k[ r ] );
		for ( int r = 0; r < 16; r++ )
			if ( i.egpr[ r ] != o.egpr[ r ] ) os << " r" << ( r + 16 ) << "=" << h( o.egpr[ r ] );
		// the rest of the FXSAVE header (FOP, FIP, FDP, MXCSR_MASK) as raw bytes
		for ( int k : { 6, 8, 16, 28 } )
		{
			int n = k == 6 ? 2 : ( k == 28 ? 4 : 8 );
			if ( std::memcmp( i.fx + k, o.fx + k, n ) )
			{
				uint64_t v = 0; std::memcpy( &v, o.fx + k, n );
				os << " fx+" << k << "=" << h( v );
			}
		}
		// FNSTENV image of the epilogue, its reserved upper words (bytes 2-3, 6-7, 10-11, 26-27)
		// masked: those are checked once by the "fnstenv" cases themselves
		uint8_t ei[ 28 ], eo[ 28 ];
		std::memcpy( ei, i.env, 28 ); std::memcpy( eo, o.env, 28 );
		for ( int k : { 2, 3, 6, 7, 10, 11, 26, 27 } ) ei[ k ] = eo[ k ] = 0;
		if ( std::memcmp( ei, eo, 28 ) ) { os << " env="; hexs( o.env, 28 ); }
		for ( uint64_t a = 0; a < MEM_SIZE; )
		{
			if ( i.mem[ a ] == o.mem[ a ] ) { ++a; continue; }
			uint64_t b = a;
			while ( b < MEM_SIZE && b - a < 32 && i.mem[ b ] != o.mem[ b ] ) ++b;
			os << " m+" << h( a ) << "=";
			hexs( o.mem + a, int( b - a ) );
			a = b;
		}
		return os.str();
	}

	// "LEAF SUB EAX EBX ECX EDX" per line (hex), '#' comments: Emulator\data\cpuid_*.txt
	static std::vector<uc_x86_cpuid> load_cpuid_profile( const std::string& path )
	{
		std::vector<uc_x86_cpuid> v;
		std::ifstream f( path );
		std::string line;
		while ( std::getline( f, line ) )
		{
			if ( line.empty() || line[ 0 ] == '#' ) continue;
			uc_x86_cpuid e{};
			std::istringstream is( line );
			is >> std::hex >> e.leaf >> e.subleaf >> e.eax >> e.ebx >> e.ecx >> e.edx;
			if ( is ) v.push_back( e );
		}
		return v;
	}

	// xcr0 / cr0: explicit overrides (--xcr0 V / --cr0 V) for both kinds of case; 0 = automatic
	// (U540: hardware cases host XCR0 / WINDOWS_CR0, expected-value cases Unicorn's reset value).
	// Never PG in cr0 (flat map).
	// strict: -1 = not written (Unicorn's default: on while a profile is installed, U435),
	// 0 = --no-strict, 1 = --strict
	struct case_opts { std::vector<uc_x86_cpuid> cpuid; int strict = -1; uint64_t xcr0 = 0; uint64_t cr0 = 0; bool expect_only = false; int avx512 = 0; int amx = 0; int avx10 = 0; int apx = 0;
					   int shard_k = 0, shard_n = 0;   /* U543: --shard K/N (0 = the whole file) */
					   int rdrand = UC_X86_RDRAND_SEEDED; uint64_t rdrand_seed = 0;   /* U835: --seeded [--rdrand-seed N] / --HostSeed */
					   int hw_repeat = 1; std::string hw_cpu; int hw_load = 0;   /* U1050: --hw-repeat N / --hw-cpu SPEC / --hw-load N */
					   std::string sde_out, sde_in;   /* U1040: --native-under-sde FILE (pass 1) / --sde-results FILE (pass 2) */ };

	// U1040: one --native-under-sde result per expected-value line: "L <line> <hash> run <faulted> <vector>
	// [OFF:HEX ...]" (the bytes of 'state' that differ from the input state), "L <line> <hash> skip
	// <reason>" or "L <line> <hash> error <message>"; <line> = index among the file's case lines,
	// <hash> = FNV-1a of the line (so a results file of another case file or an edited line is noticed)
	struct sde_rec { int kind = 0; /* 1 run, 2 not checked, 3 error */ uint64_t hash = 0; bool faulted = false; int vector = -1; std::string text; };
	static uint64_t fnv1a( const std::string& s )
	{
		uint64_t h = 0xCBF29CE484222325ull;
		for ( unsigned char c : s ) { h ^= c; h *= 0x100000001B3ull; }
		return h;
	}
	static std::string state_diff( const state& in, const state& out )
	{
		const uint8_t* a = ( const uint8_t* ) &in;
		const uint8_t* b = ( const uint8_t* ) &out;
		std::string s;
		char t[ 4 ];
		for ( size_t i = 0; i < sizeof( state ); )
		{
			if ( a[ i ] == b[ i ] ) { ++i; continue; }
			size_t j = i;
			while ( j < sizeof( state ) && j - i < 256 && a[ j ] != b[ j ] ) ++j;
			s += " " + std::to_string( i ) + ":";
			for ( size_t k = i; k < j; ++k ) { std::snprintf( t, sizeof( t ), "%02X", b[ k ] ); s += t; }
			i = j;
		}
		return s;
	}
	static bool apply_diff( state& s, const std::string& d )
	{
		uint8_t* p = ( uint8_t* ) &s;
		std::istringstream is( d );
		for ( std::string tok; is >> tok; )
		{
			const size_t c = tok.find( ':' );
			if ( c == std::string::npos || ( tok.size() - c - 1 ) % 2 ) return false;
			const size_t off = std::stoull( tok.substr( 0, c ) ), n = ( tok.size() - c - 1 ) / 2;
			if ( off + n > sizeof( state ) ) return false;
			for ( size_t k = 0; k < n; ++k ) p[ off + k ] = uint8_t( std::stoul( tok.substr( c + 1 + 2 * k, 2 ), nullptr, 16 ) );
		}
		return true;
	}
	static bool load_sde_results( const std::string& path, std::map<size_t, sde_rec>& recs, std::vector<std::string>& header )
	{
		std::ifstream f( path );
		if ( !f ) return false;
		for ( std::string l; std::getline( f, l ); )
		{
			if ( !l.empty() && l.back() == '\r' ) l.pop_back();
			if ( l.rfind( "# ", 0 ) == 0 ) { header.push_back( "sde results " + l.substr( 2 ) ); continue; }
			std::istringstream is( l );
			std::string L, kind, hash;
			size_t no = 0;
			if ( !( is >> L >> no >> hash >> kind ) || L != "L" ) continue;
			sde_rec r;
			r.hash = std::stoull( hash, nullptr, 16 );
			if ( kind == "run" ) { r.kind = 1; int fl = 0; is >> fl >> r.vector; r.faulted = fl != 0; }
			else r.kind = kind == "skip" ? 2 : 3;
			std::getline( is, r.text );
			if ( r.kind != 1 ) r.text.erase( 0, r.text.find_first_not_of( ' ' ) );
			recs[ no ] = r;
		}
		return true;
	}

	// U1040 (--native-under-sde): the CPU Intel SDE presents to the process (its emulated CPUID and
	// XCR0, read natively), printed at the start of the run; decides which extra state the native
	// thunk moves (ZMM0-31/K0-7 with AVX-512 or AVX10 and XCR0 7:5 + 2:1 set, R16-R31 with APX_F and
	// XCR0 bit 19)
	struct sde_cpu { bool zmm = false, apx = false; uint64_t xcr0 = 0; };
	// the image name of the parent process ("" when unknown): under Intel SDE it is Pin's launcher
	// pin.exe (sde.exe -> pin.exe -> the application; Pin's VM DLLs are not in the loader's module list)
	static std::wstring parent_image()
	{
		HANDLE h = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
		if ( h == INVALID_HANDLE_VALUE ) return L"";
		const DWORD me = GetCurrentProcessId();
		DWORD ppid = 0;
		std::wstring name;
		PROCESSENTRY32W e{};
		e.dwSize = sizeof( e );
		for ( BOOL ok = Process32FirstW( h, &e ); ok; ok = Process32NextW( h, &e ) )
			if ( e.th32ProcessID == me ) { ppid = e.th32ParentProcessID; break; }
		e.dwSize = sizeof( e );
		for ( BOOL ok = ppid ? Process32FirstW( h, &e ) : FALSE; ok; ok = Process32NextW( h, &e ) )
			if ( e.th32ProcessID == ppid ) { name = e.szExeFile; break; }
		CloseHandle( h );
		return name;
	}
	static sde_cpu sde_probe( std::string& text )
	{
		sde_cpu c;
		auto add = [ & ]( const char* fmt, auto... a ) { char b[ 512 ]; std::snprintf( b, sizeof( b ), fmt, a... ); text += b; };
		int r[ 4 ] = {};
		__cpuid( r, 0 );
		const unsigned max_leaf = unsigned( r[ 0 ] );
		char vendor[ 13 ] = {};
		std::memcpy( vendor, &r[ 1 ], 4 ); std::memcpy( vendor + 4, &r[ 3 ], 4 ); std::memcpy( vendor + 8, &r[ 2 ], 4 );
		__cpuid( r, 1 );
		const unsigned sig = unsigned( r[ 0 ] );
		const bool osxsave = ( r[ 2 ] >> 27 ) & 1;
		auto leaf = [ & ]( unsigned l, unsigned s, int* o ) { if ( l <= max_leaf ) __cpuidex( o, int( l ), int( s ) ); else std::memset( o, 0, 16 ); };
		int l70[ 4 ], l71[ 4 ], l19[ 4 ], l24[ 4 ], ld0[ 4 ], l1e1[ 4 ];
		leaf( 7, 0, l70 ); leaf( 7, 1, l71 ); leaf( 0x19, 0, l19 ); leaf( 0x24, 0, l24 ); leaf( 0xD, 0, ld0 ); leaf( 0x1E, 1, l1e1 );
		c.xcr0 = osxsave ? _xgetbv( 0 ) : 0;
		const bool avx512f = ( l70[ 1 ] >> 16 ) & 1, avx10 = ( l71[ 3 ] >> 19 ) & 1, apx = ( l71[ 3 ] >> 21 ) & 1;
		c.zmm = ( avx512f || avx10 ) && ( c.xcr0 & 0xE6 ) == 0xE6;
		c.apx = apx && ( c.xcr0 & 0x80000 );
		add( "native engine: Intel SDE (--native-under-sde); results of the native side are SDE-validated, not hardware-validated\n" );
		add( "sde cpu: %s max leaf %XH signature %08X, XCR0 (XGETBV 0) = 0x%llX\n", vendor, max_leaf, sig, ( unsigned long long ) c.xcr0 );
		add( "sde cpuid: (7,0) EBX=%08X ECX=%08X EDX=%08X | (7,1) EAX=%08X EDX=%08X | (0Dh,0) EAX=%08X | (19h,0) EBX=%08X | (1Eh,1) EAX=%08X | (24h,0) EBX=%08X\n",
					 unsigned( l70[ 1 ] ), unsigned( l70[ 2 ] ), unsigned( l70[ 3 ] ), unsigned( l71[ 0 ] ), unsigned( l71[ 3 ] ), unsigned( ld0[ 0 ] ), unsigned( l19[ 1 ] ),
					 unsigned( l1e1[ 0 ] ), unsigned( l24[ 1 ] ) );
		add( "sde state: AVX512F %d, AVX10 %d (version %u), APX_F %d -> native thunk moves ZMM0-31/K0-7: %s, R16-R31: %s\n", avx512f, avx10,
					 unsigned( l24[ 1 ] & 0xFF ), apx, c.zmm ? "yes" : "no", c.apx ? "yes" : "no" );
		std::printf( "%s", text.c_str() );
		return c;
	}

	// U1040: which Unicorn-only state a key needs on the native side: 1 = ZMM/K (zmm*, zmmh*, k*,
	// xmm16-31, ymmh16-31), 2 = APX R16-R31, 0 = none
	static int ext_need( const std::string& kv )
	{
		const std::string k = kv.substr( 0, kv.find( '=' ) );
		auto num_after = [ & ]( size_t pl ) { return k.size() > pl && k.find_first_not_of( "0123456789", pl ) == std::string::npos ? std::stoi( k.substr( pl ) ) : -1; };
		if ( k.rfind( "zmm", 0 ) == 0 ) return 1;
		if ( k.size() == 2 && k[ 0 ] == 'k' && k[ 1 ] >= '0' && k[ 1 ] <= '7' ) return 1;
		if ( k.rfind( "xmm", 0 ) == 0 && num_after( 3 ) >= 16 ) return 1;
		if ( k.rfind( "ymmh", 0 ) == 0 && num_after( 4 ) >= 16 ) return 1;
		if ( k.size() == 3 && k[ 0 ] == 'r' && num_after( 1 ) >= 16 ) return 2;
		return 0;
	}

	// U1040: expected-value snippets were written for Unicorn only; before one runs natively (under SDE)
	// its bytes are scanned for what must never run on the host or would end the SDE process. A plain
	// byte scan (no decoder: APX / AVX10.2 encodings are newer than our Capstone), so a match inside an
	// immediate or displacement also excludes the case (conservative). "" = may run.
	static std::string sde_unsafe( const std::vector<uint8_t>& b )
	{
		const size_t n = b.size();
		for ( size_t i = 0; i < n; ++i )
		{
			const uint8_t c = b[ i ], c1 = i + 1 < n ? b[ i + 1 ] : 0, c2 = i + 2 < n ? b[ i + 2 ] : 0;
			if ( c == 0x0F && ( c1 == 0x05 || c1 == 0x34 ) ) return "SYSCALL/SYSENTER bytes (a real kernel entry)";
			// INT n with a vector Windows x64 lets CPL3 use: 29h fast fail (ends the process), 2Bh callback
			// return, 2Ch assertion, 2Dh debug service, 2Eh system call. Any other INT n is #GP (gate DPL 0),
			// INT3 / INTO-style vectors 3, 4 are delivered as exceptions: they may run.
			if ( c == 0xCD && ( c1 == 0x29 || ( c1 >= 0x2B && c1 <= 0x2E ) ) ) return "INT 29h/2Bh-2Eh bytes (a Windows kernel entry from CPL3)";
			if ( c == 0xD5 && ( c1 & 0x80 ) && ( c2 == 0x05 || c2 == 0x34 || c2 == 0xAE ) ) return "REX2 map-1 05h/34h/AEh bytes";
			// F3 0F AE /0-/3 with mod = 11: RD/WR FS/GS BASE (SDE ends the process, -fsgs_abort; WRGSBASE would
			// move the host thread's TEB)
			if ( c == 0x0F && c1 == 0xAE && ( c2 >> 6 ) == 3 && ( ( c2 >> 3 ) & 7 ) <= 3 ) return "RD/WR FS/GS BASE bytes (SDE -fsgs_abort; the host thread's FS/GS base)";
		}
		return "";
	}

	// U1050: run-to-run measurement of hardware cases. --hw-repeat N runs each hardware case's native
	// snippet N times (the first run is the one compared with Unicorn, as without the option) and
	// prints every distinct host outcome with its count, split by the core class the run was on, and
	// the logical CPUs it was seen on. The i5-13600K is hybrid (P-cores Raptor Cove, E-cores
	// Gracemont); CPUID.1AH:EAX[31:24] (40h Core, 20h Atom) names the core type on bare metal, but this
	// machine's Windows runs as a VMware guest whose 8 vCPUs move between the host's physical cores
	// (CPUID.1AH reads 0 there). So each run is classified by a timing probe (core_probe) taken just
	// before and just after it: "P" / "E" (both probes agree), "~" (they disagree: the vCPU moved
	// or the run was preempted), "?" (no class). --hw-cpu pins the native runs to logical CPUs (vCPUs
	// in a guest): a number or a comma list, "all" (rotate over every CPU of the process), "P" / "E"
	// (rotate over the CPUs of that CPUID.1AH core type: bare metal only). --hw-load N runs N
	// busy threads (the probe's ADD chain) beside the native runs, so a guest's vCPUs compete for the
	// host's cores. Measurement only: verdicts and summary lines are unchanged.
	struct host_cpu { int cpu; int type; };
	static const char* core_name( int type ) { return type == 0x40 ? "P" : type == 0x20 ? "E" : "?"; }
	// U1050: core class from the throughput of a dependent chain of four "add rax, 1" + dec/jnz
	// (our own 25 bytes, 1024 iterations, the lowest of 3 tries in TSC ticks per iteration). Measured
	// on this machine (2026-10-09, 8 vCPUs x 2000 tries): two separate bands, 0.6-1.5 ticks (one
	// iteration per clock: the P-core's renamer handles the ADD chain, at 3.5-5.1 GHz) and 3.5-4.2
	// ticks (four clocks per iteration at about 3.4-3.9 GHz, as an E-core); between them nothing.
	// Empirical only: it tells the two classes apart on this host, it is not a CPU specification.
	struct core_probe
	{
		uint8_t* code = nullptr;
		core_probe()
		{
			static const uint8_t chain[] = { 0x48, 0x31, 0xC0,                       // xor rax, rax
											 0x48, 0x83, 0xC0, 0x01, 0x48, 0x83, 0xC0, 0x01,   // 4 x add rax, 1
											 0x48, 0x83, 0xC0, 0x01, 0x48, 0x83, 0xC0, 0x01,
											 0x48, 0xFF, 0xC9,                       // dec rcx
											 0x75, 0xEB,                             // jnz -> the first add
											 0xC3 };                                 // ret
			code = ( uint8_t* ) VirtualAlloc( nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE );
			if ( code ) { std::memcpy( code, chain, sizeof( chain ) ); FlushInstructionCache( GetCurrentProcess(), code, sizeof( chain ) ); }
		}
		~core_probe() { if ( code ) VirtualFree( code, 0, MEM_RELEASE ); }
		uint64_t spin( uint64_t n ) const { return ( ( uint64_t( * )( uint64_t ) ) code )( n ); }
		double ticks_per_iter() const
		{
			if ( !code ) return 0;
			double best = 1e30;
			for ( int k = 0; k < 3; ++k )
			{
				const uint64_t t0 = __rdtsc();
				spin( 1024 );
				const uint64_t t1 = __rdtsc();
				const double t = double( t1 - t0 ) / 1024;
				if ( t >= 0.3 && t < best ) best = t;   // a TSC step of the guest gives nonsense (< 0.3)
			}
			return best;
		}
		static char cls( double t ) { return t >= 0.3 && t < 2.4 ? 'P' : t >= 2.8 && t < 5.5 ? 'E' : '?'; }
	};
	static int core_type_here() { int r[ 4 ] = {}; __cpuidex( r, 0, 0 ); if ( r[ 0 ] < 0x1A ) return 0; __cpuidex( r, 0x1A, 0 ); return ( r[ 0 ] >> 24 ) & 0xFF; }
	static std::vector<host_cpu> host_cpus()
	{
		std::vector<host_cpu> v;
		DWORD_PTR pm = 0, sm = 0;
		if ( !GetProcessAffinityMask( GetCurrentProcess(), &pm, &sm ) ) return v;
		HANDLE t = GetCurrentThread();
		for ( int c = 0; c < int( sizeof( DWORD_PTR ) * 8 ); ++c )
		{
			if ( !( pm >> c & 1 ) ) continue;
			SetThreadAffinityMask( t, DWORD_PTR( 1 ) << c );
			SwitchToThread();
			v.push_back( { int( GetCurrentProcessorNumber() ) == c ? c : -1 - c, core_type_here() } );
		}
		SetThreadAffinityMask( t, pm );
		return v;
	}
	// the CPUs --hw-cpu SPEC selects (empty = not pinned)
	static std::vector<int> hw_pin_list( const std::string& spec, const std::vector<host_cpu>& cpus, std::string& err )
	{
		std::vector<int> pin;
		if ( spec.empty() || spec == "none" ) return pin;
		for ( const host_cpu& c : cpus )
			if ( c.cpu >= 0 && ( spec == "all" || ( spec == "P" && c.type == 0x40 ) || ( spec == "E" && c.type == 0x20 ) ) ) pin.push_back( c.cpu );
		if ( spec != "all" && spec != "P" && spec != "E" )
		{
			std::istringstream ss( spec );
			std::string tok;
			while ( std::getline( ss, tok, ',' ) )
			{
				char* end = nullptr;
				long c = std::strtol( tok.c_str(), &end, 10 );
				bool found = false;
				for ( const host_cpu& h : cpus ) found |= h.cpu == c;
				if ( tok.empty() || *end || !found ) { err = "--hw-cpu: no logical CPU " + tok + " in the process affinity mask"; return {}; }
				pin.push_back( int( c ) );
			}
		}
		if ( pin.empty() ) err = "--hw-cpu " + spec + ": no such CPU";
		return pin;
	}

	// "#UD", "#GP", ..., "#13" -> vector; -1 when not a fault token. U772: an error code may follow
	// in parentheses, "#GP(0)", "#SS(0)", "#PF(0x6)", "#13(0x30)", stored in *ec (else -1)
	static int fault_vector( const std::string& t, int64_t* ec = nullptr )
	{
		static const char* names[] = { "DE", "DB", "NMI", "BP", "OF", "BR", "UD", "NM", "DF", "", "TS", "NP", "SS", "GP", "PF", "",
									   "MF", "AC", "MC", "XM", "VE", "CP" };
		if ( ec ) *ec = -1;
		if ( t.size() < 2 || t[ 0 ] != '#' ) return -1;
		std::string n = t.substr( 1 );
		size_t par = n.find( '(' );
		if ( par != std::string::npos )
		{
			if ( !ec || n.back() != ')' || par + 2 >= n.size() ) return -1;
			std::string v = n.substr( par + 1, n.size() - par - 2 );
			char* end = nullptr;
			unsigned long long x = std::strtoull( v.c_str(), &end, 0 );
			if ( !end || *end || x > 0xFFFFFFFFull ) return -1;
			*ec = int64_t( x );
			n = n.substr( 0, par );
		}
		for ( char& c : n ) c = char( std::toupper( ( unsigned char ) c ) );
		for ( int v = 0; v < int( sizeof( names ) / sizeof( names[ 0 ] ) ); ++v )
			if ( names[ v ][ 0 ] && n == names[ v ] ) return v;
		if ( n.find_first_not_of( "0123456789" ) == std::string::npos && n.size() <= 3 ) { int v = std::stoi( n ); return v < 256 ? v : -1; }
		return -1;
	}

	// the bytes of 'state' a strict expectation checks (see the header comment)
	static void strict_care( std::vector<uint8_t>& care )
	{
		auto on = [ & ]( size_t off, size_t n ) { std::memset( care.data() + off, 1, n ); };
		const size_t fx = offsetof( state, fx );
		on( offsetof( state, gpr ), sizeof( state::gpr ) );
		on( offsetof( state, rflags ), sizeof( state::rflags ) );
		on( fx + 0, 5 );                                  // FCW, FSW, abridged FTW
		on( fx + 24, 4 );                                 // MXCSR
		for ( int i = 0; i < 8; ++i ) on( fx + 32 + 16 * i, 10 );
		on( fx + 160, 256 );                              // XMM0-15
		on( offsetof( state, ymmh ), sizeof( state::ymmh ) );
		on( offsetof( state, zmmh ), sizeof( state::zmmh ) );
		on( offsetof( state, zmmx ), sizeof( state::zmmx ) );
		on( offsetof( state, k ), sizeof( state::k ) );
		on( offsetof( state, egpr ), sizeof( state::egpr ) );   // U614: APX R16-R31
		on( offsetof( state, mem ), sizeof( state::mem ) );
	}

	static std::string fault_text( bool faulted, int vector, int64_t ec = -1 )
	{
		if ( !faulted ) return "";
		std::string s = "fault #" + std::to_string( vector );
		if ( ec >= 0 ) s += "(" + hx( uint64_t( ec ) ) + ")";   // U772: the error code
		return s + " ";
	}

	// U530: trailing tag of a hardware case, "# known deviation: NAME" (a CPU-vs-SDM deviation
	// documented in docs\quirks.md; the emulator implements the SDM) or "# host state: REASON"
	// (the host's state differs from the emulator's, e.g. RDRAND values, the APIC ID, CET shadow
	// stacks Windows does not enable). Returns the kind ("" = untagged) and strips the tag.
	static std::string case_tag( std::string& line, std::string& name )
	{
		static const char* kinds[] = { "known deviation", "host state" };
		for ( const char* k : kinds )
		{
			std::string key = std::string( "# " ) + k + ":";
			size_t p = line.find( key );
			if ( p == std::string::npos ) continue;
			name = line.substr( p + key.size() );
			name.erase( 0, name.find_first_not_of( " \t" ) );
			while ( !name.empty() && ( name.back() == ' ' || name.back() == '\t' ) ) name.pop_back();
			line.erase( p );
			while ( !line.empty() && ( line.back() == ' ' || line.back() == '\t' ) ) line.pop_back();
			return k;
		}
		return "";
	}

	static int run_cases( const std::string& path, const case_opts& opt = {} )
	{
		std::ifstream f( path );
		if ( !f ) { std::printf( "cannot open %s\n", path.c_str() ); return 2; }
		// U540 (plan 1.H.2): XCR0 / CR0 per kind of case (0 = not written: Unicorn's reset value)
		const uint64_t host_x = host_xcr0();
		// U547: the host OS state plus the components of features Unicorn is opted in to that the host
		// lacks (an OS enabling them would set these bits): AVX-512/AVX10 7:5, AMX 18:17, APX 19.
		uint64_t optin_x = 0;
		if ( opt.avx512 || opt.avx10 ) optin_x |= 0xE0ull;
		if ( opt.amx ) optin_x |= 0x60000ull;
		if ( opt.apx ) optin_x |= 0x80000ull;
		const uint64_t hw_xcr0 = opt.xcr0 ? opt.xcr0 : ( host_x ? ( host_x | optin_x ) : 0 ), hw_cr0 = opt.cr0 ? opt.cr0 : WINDOWS_CR0;
		const uint64_t exp_xcr0 = opt.xcr0, exp_cr0 = opt.cr0;
		{
			// the effective values as an engine of this run starts with them (reset values included)
			auto show = [ & ]( const char* kind, uint64_t x, uint64_t c, const char* xsrc, const char* csrc )
			{
				unicorn_engine pe( UC_CPU_X86_MAX );
				pe.cpuid = opt.cpuid; pe.strict = opt.strict; pe.xcr0 = x; pe.cr0 = c;
				pe.avx512 = opt.avx512; pe.amx = opt.amx; pe.avx10 = opt.avx10; pe.apx = opt.apx;
				pe.rdrand = opt.rdrand; pe.rdrand_seed = opt.rdrand_seed;
				uint64_t ex = 0, ec = 0;
				std::string perr;
				if ( !pe.probe( ex, ec, perr ) ) { std::printf( "machine state: %s cases: %s\n", kind, perr.c_str() ); return; }
				std::printf( "machine state: %s cases XCR0=0x%llX (%s) CR0=0x%llX (%s)\n", kind, ( unsigned long long ) ex, xsrc,
							 ( unsigned long long ) ec, csrc );
			};
			if ( !opt.expect_only )
				show( "hardware", hw_xcr0, hw_cr0, opt.xcr0 ? "--xcr0" : host_x ? "host XGETBV(0)" : "Unicorn reset: host has no OSXSAVE",
					  opt.cr0 ? "--cr0" : "Windows x64 PE|MP|ET|NE|AM" );
			show( "expected-value", exp_xcr0, exp_cr0, opt.xcr0 ? "--xcr0" : "Unicorn reset", opt.cr0 ? "--cr0" : "Unicorn reset" );
			// U835 (D8): the RDRAND/RDSEED source of every Unicorn engine of this run
			if ( opt.rdrand == UC_X86_RDRAND_HOST ) std::printf( "rdrand: host DRNG (--HostSeed)\n" );
			else std::printf( "rdrand: seeded, seed 0x%llX (--seeded, the default)\n", ( unsigned long long ) opt.rdrand_seed );
		}
		native_engine hw;
		bool hw_open = false;
		// U1040: pass 1, --native-under-sde FILE: the "native" engine is Intel SDE's emulated CPU (the whole
		// process runs under sde.exe; refused otherwise, so a hardware result is never labelled SDE): only
		// the native side of each expected-value line runs, its result goes to FILE. Pass 2, --sde-results
		// FILE (a normal run): Unicorn runs as always and is also compared with FILE's results. Two passes
		// because Unicorn's own JIT under SDE's Pin runs at about one case per second.
		int sde_n = 0, sde_agree = 0, sde_disagree = 0, sde_errors = 0, sde_exp_agree = 0, sde_written = 0;
		std::map<std::string, int> sde_skip;
		FILE* sde_file = nullptr;
		std::map<size_t, sde_rec> sde_recs;
		std::set<size_t> sde_done;   // pass 1: case lines FILE already has (resume)
		if ( !opt.sde_out.empty() )
		{
			const std::wstring parent = parent_image();
			if ( _wcsicmp( parent.c_str(), L"pin.exe" ) )
			{
				std::printf( "--native-under-sde: the parent process is \"%ls\", not pin.exe: this process does not run under Intel SDE (sde.exe -<cpu> -- emu-alltest.exe ...)\n",
							 parent.c_str() );
				return 2;
			}
			if ( !opt.expect_only ) { std::printf( "--native-under-sde needs --expect-only (it runs the native side of the expected-value lines only)\n" ); return 2; }
			std::string text;
			const sde_cpu c = sde_probe( text );
			hw.ext_zmm = c.zmm;
			hw.ext_apx = c.apx;
			// resume: SDE ends the whole process on some conditions (an AMX #UD it reports as "AMX Exception",
			// a memory error of an emulated access, ...). Each line is marked "P <line>" before it runs; a
			// rerun with the same FILE keeps the finished lines, records a marked line without a result as
			// "error SDE ended the process" and goes on with the next one.
			{
				std::ifstream old( opt.sde_out );
				std::map<size_t, uint64_t> pending;
				for ( std::string l; std::getline( old, l ); )
				{
					if ( !l.empty() && l.back() == '\r' ) l.pop_back();
					std::istringstream is( l );
					std::string k, hash;
					size_t no = 0;
					if ( !( is >> k >> no >> hash ) ) continue;
					if ( k == "P" ) pending[ no ] = std::stoull( hash, nullptr, 16 );
					else if ( k == "L" ) { sde_done.insert( no ); pending.erase( no ); }
				}
				const bool resume = old.is_open() && !old.bad();
				old.close();
				if ( fopen_s( &sde_file, opt.sde_out.c_str(), resume ? "ab" : "wb" ) || !sde_file ) { std::printf( "cannot write %s\n", opt.sde_out.c_str() ); return 2; }
				if ( !resume )
				{
					std::fprintf( sde_file, "# emu-alltest --native-under-sde results (U1040) of %s\n", path.c_str() );
					std::istringstream ts( text );
					for ( std::string l; std::getline( ts, l ); ) std::fprintf( sde_file, "# %s\n", l.c_str() );
				}
				for ( auto& [ no, hash ] : pending )
				{
					std::fprintf( sde_file, "L %zu %016llX error SDE ended the process at this line\n", no, ( unsigned long long ) hash );
					sde_done.insert( no );
					std::printf( "resume: case line %zu ended the previous SDE run\n", no );
				}
				if ( resume ) std::printf( "resume: %zu case lines already in %s\n", sde_done.size(), opt.sde_out.c_str() );
				std::fflush( sde_file );
			}
		}
		if ( !opt.sde_in.empty() )
		{
			std::vector<std::string> header;
			if ( !load_sde_results( opt.sde_in, sde_recs, header ) ) { std::printf( "cannot read %s\n", opt.sde_in.c_str() ); return 2; }
			std::printf( "sde results: %s (%zu lines); results of the native side are SDE-validated, not hardware-validated\n", opt.sde_in.c_str(), sde_recs.size() );
			for ( auto& h : header ) std::printf( "%s\n", h.c_str() );
		}
		std::string err;
		// U1050: --hw-repeat / --hw-cpu (run-to-run measurement); the logical CPUs and their core types
		const bool hw_measure = opt.hw_repeat > 1 || !opt.hw_cpu.empty() || opt.hw_load > 0;
		std::vector<host_cpu> cpus;
		std::vector<int> pin;
		if ( hw_measure && !opt.expect_only )
		{
			cpus = host_cpus();
			pin = hw_pin_list( opt.hw_cpu, cpus, err );
			if ( !err.empty() ) { std::printf( "%s\n", err.c_str() ); return 2; }
			std::printf( "hw runs: %d per hardware case, %s:", opt.hw_repeat, pin.empty() ? "not pinned" : ( "pinned (--hw-cpu " + opt.hw_cpu + ")" ).c_str() );
			for ( int c : pin ) std::printf( " %d", c );
			std::printf( "\nhost CPUs:" );
			for ( const host_cpu& c : cpus ) std::printf( " %d%s", c.cpu < 0 ? -1 - c.cpu : c.cpu, c.cpu < 0 ? "(pin failed)" : core_name( c.type ) );
			std::printf( "\n" );
		}
		core_probe probe;
		// U1050: --hw-load N busy threads (the probe's chain), stopped when run_cases returns
		struct load_threads
		{
			std::vector<HANDLE> t;
			volatile LONG stop = 0;
			const core_probe* pr = nullptr;
			static DWORD WINAPI body( void* p ) { auto* l = ( load_threads* ) p; while ( !l->stop ) l->pr->spin( 1 << 16 ); return 0; }
			void start( int n, const core_probe& p ) { pr = &p; for ( int k = 0; k < n; ++k ) if ( HANDLE h = CreateThread( nullptr, 0, body, this, 0, nullptr ) ) t.push_back( h ); }
			~load_threads() { stop = 1; for ( HANDLE h : t ) { WaitForSingleObject( h, INFINITE ); CloseHandle( h ); } }
		} load;
		if ( hw_measure && !opt.expect_only )
		{
			if ( opt.hw_load > 0 ) load.start( opt.hw_load, probe );
			int cid[ 4 ] = {};
			__cpuidex( cid, 0x1A, 0 );
			std::printf( "core probe: ADD-chain ticks/iteration P < 2.4 <= ? < 2.8 <= E < 5.5; CPUID.1AH:EAX = 0x%X; load threads: %d\n", unsigned( cid[ 0 ] ), int( load.t.size() ) );
		}
		std::string line;
		int n = 0, differ = 0;
		int exp_n = 0, exp_differ = 0, exp_errors = 0, skipped = 0;
		// U530: tagged hardware cases (see case_tag): differing ones per tag name, matching ones listed
		std::map<std::string, int> known, host;
		std::vector<std::string> not_observed;
		int known_n = 0, host_n = 0;
		// U543: --shard K/N runs the K-th of N contiguous blocks of the file's case lines (non-empty,
		// not '#'); the N blocks together are exactly the file, in order (test.cmd runs them side by side)
		size_t shard_lo = 0, shard_hi = SIZE_MAX, case_line = 0;
		if ( opt.shard_n > 1 )
		{
			size_t total = 0;
			while ( std::getline( f, line ) )
			{
				if ( !line.empty() && line.back() == '\r' ) line.pop_back();
				total += !( line.empty() || line[ 0 ] == '#' );
			}
			f.clear();
			f.seekg( 0 );
			shard_lo = total * size_t( opt.shard_k - 1 ) / size_t( opt.shard_n );
			shard_hi = total * size_t( opt.shard_k ) / size_t( opt.shard_n );
			std::printf( "shard %d/%d: case lines %zu-%zu of %zu\n", opt.shard_k, opt.shard_n, shard_lo + 1, shard_hi, total );
		}
		while ( std::getline( f, line ) )
		{
			if ( !line.empty() && line.back() == '\r' ) line.pop_back();
			if ( line.empty() || line[ 0 ] == '#' ) continue;
			if ( const size_t cl = case_line++; cl < shard_lo || cl >= shard_hi ) continue;   // U543: another shard's line
			const size_t line_no = case_line - 1;   // U1040: the key of a --native-under-sde result
			std::string tag_name, tag_kind = case_tag( line, tag_name );
			if ( !tag_kind.empty() && ( line.find( "=>" ) != std::string::npos || tag_name.empty() ) )
			{
				std::printf( "[%d] %s\n    bad tag: \"# %s:\" needs a name and belongs to hardware cases only\n", n, line.c_str(), tag_kind.c_str() );
				++exp_errors;
				continue;
			}
			// "=>" / "=>!" splits off the expectations (expected-value case: Unicorn only)
			size_t arrow = line.find( "=>" );
			bool expect = arrow != std::string::npos, loose = false;
			std::string body = expect ? line.substr( 0, arrow ) : line, rhs;
			if ( expect )
			{
				rhs = line.substr( arrow + 2 );
				if ( !rhs.empty() && rhs[ 0 ] == '!' ) { loose = true; rhs.erase( 0, 1 ); }
			}
			else if ( opt.expect_only ) { ++skipped; continue; }
			size_t bar = body.find( '|' );
			std::string text = body.substr( 0, bar ), assigns = bar == std::string::npos ? "" : body.substr( bar + 1 );
			// U614: "<host asm> ~~ <unicorn asm>" pairs two encodings of one operation (hardware cases)
			std::string uc_text = text;
			size_t pair = text.find( "~~" );
			if ( pair != std::string::npos )
			{
				if ( expect )
				{
					std::printf( "[%d] %s\n    bad case: \"~~\" pairs belong to hardware cases only\n", n, line.c_str() );
					++exp_errors;
					continue;
				}
				uc_text = text.substr( pair + 2 );
				text = text.substr( 0, pair );
			}
			std::string asmtext = text, uc_asmtext = uc_text;
			for ( char& c : asmtext ) if ( c == ';' ) c = '\n';
			for ( char& c : uc_asmtext ) if ( c == ';' ) c = '\n';
			auto st_in = std::make_unique<state>();
			case_default( *st_in );
			std::istringstream as( assigns );
			std::string kv;
			bool ok = true, ext = false, cpl3 = false;
			int need = 0;   // U1040: Unicorn-only state the case uses (ext_need)
			while ( as >> kv )
			{
				if ( kv == "cpl=3" ) { cpl3 = true; continue; }
				if ( kv == "cpl=0" ) { cpl3 = false; continue; }
				need |= ext_need( kv );
				bool good = false;
				try { good = case_assign( *st_in, kv, err, nullptr, &ext ); }
				catch ( const std::exception& ) { err = "bad value in " + kv; }
				if ( !good ) { std::printf( "[%d] %s\n    bad assignment: %s\n", n, line.c_str(), err.c_str() ); ok = false; break; }
			}
			if ( !ok ) { exp_errors += expect; continue; }
			if ( ext && !expect )
			{
				std::printf( "[%d] %s\n    unsupported register for a hardware case: zmm*/zmmh*/k*/xmm16-31/ymmh16-31/r16-r31 exist only in expected-value cases (\"=>\"); the host CPU has no AVX-512 or APX\n", n, line.c_str() );
				continue;
			}
			// expectations: the input state with the listed assignments applied
			auto st_exp = std::make_unique<state>( *st_in );
			std::vector<uint8_t> listed( expect ? sizeof( state ) : 0, 0 );
			bool exp_fault = false;
			int exp_vector = -1;
			int64_t exp_ec = -1;     // U772: "#GP(0)": the error code must match too
			if ( expect )
			{
				std::istringstream es( rhs );
				std::string tok;
				while ( ok && es >> tok )
				{
					if ( tok[ 0 ] == '#' )
					{
						int64_t ec = -1;
						int v = fault_vector( tok, &ec );
						if ( v < 0 ) { err = "unknown fault " + tok; ok = false; }
						else if ( exp_fault ) { err = "more than one fault token"; ok = false; }
						else { exp_fault = true; exp_vector = v; exp_ec = ec; }
						continue;
					}
					need |= ext_need( tok );
					try { ok = case_assign( *st_exp, tok, err, &listed, nullptr ); }
					catch ( const std::exception& ) { err = "bad value in " + tok; ok = false; }
				}
				if ( !ok ) { std::printf( "[%d] %s\n    bad expectation: %s\n", n, line.c_str(), err.c_str() ); ++exp_errors; continue; }
			}
			std::vector<uint8_t> bytes = assemble( asmtext, CODE, &err );
			if ( bytes.empty() ) { std::printf( "[%d] %s\n    assembly failed: %s\n", n, line.c_str(), err.c_str() ); exp_errors += expect; continue; }
			program p = build( bytes, &err );
			if ( p.code.empty() ) { std::printf( "[%d] %s\n    build failed: %s\n", n, line.c_str(), err.c_str() ); exp_errors += expect; continue; }
			// U1040: pass 1 (--native-under-sde FILE): only the native side, on SDE's CPU, into FILE
			if ( expect && sde_file )
			{
				if ( sde_done.count( line_no ) ) { ++n; continue; }   // resume: already in FILE
				std::string why;
				if ( ( need & 1 ) && !hw.ext_zmm ) why = "ZMM/K state: the SDE CPU has no AVX-512/AVX10 state";
				else if ( ( need & 2 ) && !hw.ext_apx ) why = "R16-R31: the SDE CPU has no APX state";
				else if ( st_in->mxcsr() & ~0xFFFFu ) why = "MXCSR reserved bits (FXRSTOR64 #GP in the native prologue)";
				// U1041: SDE 10.13.1 ends the process (an unhandled X87_INVALID_OPERATION inside its own
				// sde-mix-mt.dll) instead of delivering an unmasked SIMD exception of an instruction it
				// emulates: all 258 "=> #XM" EVEX lines of cases_evex_m1.txt did, no other line did. Such a
				// line (a 62h byte in the snippet) is not run; SSE/AVX #XM lines run on the host CPU.
				else if ( exp_fault && exp_vector == 19 && std::find( bytes.begin(), bytes.end(), uint8_t( 0x62 ) ) != bytes.end() )
					why = "expected #XM of an EVEX instruction (SDE 10.13.1 ends the process: internal X87_INVALID_OPERATION)";
				else why = sde_unsafe( bytes );
				const unsigned long long hsh = ( unsigned long long ) fnv1a( line );
				if ( !why.empty() )
				{
					++sde_skip[ why ];
					std::fprintf( sde_file, "L %zu %016llX skip %s\n", line_no, hsh, why.c_str() );
					std::printf( "[%d] NOT CHECKED (%s) %s\n", n, why.c_str(), line.c_str() );
				}
				else
				{
					if ( !hw_open )
					{
						if ( !hw.open( err ) ) { std::printf( "native engine: %s\n", err.c_str() ); return 2; }
						hw_open = true;
					}
					std::fprintf( sde_file, "P %zu %016llX\n", line_no, hsh );   // resume marker (see above)
					std::fflush( sde_file );
					result h;
					hw.run( p, *st_in, h );
					if ( !h.ran )
					{
						++sde_errors;
						std::fprintf( sde_file, "L %zu %016llX error %s\n", line_no, hsh, h.err.c_str() );
						std::printf( "[%d] ERROR %s %s\n", n, h.err.c_str(), line.c_str() );
					}
					else
					{
						++sde_written;
						std::fprintf( sde_file, "L %zu %016llX run %d %d%s\n", line_no, hsh, h.faulted ? 1 : 0, h.faulted ? h.vector : -1, state_diff( *st_in, *h.s ).c_str() );
						std::printf( "[%d] RAN %s\n    sde: %s%s\n", n, line.c_str(), fault_text( h.faulted, h.vector ).c_str(), case_fields( *st_in, *h.s ).c_str() );
					}
				}
				std::fflush( sde_file );
				++n;
				continue;
			}
			// U614: the Unicorn snippet of a "~~" pair (else the same program)
			program p_uc = p;
			if ( pair != std::string::npos )
			{
				std::vector<uint8_t> uc_bytes = assemble( uc_asmtext, CODE, &err );
				if ( uc_bytes.empty() ) { std::printf( "[%d] %s\n    assembly failed (unicorn side): %s\n", n, line.c_str(), err.c_str() ); continue; }
				p_uc = build( uc_bytes, &err );
				if ( p_uc.code.empty() ) { std::printf( "[%d] %s\n    build failed (unicorn side): %s\n", n, line.c_str(), err.c_str() ); continue; }
			}
			result h, u;
			unicorn_engine uc( UC_CPU_X86_MAX );
			uc.cpuid = opt.cpuid; uc.strict = opt.strict;
			uc.xcr0 = expect ? exp_xcr0 : hw_xcr0;   // U540
			uc.cr0 = expect ? exp_cr0 : hw_cr0;
			uc.avx512 = opt.avx512;
			uc.amx = opt.amx;
			uc.avx10 = opt.avx10;
			uc.apx = opt.apx;
			uc.rdrand = opt.rdrand;   // U835
			uc.rdrand_seed = opt.rdrand_seed;
			uc.ext_regs = expect;
			uc.cpl3 = cpl3;
			if ( !uc.load( p_uc, err ) ) { std::printf( "[%d] uc load: %s\n", n, err.c_str() ); exp_errors += expect; continue; }
			uc.run( *st_in, u );
			if ( expect )
			{
				// compare only the checked bytes: copy Unicorn's bytes into the expected image where
				// they are checked, then list the fields that still differ
				std::vector<uint8_t> care( sizeof( state ), 0 );
				if ( !loose ) strict_care( care );
				for ( size_t b = 0; b < care.size(); ++b ) care[ b ] |= listed[ b ];
				auto masked = std::make_unique<state>( *st_exp );
				const uint8_t* us = ( const uint8_t* ) u.s.get();
				uint8_t* ms = ( uint8_t* ) masked.get();
				for ( size_t b = 0; b < care.size(); ++b ) if ( care[ b ] ) ms[ b ] = us[ b ];
				bool same_fault = exp_fault == u.faulted && exp_vector == u.vector && ( exp_ec < 0 || exp_ec == u.error_code );
				std::string ed = case_fields( *st_in, *st_exp ), ud = case_fields( *st_in, *u.s ), x = case_fields( *st_exp, *masked );
				bool same = same_fault && x.empty() && u.err.empty();
				differ += !same;
				exp_differ += !same;
				++exp_n;
				std::printf( "[%d] %s %s\n", n, same ? "SAME" : "DIFF", line.c_str() );
				std::printf( "    exp%s: %s%s\n", loose ? "!" : "", fault_text( exp_fault, exp_vector, exp_ec ).c_str(), ed.c_str() );
				std::printf( "    uc: %s%s\n", fault_text( u.faulted, u.vector, exp_ec >= 0 ? u.error_code : -1 ).c_str(), ud.c_str() );
				if ( !u.err.empty() ) std::printf( "    uc error: %s\n", u.err.c_str() );
				if ( !same ) std::printf( "    uc vs exp:%s%s\n", same_fault ? "" : " (fault differs)", x.c_str() );
				// U1040: pass 2 (--sde-results FILE): the same snippet and input state as run on Intel SDE's CPU
				// (pass 1), compared with Unicorn on the bytes this expectation checks (strict: every checked field;
				// loose: the listed ones) and the fault vector. A second, labelled reference; the "=>" verdict above
				// is unchanged.
				if ( !opt.sde_in.empty() )
				{
					auto rec = sde_recs.find( line_no );
					if ( rec == sde_recs.end() || rec->second.hash != fnv1a( line ) )
					{
						++sde_errors;
						std::printf( "    sde: ERROR no result for this line in %s (another case file, or the line changed)\n", opt.sde_in.c_str() );
					}
					else if ( rec->second.kind != 1 )
					{
						if ( rec->second.kind == 2 || rec->second.text.rfind( "SDE ended the process", 0 ) == 0 ) ++sde_skip[ rec->second.text ];   // SDE's own stop: not checkable
						else ++sde_errors;
						std::printf( "    sde: %s (%s)\n", rec->second.kind == 2 || rec->second.text.rfind( "SDE ended the process", 0 ) == 0 ? "NOT CHECKED" : "ERROR", rec->second.text.c_str() );
					}
					else
					{
						result h;
						*h.s = *st_in;
						if ( !apply_diff( *h.s, rec->second.text ) ) { ++sde_errors; std::printf( "    sde: ERROR bad result record\n" ); ++n; continue; }
						h.faulted = rec->second.faulted;
						h.vector = rec->second.vector;
						++sde_n;
						// as for hardware cases (U834): after a fault the host's RFLAGS.AC is the OS's
						if ( h.faulted && u.faulted ) h.s->rflags = ( h.s->rflags & ~0x40000ull ) | ( u.s->rflags & 0x40000ull );
						auto ms = std::make_unique<state>( *u.s );
						auto me = std::make_unique<state>( *st_exp );
						const uint8_t* hs = ( const uint8_t* ) h.s.get();
						uint8_t* msb = ( uint8_t* ) ms.get();
						uint8_t* meb = ( uint8_t* ) me.get();
						for ( size_t b = 0; b < care.size(); ++b ) if ( care[ b ] ) { msb[ b ] = hs[ b ]; meb[ b ] = hs[ b ]; }
						const bool sf = h.faulted == u.faulted && h.vector == u.vector;
						const bool ef = h.faulted == exp_fault && h.vector == exp_vector;
						const std::string xs = case_fields( *u.s, *ms ), xe = case_fields( *st_exp, *me );
						const bool agree = sf && xs.empty(), eagree = ef && xe.empty();
						sde_agree += agree;
						sde_disagree += !agree;
						sde_exp_agree += eagree;
						std::printf( "    sde: %s%s\n", fault_text( h.faulted, h.vector ).c_str(), case_fields( *st_in, *h.s ).c_str() );
						if ( agree ) std::printf( "    uc vs sde: AGREE%s\n", eagree ? "" : " (exp differs from both)" );
						else std::printf( "    uc vs sde: DISAGREE%s%s | exp vs sde: %s%s%s\n", sf ? "" : " (fault differs)", xs.c_str(), eagree ? "AGREE" : "DISAGREE",
										  ef ? "" : " (fault differs)", xe.c_str() );
					}
				}
				++n;
				continue;
			}
			if ( !hw_open )
			{
				if ( !hw.open( err ) ) { std::printf( "native engine: %s\n", err.c_str() ); return 2; }
				hw_open = true;
			}
			std::string hw_report;   // U1050
			if ( !hw_measure ) hw.run( p, *st_in, h );
			else
			{
				// U1050: N native runs; the first one is compared with Unicorn below. Outcome = the
				// vector, for an access violation the Windows access type and address, and every
				// field that changed; counted per core type, with the logical CPUs seen.
				struct tally { int count = 0; std::map<int, int> per_cpu; };
				std::map<std::string, tally> outcomes;
				int moved = 0;
				for ( int k = 0; k < opt.hw_repeat; ++k )
				{
					result r;
					result& rr = k == 0 ? h : r;
					if ( !pin.empty() ) { SetThreadAffinityMask( GetCurrentThread(), DWORD_PTR( 1 ) << pin[ size_t( k ) % pin.size() ] ); SwitchToThread(); }
					const char cb = core_probe::cls( probe.ticks_per_iter() );
					const int before = int( GetCurrentProcessorNumber() );
					hw.run( p, *st_in, rr );
					const int after = int( GetCurrentProcessorNumber() );
					const char ca = core_probe::cls( probe.ticks_per_iter() );
					moved += before != after;
					const char cc = cb == ca ? cb : '~';
					std::string key = std::string( "[" ) + cc + ( before != after ? "*" : "" ) + "] ";
					if ( rr.faulted )
					{
						key += "fault #" + std::to_string( rr.vector );
						if ( rr.vector == 14 || rr.vector == 13 || rr.vector == 12 ) key += " (av " + hx( rr.fault_info0 ) + " " + hx( rr.fault_info1 ) + ")";
						key += " rip+" + hx( rr.fault_rip - CODE ) + " ";
					}
					key += case_fields( *st_in, *rr.s );
					tally& t = outcomes[ key ];
					++t.count;
					++t.per_cpu[ before ];
				}
				if ( !pin.empty() ) { DWORD_PTR pm = 0, sm = 0; GetProcessAffinityMask( GetCurrentProcess(), &pm, &sm ); SetThreadAffinityMask( GetCurrentThread(), pm ); }
				char b[ 96 ];
				std::snprintf( b, sizeof( b ), "    hw runs: %d, distinct outcomes: %zu, moved during a run: %d\n", opt.hw_repeat, outcomes.size(), moved );
				hw_report = b;
				for ( auto& [ key, t ] : outcomes )
				{
					std::snprintf( b, sizeof( b ), "      %5d x ", t.count );
					hw_report += b + key + "\n            cpus:";
					for ( auto& [ c, m ] : t.per_cpu ) { std::snprintf( b, sizeof( b ), " %d:%d", c, m ); hw_report += b; }
					hw_report += "\n";
				}
			}
			// U834: Windows clears RFLAGS.AC while it dispatches a user-mode exception (observed for
			// #AC, #GP, #UD, ...: the context the VEH resumes has AC = 0), so after a hardware fault
			// the host's AC bit is the OS's, not the CPU's: the fault state compares Unicorn's AC
			if ( h.faulted && u.faulted ) h.s->rflags = ( h.s->rflags & ~0x40000ull ) | ( u.s->rflags & 0x40000ull );
			bool same_fault = h.faulted == u.faulted && h.vector == u.vector;
			std::string hd = case_fields( *st_in, *h.s ), ud = case_fields( *st_in, *u.s ), x = case_fields( *h.s, *u.s );
			bool same = same_fault && x.empty();
			// U530: a tagged case that differs is counted under its tag, not as differing; a tagged
			// case that matches is listed (the deviation was not observed: stale tag or a CPU that
			// sometimes takes the SDM path)
			const char* verdict = same ? "SAME" : "DIFF";
			if ( tag_kind.empty() ) differ += !same;
			else if ( same )
			{
				not_observed.push_back( "[" + std::to_string( n ) + "] " + tag_kind + ": " + tag_name );
				verdict = "SAME (tagged, not observed)";
			}
			else if ( tag_kind == "known deviation" ) { ++known[ tag_name ]; ++known_n; verdict = "KNOWN DEVIATION"; }
			else { ++host[ tag_name ]; ++host_n; verdict = "HOST STATE"; }
			std::printf( "[%d] %s %s%s\n", n, verdict, line.c_str(), tag_kind.empty() ? "" : ( "  # " + tag_kind + ": " + tag_name ).c_str() );
			std::printf( "    hw: %s%s\n", h.faulted ? ( "fault #" + std::to_string( h.vector ) + " " ).c_str() : "", hd.c_str() );
			std::printf( "    uc: %s%s\n", u.faulted ? ( "fault #" + std::to_string( u.vector ) + " " ).c_str() : "", ud.c_str() );
			if ( !same ) std::printf( "    uc vs hw:%s%s\n", same_fault ? "" : " (fault differs)", x.c_str() );
			if ( !hw_report.empty() ) std::printf( "%s", hw_report.c_str() );
			++n;
		}
		// U530: "differing" counts only untagged cases (hardware and expected-value); tagged hardware
		// cases that differ are counted per tag (docs\quirks.md names / host-state reasons)
		std::printf( "cases: %d, differing: %d, known deviations: %d, host state: %d\n", n, differ, known_n, host_n );
		for ( auto& [ k, v ] : known ) std::printf( "known deviation: %s: %d\n", k.c_str(), v );
		for ( auto& [ k, v ] : host ) std::printf( "host state: %s: %d\n", k.c_str(), v );
		std::printf( "tagged but not observed (matched the hardware): %zu\n", not_observed.size() );
		for ( auto& s : not_observed ) std::printf( "  not observed %s\n", s.c_str() );
		// U435: the CPUID profile and the strict setting Unicorn used
		if ( opt.cpuid.empty() )
			std::printf( "cpuid: model (no profile), strict %s\n", opt.strict < 0 ? "off (default)" : opt.strict ? "on (--strict)" : "off (--no-strict)" );
		else
			std::printf( "cpuid: profile (%zu entries), strict %s\n", opt.cpuid.size(), opt.strict < 0 ? "on (default with a profile)" : opt.strict ? "on (--strict)" : "off (--no-strict)" );
		if ( exp_n || exp_errors || skipped )
			std::printf( "expected-value cases: %d, differing: %d, errors: %d, skipped (no \"=>\"): %d\n", exp_n, exp_differ, exp_errors, skipped );
		// U1040: the SDE cross-check of the expected-value cases (does not change the exit status)
		int sde_skip_n = 0;
		for ( auto& [ k, v ] : sde_skip ) sde_skip_n += v;
		if ( sde_file )
		{
			std::fclose( sde_file );
			std::printf( "sde native run: ran %d, not checked %d, errors %d -> %s\n", sde_written, sde_skip_n, sde_errors, opt.sde_out.c_str() );
		}
		if ( !opt.sde_in.empty() )
			std::printf( "sde cases: %d, uc agrees with sde: %d, uc disagrees with sde: %d, exp agrees with sde: %d, not checked: %d, errors: %d\n", sde_n,
						 sde_agree, sde_disagree, sde_exp_agree, sde_skip_n, sde_errors );
		for ( auto& [ k, v ] : sde_skip ) std::printf( "sde not checked: %s: %d\n", k.c_str(), v );
		return ( exp_differ || exp_errors ) ? 1 : 0;
	}
}
