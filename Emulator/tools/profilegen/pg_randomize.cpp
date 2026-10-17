// pg_randomize.cpp - Randomizer (pg_randomize.hpp): RDRAND through pg_rand.asm, and every format rule of the synthetic
// values. Design: the user's rid demo (randomize.cpp) - re-implemented: the same rules (CPUID check once; 10 retries and
// the 0 / ~0 rejection in the assembly; rejection sampling for every character; the installation-ID check digit;
// thread-local result buffers), C++ strings inside and exceptions on failure. Thread-safe and lock-free: no member is written
// after construction; the byte pool and the result buffers are thread_local (plan 1.J.49.a).
#include "pg_randomize.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <regex>
#include <stdexcept>
#include <vector>

extern "C" int pg_has_rdrand();							// pg_rand.asm
extern "C" int pg_rdrand64( unsigned long long* out );	// pg_rand.asm

namespace emu::platform
{
	namespace
	{
		const char* const k_digits = "0123456789";
		const char* const k_upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
		const char* const k_lower = "abcdefghijklmnopqrstuvwxyz";
		bool is_hex( char c ) { return std::isxdigit( uint8_t( c ) ) != 0; }
		bool ieq( const std::string& a, const std::string& b )
		{
			return a.size() == b.size() && std::equal( a.begin(), a.end(), b.begin(), []( char x, char y ) { return std::tolower( uint8_t( x ) ) == std::tolower( uint8_t( y ) ); } );
		}
		[[noreturn]] void fail( const std::string& m ) { throw std::runtime_error( m ); }
		// the byte pool of gen_char(): the bytes of one RDRAND draw, consumed low byte first; one per thread (thread_local),
		// so the Randomizer has no mutable member state and needs no lock (plan 1.J.49.a)
		struct BytePool
		{
			uint64_t bits = 0;	// the not yet consumed bytes of the last draw, next byte in bits 7:0
			int left = 0;		// how many bytes of bits are still unused (0..8)
		};
		thread_local BytePool t_pool;
		// the result buffer of one gen_: valid until that gen_ runs again on this thread (the demo's thread_local buffers)
		const char* result( std::string& buf, std::string v )
		{
			buf = std::move( v );
			return buf.c_str();
		}
	}

	Randomizer& Randomizer::get_instance()
	{
		static Randomizer instance;	// magic static: thread-safe one-time construction (C++11)
		return instance;
	}

	Randomizer::Randomizer() : cpu_supports_rand( pg_has_rdrand() != 0 ) {}

	// ── primitives ───────────────────────────────────────────────────────────────────────────────────────────────────────
	uint64_t Randomizer::gen_u64()
	{
		if ( !cpu_supports_rand ) fail( "RDRAND is not available on this CPU (CPUID.1:ECX[30] = 0): a new identity cannot be generated (no fallback)" );
		unsigned long long v = 0;
		if ( !pg_rdrand64( &v ) ) fail( "RDRAND returned no random data in 10 attempts: a new identity cannot be generated (no fallback)" );
		return v;
	}
	uint8_t Randomizer::byte()
	{
		BytePool& p = t_pool;	// this thread's pool: no other thread reads or writes it
		if ( p.left == 0 )
		{
			p.bits = gen_u64();
			p.left = 8;
		}
		const uint8_t b = uint8_t( p.bits );
		p.bits >>= 8;
		p.left--;
		return b;
	}
	uint64_t Randomizer::gen_below( uint64_t n )
	{
		if ( n == 0 ) fail( "Randomizer::gen_below( 0 )" );
		const uint64_t limit = ~0ull - ( ~0ull % n + 1 ) % n;	// values above the largest multiple of n are drawn again
		for ( ;; )
		{
			const uint64_t r = gen_u64();
			if ( r <= limit ) return r % n;
		}
	}
	int64_t Randomizer::gen_in_range( int64_t lo, int64_t hi )
	{
		if ( hi < lo ) fail( "Randomizer::gen_in_range: an empty range" );
		return lo + int64_t( gen_below( uint64_t( hi - lo ) + 1 ) );
	}
	char Randomizer::gen_char( const char* set )
	{
		const size_t n = strlen( set );
		if ( n == 0 || n > 256 ) fail( "Randomizer::gen_char: a set of 1..256 characters" );
		const unsigned limit = unsigned( 256 - 256 % n );	// bytes >= limit are drawn again: no modulo bias
		for ( ;; )
		{
			const unsigned b = byte();
			if ( b < limit ) return set[ b % n ];
		}
	}
	const char* Randomizer::gen_decimal_in_range( int64_t lo, int64_t hi )
	{
		thread_local std::string buf;
		return result( buf, std::to_string( gen_in_range( lo, hi ) ) );
	}
	const char* Randomizer::gen_fill( const char* pattern, const char* set )
	{
		thread_local std::string buf;
		std::string out = pattern;
		for ( char& c : out )
			if ( c == '?' ) c = gen_char( set );
		return result( buf, out );
	}

