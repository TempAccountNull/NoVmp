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
// --xcr0 V configure Unicorn for both kinds of case (U435: a --cpuid profile is strict by
// default, --no-strict writes UC_CTL_X86_CPUID_STRICT = 0). U539: Unicorn implements the SDM only,
// for both kinds of case (no quirk switch; hardware deviations are tags, below). --avx512 opts
// Unicorn in to AVX-512
// (UC_CTL_X86_AVX512 = AVX512F|DQ|BW|VL|CD|IFMA|VPOPCNTDQ|BITALG|VBMI|FP16|VP2INTERSECT|VBMI2|VNNI|BF16, before the engine is
// initialised; reset XCR0 then has 7:5 set) for opmask/EVEX expected-value cases, e.g.
// Emulator\data\cases_opmask.txt. --amx
// opts in to Intel AMX (UC_CTL_X86_AMX = UC_X86_AMX_ALL) for Emulator\data\cases_amx.txt; the tile
// state itself is not a checked field (the cases store their results to memory). --avx10 N opts in
// to Intel AVX10 version N (UC_CTL_X86_AVX10 = N, 1 or 2; AVX-512 CPUID bits stay off unless
// --avx512 is given too) for Emulator\data\cases_avx10_a.txt.
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
#include <map>
#include <sstream>

namespace at
{
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

	// cr0: Unicorn's CR0 for the cases (0 = Unicorn default, PE only). Windows x64 runs with NE = 1:
	// pending unmasked x87 exceptions raise #MF, not FERR#; use 0x33 (PE|MP|ET|NE), never PG (flat map)
	// strict: -1 = not written (Unicorn's default: on while a profile is installed, U435),
	// 0 = --no-strict, 1 = --strict
	struct case_opts { std::vector<uc_x86_cpuid> cpuid; int strict = -1; uint64_t xcr0 = 0; uint64_t cr0 = 0; bool expect_only = false; int avx512 = 0; int amx = 0; int avx10 = 0; };

	// "#UD", "#GP", ..., "#13" -> vector; -1 when not a fault token
	static int fault_vector( const std::string& t )
	{
		static const char* names[] = { "DE", "DB", "NMI", "BP", "OF", "BR", "UD", "NM", "DF", "", "TS", "NP", "SS", "GP", "PF", "",
									   "MF", "AC", "MC", "XM", "VE", "CP" };
		if ( t.size() < 2 || t[ 0 ] != '#' ) return -1;
		std::string n = t.substr( 1 );
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
		on( offsetof( state, mem ), sizeof( state::mem ) );
	}