	// ── format-preserving values ─────────────────────────────────────────────────────────────────────────────────────────
	const char* Randomizer::gen_like( const char* real )
	{
		thread_local std::string buf;
		const std::string r = real;
		if ( std::none_of( r.begin(), r.end(), []( char c ) { return std::isalnum( uint8_t( c ) ) != 0; } ) ) fail( "rule 2: a value without letters or digits cannot be replaced" );
		for ( ;; )
		{
			std::string o = r;
			for ( char& c : o )
			{
				if ( c >= '0' && c <= '9' ) c = gen_char( k_digits );
				else if ( c >= 'A' && c <= 'Z' ) c = gen_char( k_upper );
				else if ( c >= 'a' && c <= 'z' ) c = gen_char( k_lower );
			}
			if ( o != r ) return result( buf, o );
		}
	}
	const char* Randomizer::gen_hex_like( const char* real, bool keep_digit_class )
	{
		thread_local std::string buf;
		const std::string r = real;
		if ( std::none_of( r.begin(), r.end(), is_hex ) ) fail( "a value without hex digits cannot be replaced" );
		const bool lower_case = std::any_of( r.begin(), r.end(), []( char c ) { return c >= 'a' && c <= 'f'; } );
		for ( ;; )
		{
			std::string o = r;
			for ( char& c : o )
			{
				if ( !is_hex( c ) ) continue;
				if ( keep_digit_class && c >= '0' && c <= '9' ) c = gen_char( k_digits );
				else if ( keep_digit_class ) c = gen_char( c >= 'a' ? "abcdef" : "ABCDEF" );
				else c = gen_char( lower_case ? "0123456789abcdef" : "0123456789ABCDEF" );
			}
			if ( !ieq( o, r ) ) return result( buf, o );
		}
	}
	const char* Randomizer::gen_usb_serial_like( const char* real )
	{
		thread_local std::string buf;
		const std::string r = real;
		const bool hex = r.size() >= 2 && std::all_of( r.begin(), r.end(), is_hex );
		const bool upper = std::none_of( r.begin(), r.end(), []( char c ) { return c >= 'a' && c <= 'f'; } );
		if ( hex && r.size() % 2 == 0 && r.size() >= 8 )
		{
			std::string txt;
			for ( size_t i = 0; i < r.size(); i += 2 ) txt += char( std::stoul( r.substr( i, 2 ), nullptr, 16 ) );
			if ( std::all_of( txt.begin(), txt.end(), []( char c ) { return c >= 0x20 && c <= 0x7E; } ) && std::any_of( txt.begin(), txt.end(), []( char c ) { return std::isalnum( uint8_t( c ) ) != 0; } ) )
			{
				const std::string t = gen_like( txt.c_str() );
				std::string o;
				char h[ 3 ];
				for ( char c : t ) { snprintf( h, sizeof h, upper ? "%02X" : "%02x", unsigned( uint8_t( c ) ) ); o += h; }
				return result( buf, o );
			}
		}
		return result( buf, hex ? gen_hex_like( real, true ) : gen_like( real ) );
	}
	const char* Randomizer::gen_computer_name( const char* avoid )
	{
		thread_local std::string buf;
		for ( ;; )
		{
			const std::string n = gen_fill( "DESKTOP-???????", "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" );
			if ( !avoid || !ieq( n, avoid ) ) return result( buf, n );
		}
	}
	const char* Randomizer::gen_mac_like( const char* real, unsigned max_delta )
	{
		thread_local std::string buf;
		const std::string r = real;
		if ( r.size() != 17 ) fail( "rule 3: not a MAC in XX:XX:XX:XX:XX:XX form" );
		for ( ;; )
		{
			const uint64_t tail = gen_below( 0x1000000ull - max_delta );	// the tail leaves room for the largest delta of its family
			char t[ 9 ];
			snprintf( t, sizeof t, "%02X:%02X:%02X", unsigned( tail >> 16 & 0xFF ), unsigned( tail >> 8 & 0xFF ), unsigned( tail & 0xFF ) );
			std::string o = r.substr( 0, 9 );
			for ( char& c : o ) c = char( std::toupper( uint8_t( c ) ) );
			o += t;
			if ( !ieq( o, r ) ) return result( buf, o );
		}
	}
	const char* Randomizer::gen_guid_like( const char* real, const char* node )
	{
		thread_local std::string buf;
		static const std::regex k_guid( R"(\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?)" );
		const std::string g = real;
		if ( !std::regex_match( g, k_guid ) ) fail( "rule 4c: not a GUID" );
		const bool up = std::none_of( g.begin(), g.end(), []( char c ) { return c >= 'a' && c <= 'f'; } );
		std::vector<size_t> hp;
		for ( size_t i = 0; i < g.size(); i++ ) if ( is_hex( g[ i ] ) ) hp.push_back( i );
		bool mbr = true;
		for ( size_t k = 8; k < 20; k++ ) mbr &= g[ hp[ k ] ] == '0';
		std::string real_node;
		for ( size_t k = 20; k < 32; k++ ) real_node += char( std::tolower( uint8_t( g[ hp[ k ] ] ) ) );
		const bool v1 = !mbr && g[ hp[ 12 ] ] == '1';
		const char* set = up ? "0123456789ABCDEF" : "0123456789abcdef";
		for ( ;; )
		{
			std::string o = g;
			for ( size_t k = 0; k < 32; k++ )
			{
				if ( k == 12 || ( mbr && k >= 8 ) ) continue;	// the version digit; an MBR GUID's fixed part and partition offset
				char r = gen_char( set );
				if ( k == 16 )	// the variant bits: 0xxx NCS bit 3, 10xx RFC 4122 bits 3:2, 110x / 111x bits 3:1
				{
					const unsigned v = unsigned( std::stoul( std::string( 1, g[ hp[ k ] ] ), nullptr, 16 ) ), x = unsigned( std::stoul( std::string( 1, r ), nullptr, 16 ) );
					const unsigned keep = v < 8 ? 0x8 : v < 12 ? 0xC : 0xE;
					r = set[ ( v & keep ) | ( x & ~keep & 15 ) ];
				}
				o[ hp[ k ] ] = r;
			}
			if ( v1 && node )
				for ( size_t k = 20; k < 32; k++ ) o[ hp[ k ] ] = up ? char( std::toupper( uint8_t( node[ k - 20 ] ) ) ) : char( std::tolower( uint8_t( node[ k - 20 ] ) ) );
			else if ( v1 && real_node == "806e6f6e6963" )
				for ( size_t k = 20; k < 32; k++ ) o[ hp[ k ] ] = g[ hp[ k ] ];
			if ( !ieq( o, g ) ) return result( buf, o );
		}
	}
	const char* Randomizer::gen_system_uuid_like( const char* real_hex, bool vmware )
	{
		thread_local std::string buf;
		const std::string r = real_hex;
		if ( r.size() != 32 || !std::all_of( r.begin(), r.end(), is_hex ) ) fail( "rule 1: a UUID of 32 hex digits expected" );
		auto nib = [ & ]( size_t i ) { return unsigned( std::stoul( r.substr( i, 1 ), nullptr, 16 ) ); };
		if ( vmware && !ieq( r.substr( 0, 4 ), "564d" ) ) fail( "rule 1: the guest UUID does not have VMware's 56 4D layout" );
		for ( ;; )
		{
			std::string o = gen_fill( "????????????????????????????????", "0123456789abcdef" );
			if ( vmware ) o.replace( 0, 4, "564d" );
			else
			{
				o[ 12 ] = char( std::tolower( uint8_t( r[ 12 ] ) ) );	// byte 6 bits 7:4 (the version under one byte order)
				o[ 14 ] = char( std::tolower( uint8_t( r[ 14 ] ) ) );	// byte 7 bits 7:4 (under the other)
				const unsigned v = nib( 16 ), x = unsigned( std::stoul( o.substr( 16, 1 ), nullptr, 16 ) );	// byte 8 bits 7:4: the variant
				const unsigned keep = v < 8 ? 0x8 : v < 12 ? 0xC : 0xE;
				o[ 16 ] = "0123456789abcdef"[ ( v & keep ) | ( x & ~keep & 15 ) ];
			}
			if ( !ieq( o, r ) ) return result( buf, o );
		}
	}
	const char* Randomizer::gen_decimal_like( const char* real, uint64_t max )
	{
		thread_local std::string buf;
		const std::string r = real;
		if ( r.empty() || r.size() > 19 || !std::all_of( r.begin(), r.end(), []( char c ) { return c >= '0' && c <= '9'; } ) ) fail( "a decimal number expected" );
		uint64_t lo = 1;
		for ( size_t i = 1; i < r.size(); i++ ) lo *= 10;
		const uint64_t hi = std::min<uint64_t>( r.size() == 19 ? ~0ull : lo * 10 - 1, max );
		if ( r.size() == 1 ) lo = 0;
		if ( hi < lo || hi == lo ) fail( "no other decimal of that digit count" );
		for ( ;; )
		{
			const std::string o = std::to_string( lo + gen_below( hi - lo + 1 ) );
			if ( o != r ) return result( buf, o );
		}
	}
	const char* Randomizer::gen_edid_serial_like( const char* real_decimal )
	{
		thread_local std::string buf;
		const uint32_t v = uint32_t( std::stoull( real_decimal ) );
		const char c[ 4 ] = { char( v & 0xFF ), char( v >> 8 & 0xFF ), char( v >> 16 & 0xFF ), char( v >> 24 & 0xFF ) };
		if ( std::all_of( c, c + 4, []( char x ) { return std::isalnum( uint8_t( x ) ) != 0; } ) )	// some vendors store four characters
		{
			const std::string t = gen_like( std::string( c, 4 ).c_str() );
			const uint32_t s = uint32_t( uint8_t( t[ 0 ] ) ) | uint32_t( uint8_t( t[ 1 ] ) ) << 8 | uint32_t( uint8_t( t[ 2 ] ) ) << 16 | uint32_t( uint8_t( t[ 3 ] ) ) << 24;
			return result( buf, std::to_string( s ) );
		}
		return result( buf, gen_decimal_like( real_decimal, 0xFFFFFFFFull ) );
	}
	const char* Randomizer::gen_sid_like( const char* prefix )
	{
		thread_local std::string buf;
		static const std::regex r( R"(^S-1-5-21-([0-9]+)-([0-9]+)-([0-9]+)$)", std::regex::icase );
		const std::string p = prefix;
		std::smatch m;
		if ( !std::regex_match( p, m, r ) ) fail( "rule 7d: not an S-1-5-21-a-b-c SID prefix" );
		for ( ;; )
		{
			std::string o = p.substr( 0, 9 );
			for ( int k = 1; k <= 3; k++ ) o += std::string( gen_decimal_like( m[ k ].str().c_str(), 0xFFFFFFFFull ) ) + ( k < 3 ? "-" : "" );
			if ( !ieq( o, p ) ) return result( buf, o );
		}
	}
	char Randomizer::check_digit( const char* six )
	{
		if ( strlen( six ) != 6 ) fail( "installation-ID group: six digits expected" );
		unsigned sum = 0;
		for ( size_t i = 0; i < 6; i++ )
		{
			if ( six[ i ] < '0' || six[ i ] > '9' ) fail( "installation-ID group: six digits expected" );
			sum += unsigned( six[ i ] - '0' ) * ( i & 1 ? 2 : 1 );
		}
		return char( '0' + sum % 7 );
	}
	const char* Randomizer::gen_installation_id_like( const char* real )
	{
		thread_local std::string buf;
		const std::string r = real;
		if ( std::count_if( r.begin(), r.end(), []( char c ) { return c >= '0' && c <= '9'; } ) != 63 ) fail( "rule 8a: an installation ID of 63 digits (9 groups of 7) expected" );
		for ( ;; )
		{
			std::string digits;
			for ( int g = 0; g < 9; g++ )
			{
				const std::string six = gen_fill( "??????", k_digits );
				digits += six + check_digit( six.c_str() );
			}
			std::string o = r;
			size_t k = 0;
			for ( char& c : o )
				if ( c >= '0' && c <= '9' ) c = digits[ k++ ];
			if ( o != r ) return result( buf, o );
		}
	}
}