	static std::string fault_text( bool faulted, int vector ) { return faulted ? "fault #" + std::to_string( vector ) + " " : ""; }

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
		native_engine hw;
		bool hw_open = false;
		std::string err;
		std::string line;
		int n = 0, differ = 0;
		int exp_n = 0, exp_differ = 0, exp_errors = 0, skipped = 0;
		// U530: tagged hardware cases (see case_tag): differing ones per tag name, matching ones listed
		std::map<std::string, int> known, host;
		std::vector<std::string> not_observed;
		int known_n = 0, host_n = 0;
		while ( std::getline( f, line ) )
		{
			if ( !line.empty() && line.back() == '\r' ) line.pop_back();
			if ( line.empty() || line[ 0 ] == '#' ) continue;
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
			std::string asmtext = text;
			for ( char& c : asmtext ) if ( c == ';' ) c = '\n';
			auto st_in = std::make_unique<state>();
			case_default( *st_in );
			std::istringstream as( assigns );
			std::string kv;
			bool ok = true, ext = false, cpl3 = false;
			while ( as >> kv )
			{
				if ( kv == "cpl=3" ) { cpl3 = true; continue; }
				if ( kv == "cpl=0" ) { cpl3 = false; continue; }
				bool good = false;
				try { good = case_assign( *st_in, kv, err, nullptr, &ext ); }
				catch ( const std::exception& ) { err = "bad value in " + kv; }
				if ( !good ) { std::printf( "[%d] %s\n    bad assignment: %s\n", n, line.c_str(), err.c_str() ); ok = false; break; }
			}
			if ( !ok ) { exp_errors += expect; continue; }
			if ( ext && !expect )
			{
				std::printf( "[%d] %s\n    unsupported register for a hardware case: zmm*/zmmh*/k*/xmm16-31/ymmh16-31 exist only in expected-value cases (\"=>\"); the host CPU has no AVX-512\n", n, line.c_str() );
				continue;
			}
			// expectations: the input state with the listed assignments applied
			auto st_exp = std::make_unique<state>( *st_in );
			std::vector<uint8_t> listed( expect ? sizeof( state ) : 0, 0 );
			bool exp_fault = false;
			int exp_vector = -1;
			if ( expect )
			{
				std::istringstream es( rhs );
				std::string tok;
				while ( ok && es >> tok )
				{
					if ( tok[ 0 ] == '#' )
					{
						int v = fault_vector( tok );
						if ( v < 0 ) { err = "unknown fault " + tok; ok = false; }
						else if ( exp_fault ) { err = "more than one fault token"; ok = false; }
						else { exp_fault = true; exp_vector = v; }
						continue;
					}
					try { ok = case_assign( *st_exp, tok, err, &listed, nullptr ); }
					catch ( const std::exception& ) { err = "bad value in " + tok; ok = false; }
				}
				if ( !ok ) { std::printf( "[%d] %s\n    bad expectation: %s\n", n, line.c_str(), err.c_str() ); ++exp_errors; continue; }
			}
			std::vector<uint8_t> bytes = assemble( asmtext, CODE, &err );
			if ( bytes.empty() ) { std::printf( "[%d] %s\n    assembly failed: %s\n", n, line.c_str(), err.c_str() ); exp_errors += expect; continue; }
			program p = build( bytes, &err );
			if ( p.code.empty() ) { std::printf( "[%d] %s\n    build failed: %s\n", n, line.c_str(), err.c_str() ); exp_errors += expect; continue; }
			result h, u;
			unicorn_engine uc( UC_CPU_X86_MAX );
			uc.cpuid = opt.cpuid; uc.strict = opt.strict; uc.xcr0 = opt.xcr0; uc.cr0 = opt.cr0;
			uc.avx512 = opt.avx512;
			uc.amx = opt.amx;
			uc.avx10 = opt.avx10;
			uc.ext_regs = expect;
			uc.cpl3 = cpl3;
			if ( !uc.load( p, err ) ) { std::printf( "[%d] uc load: %s\n", n, err.c_str() ); exp_errors += expect; continue; }
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
				bool same_fault = exp_fault == u.faulted && exp_vector == u.vector;
				std::string ed = case_fields( *st_in, *st_exp ), ud = case_fields( *st_in, *u.s ), x = case_fields( *st_exp, *masked );
				bool same = same_fault && x.empty() && u.err.empty();
				differ += !same;
				exp_differ += !same;
				++exp_n;
				std::printf( "[%d] %s %s\n", n, same ? "SAME" : "DIFF", line.c_str() );
				std::printf( "    exp%s: %s%s\n", loose ? "!" : "", fault_text( exp_fault, exp_vector ).c_str(), ed.c_str() );
				std::printf( "    uc: %s%s\n", fault_text( u.faulted, u.vector ).c_str(), ud.c_str() );
				if ( !u.err.empty() ) std::printf( "    uc error: %s\n", u.err.c_str() );
				if ( !same ) std::printf( "    uc vs exp:%s%s\n", same_fault ? "" : " (fault differs)", x.c_str() );
				++n;
				continue;
			}
			if ( !hw_open )
			{
				if ( !hw.open( err ) ) { std::printf( "native engine: %s\n", err.c_str() ); return 2; }
				hw_open = true;
			}
			hw.run( p, *st_in, h );
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
		return ( exp_differ || exp_errors ) ? 1 : 0;
	}
}
