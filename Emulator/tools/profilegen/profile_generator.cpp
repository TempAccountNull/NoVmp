// profile_generator.cpp - ProfileGenerator (plan 1.J.49, ledger U1510-U1512). The field rules and the exact locations
// they touch in the clean captures are documented in profile_generator.hpp; each gen_ below repeats the locations it
// changes. Nothing here ever prints or writes a real identifier value outside the ORIGINAL profile: log lines, errors, the
// field map and the manifests carry locations, lengths and counts only.
#include "profile_generator.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>

#pragma comment( lib, "bcrypt.lib" )

namespace fs = std::filesystem;

namespace emu::platform
{
	static const char* const k_tool = "emu-profilegen 1.0 (ProfileGenerator, ledger U1510-U1512)";

	const char* env_dir_name( Environment e ) { return e == Environment::vmware_guest ? "vmware_guest" : "bare_host"; }
	const char* env_switch( Environment e ) { return e == Environment::vmware_guest ? "--guest" : "--host"; }

	// ── small helpers ─────────────────────────────────────────────────────────────────────────────────────────────────────
	[[noreturn]] static void fail( const std::string& msg ) { throw std::runtime_error( msg ); }

	static std::string lower( std::string s )
	{
		for ( char& c : s ) c = char( std::tolower( uint8_t( c ) ) );
		return s;
	}
	static std::string upper( std::string s )
	{
		for ( char& c : s ) c = char( std::toupper( uint8_t( c ) ) );
		return s;
	}
	static bool ieq( const std::string& a, const std::string& b ) { return lower( a ) == lower( b ); }
	static bool is_alnum( char c ) { return std::isalnum( uint8_t( c ) ) != 0; }
	static bool is_hex( char c ) { return std::isxdigit( uint8_t( c ) ) != 0; }
	static bool has_lower_hex( const std::string& s ) { return std::any_of( s.begin(), s.end(), []( char c ) { return c >= 'a' && c <= 'f'; } ); }
	static std::string fmt( const char* f, ... )
	{
		char buf[ 1024 ];
		va_list ap;
		va_start( ap, f );
		vsnprintf( buf, sizeof buf, f, ap );
		va_end( ap );
		return buf;
	}
	static std::string hex_of( const uint8_t* p, size_t n, bool upper_case )
	{
		static const char* d[ 2 ] = { "0123456789abcdef", "0123456789ABCDEF" };
		std::string s;
		for ( size_t i = 0; i < n; i++ ) { s += d[ upper_case ][ p[ i ] >> 4 ]; s += d[ upper_case ][ p[ i ] & 15 ]; }
		return s;
	}
	static Bytes to_bytes( const std::string& s ) { return Bytes( s.begin(), s.end() ); }
	static std::string to_string( const Bytes& b, size_t off, size_t n ) { return std::string( b.begin() + off, b.begin() + off + n ); }

	static Bytes read_file( const fs::path& p )
	{
		std::ifstream f( p, std::ios::binary );
		if ( !f ) fail( "cannot read " + p.filename().string() );
		return Bytes( std::istreambuf_iterator<char>( f ), std::istreambuf_iterator<char>() );
	}
	static std::string env_var( const char* name )
	{
		char* v = nullptr;
		size_t n = 0;
		std::string s;
		if ( _dupenv_s( &v, &n, name ) == 0 && v ) { s = v; free( v ); }
		return s;
	}
	// log / error text for a path: the user-profile part is shown as %USERPROFILE% (it holds the account name)
	static std::string show( const fs::path& p )
	{
		std::string s = p.string(), u = env_var( "USERPROFILE" );
		if ( !u.empty() && s.size() >= u.size() && ieq( s.substr( 0, u.size() ), u ) ) s = "%USERPROFILE%" + s.substr( u.size() );
		return s;
	}
	static std::vector<std::string> split_lines( const Bytes& b )
	{
		std::vector<std::string> v;
		std::string cur;
		for ( uint8_t c : b )
		{
			if ( c == '\n' ) { if ( !cur.empty() && cur.back() == '\r' ) cur.pop_back(); v.push_back( cur ); cur.clear(); }
			else cur += char( c );
		}
		if ( !cur.empty() ) v.push_back( cur );
		return v;
	}

	// ── CNG ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
	static BCRYPT_ALG_HANDLE alg( bool hmac )
	{
		static BCRYPT_ALG_HANDLE h[ 2 ] = {};
		if ( !h[ hmac ] && !BCRYPT_SUCCESS( BCryptOpenAlgorithmProvider( &h[ hmac ], BCRYPT_SHA256_ALGORITHM, nullptr, hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0 ) ) )
			fail( "BCryptOpenAlgorithmProvider(SHA256) failed" );
		return h[ hmac ];
	}
	static std::array<uint8_t, 32> cng_hash( bool hmac, const uint8_t* key, size_t key_len, const void* data, size_t size )
	{
		std::array<uint8_t, 32> out {};
		BCRYPT_HASH_HANDLE h = nullptr;
		if ( !BCRYPT_SUCCESS( BCryptCreateHash( alg( hmac ), &h, nullptr, 0, PUCHAR( key ), ULONG( key_len ), 0 ) ) ) fail( "BCryptCreateHash failed" );
		const uint8_t* p = static_cast<const uint8_t*>( data );
		while ( size )	// BCryptHashData takes a ULONG length
		{
			ULONG part = ULONG( std::min<size_t>( size, 0x40000000 ) );
			if ( !BCRYPT_SUCCESS( BCryptHashData( h, PUCHAR( p ), part, 0 ) ) ) { BCryptDestroyHash( h ); fail( "BCryptHashData failed" ); }
			p += part;
			size -= part;
		}
		NTSTATUS st = BCryptFinishHash( h, out.data(), ULONG( out.size() ), 0 );
		BCryptDestroyHash( h );
		if ( !BCRYPT_SUCCESS( st ) ) fail( "BCryptFinishHash failed" );
		return out;
	}
	std::array<uint8_t, 32> sha256( const void* data, size_t size ) { return cng_hash( false, nullptr, 0, data, size ); }
	std::string sha256_hex( const Bytes& b )
	{
		auto h = sha256( b.data(), b.size() );
		return hex_of( h.data(), h.size(), true );
	}
	std::array<uint8_t, 32> hmac_sha256( const Bytes& key, const std::string& message ) { return cng_hash( true, key.data(), key.size(), message.data(), message.size() ); }
	Bytes random_bytes( size_t n )
	{
		Bytes b( n );
		if ( !BCRYPT_SUCCESS( BCryptGenRandom( nullptr, b.data(), ULONG( n ), BCRYPT_USE_SYSTEM_PREFERRED_RNG ) ) ) fail( "BCryptGenRandom failed" );
		return b;
	}

	// ── SyntheticStream ───────────────────────────────────────────────────────────────────────────────────────────────────
	// block k = HMAC-SHA256( seed, "emu-profilegen/1|<env>|<label>|<k as 8 hex digits>" ). The label names a field location
	// (environment + field id), never a value, so no synthetic byte depends on a real value.
	SyntheticStream::SyntheticStream( const Bytes& seed, Environment env, const std::string& label )
		: key_( seed ), prefix_( std::string( "emu-profilegen/1|" ) + env_dir_name( env ) + "|" + label + "|" )
	{
		if ( seed.size() != 32 ) fail( "seed must be 32 bytes" );
	}
	SyntheticStream SyntheticStream::shared( const Bytes& seed, const std::string& label )
	{
		SyntheticStream s( seed, Environment::vmware_guest, label );
		s.prefix_ = "emu-profilegen/1|shared|" + label + "|";	// the same stream for both environments
		return s;
	}
	uint8_t SyntheticStream::next_byte()
	{
		if ( pos_ == block_.size() )
		{
			block_ = hmac_sha256( key_, prefix_ + fmt( "%08X", counter_++ ) );
			pos_ = 0;
		}
		return block_[ pos_++ ];
	}
	char SyntheticStream::digit() { for ( ;; ) { uint8_t b = next_byte(); if ( b < 250 ) return char( '0' + b % 10 ); } }
	char SyntheticStream::upper() { for ( ;; ) { uint8_t b = next_byte(); if ( b < 234 ) return char( 'A' + b % 26 ); } }
	char SyntheticStream::lower() { for ( ;; ) { uint8_t b = next_byte(); if ( b < 234 ) return char( 'a' + b % 26 ); } }
	char SyntheticStream::hex( bool upper_case ) { return ( upper_case ? "0123456789ABCDEF" : "0123456789abcdef" )[ next_byte() & 15 ]; }
	char SyntheticStream::alnum_upper()
	{
		for ( ;; )
		{
			uint8_t b = next_byte();
			if ( b < 252 ) return "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[ b % 36 ];
		}
	}
	uint64_t SyntheticStream::next_u64()
	{
		uint64_t r = 0;
		for ( int i = 0; i < 8; i++ ) r = r << 8 | next_byte();
		return r;
	}

	// ── draw primitives (internal; every gen_ below uses them on its own stream) ────────────────────────────────────────────
	// rule-2 classes: same length, same character class per position (digit / A-Z / a-z), every other character kept
	static std::string draw_classes( const std::string& real, SyntheticStream& s )
	{
		if ( std::none_of( real.begin(), real.end(), is_alnum ) ) fail( "rule 2: a value without letters or digits cannot be replaced" );
		for ( ;; )
		{
			std::string out = real;
			for ( char& c : out )
			{
				if ( c >= '0' && c <= '9' ) c = s.digit();
				else if ( c >= 'A' && c <= 'Z' ) c = s.upper();
				else if ( c >= 'a' && c <= 'z' ) c = s.lower();
			}
			if ( out != real ) return out;	// equal to the real value: draw again from the same stream
		}
	}
	// hex digits in the value's case (lower when it has no a-f letters... unless all upper), other characters kept
	static std::string draw_hex( const std::string& real, SyntheticStream& s, bool keep_digit_class )
	{
		if ( std::none_of( real.begin(), real.end(), is_hex ) ) fail( "a value without hex digits cannot be replaced" );
		bool up = std::any_of( real.begin(), real.end(), []( char c ) { return c >= 'A' && c <= 'F'; } ) || !has_lower_hex( real );
		if ( has_lower_hex( real ) ) up = false;
		for ( ;; )
		{
			std::string o = real;
			for ( char& c : o )
			{
				if ( !is_hex( c ) ) continue;
				if ( keep_digit_class && c >= '0' && c <= '9' ) c = s.digit();
				else if ( keep_digit_class ) for ( ;; ) { uint8_t b = s.next_byte(); if ( b < 252 ) { c = char( ( c >= 'a' ? 'a' : 'A' ) + b % 6 ); break; } }
				else c = s.hex( up );
			}
			if ( !ieq( o, real ) ) return o;
		}
	}
	static bool parse_mac_text( const std::string& t, uint8_t m[ 6 ] )
	{
		if ( t.size() != 17 ) return false;
		for ( int i = 0; i < 6; i++ )
		{
			if ( !is_hex( t[ i * 3 ] ) || !is_hex( t[ i * 3 + 1 ] ) || ( i < 5 && t[ i * 3 + 2 ] != ':' ) ) return false;
			m[ i ] = uint8_t( std::stoul( t.substr( i * 3, 2 ), nullptr, 16 ) );
		}
		return true;
	}
	static uint64_t mac_value( const std::string& t )
	{
		uint64_t v = 0;
		for ( char c : t ) if ( is_hex( c ) ) v = v << 4 | uint64_t( std::stoul( std::string( 1, c ), nullptr, 16 ) );
		return v;
	}
	static std::string mac_text( uint64_t v )
	{
		std::string t;
		for ( int i = 5; i >= 0; i-- ) t += fmt( "%02X", unsigned( v >> ( i * 8 ) & 0xFF ) ) + ( i ? ":" : "" );
		return t;
	}
	static std::string mac_plain( const std::string& t ) { std::string p; for ( char c : lower( t ) ) if ( c != ':' ) p += c; return p; }
	// VMware Workstation's host-only / NAT adapters: the fixed pattern 00:50:56:C0:00:<VMnet number> (kept exact)
	static bool is_vmnet_mac( const std::string& t ) { return lower( t ).rfind( "00:50:56:c0:00:", 0 ) == 0; }

	// ── SMBIOS parser (RawSMBIOSData; structure layout: phnt smbios.h, System Informer) ─────────────────────────────────
	SmbiosTable parse_smbios( const Bytes& raw )
	{
		SmbiosTable t;
		if ( raw.size() < 8 ) fail( "RawSMBIOSData shorter than its 8-byte header" );
		t.major = raw[ 1 ];
		t.minor = raw[ 2 ];
		t.length = uint32_t( raw[ 4 ] ) | uint32_t( raw[ 5 ] ) << 8 | uint32_t( raw[ 6 ] ) << 16 | uint32_t( raw[ 7 ] ) << 24;
		if ( raw.size() != size_t( 8 ) + t.length ) fail( fmt( "RawSMBIOSData Length %u does not match the blob (%zu bytes)", t.length, raw.size() ) );
		const size_t end = raw.size();
		size_t i = 8;
		while ( i < end )
		{
			if ( i + 4 > end ) fail( fmt( "SMBIOS structure header at 0x%04zX runs past the table", i ) );
			SmbiosStructure s;
			s.offset = i;
			s.type = raw[ i ];
			s.length = raw[ i + 1 ];
			s.handle = uint16_t( raw[ i + 2 ] | raw[ i + 3 ] << 8 );
			if ( s.length < 4 || i + s.length > end ) fail( fmt( "SMBIOS structure at 0x%04zX: bad formatted length 0x%02X", i, s.length ) );
			size_t j = i + s.length;
			if ( j + 1 < end && raw[ j ] == 0 && raw[ j + 1 ] == 0 ) j += 2;	// no strings
			else
			{
				for ( ;; )
				{
					size_t k = j;
					while ( k < end && raw[ k ] ) k++;
					if ( k >= end ) fail( fmt( "SMBIOS structure at 0x%04zX: string set not terminated", i ) );
					s.strings.push_back( { j, k - j } );
					j = k + 1;
					if ( j >= end ) fail( fmt( "SMBIOS structure at 0x%04zX: string set not terminated", i ) );
					if ( raw[ j ] == 0 ) { j++; break; }
				}
			}
			s.end = j;
			t.structures.push_back( s );
			i = j;
			if ( s.type == 127 ) break;	// end-of-table; any bytes after it are copied as they are
		}
		return t;
	}

	// known string-reference offsets of the formatted areas this tool inspects (phnt smbios.h)
	static std::vector<uint8_t> string_fields( uint8_t type )
	{
		switch ( type )
		{
		case 1: return { 0x04, 0x05, 0x06, 0x07, 0x19, 0x1A };			// Manufacturer, ProductName, Version, SerialNumber, SKUNumber, Family
		case 2: return { 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A };			// Manufacturer, Product, Version, SerialNumber, AssetTag, Location
		case 3: return { 0x04, 0x06, 0x07, 0x08 };						// Manufacturer, Version, SerialNumber, AssetTag
		case 4: return { 0x04, 0x07, 0x10, 0x20, 0x21, 0x22 };			// Socket, Manufacturer, Version, SerialNumber, AssetTag, PartNumber
		case 17: return { 0x10, 0x11, 0x17, 0x18, 0x19, 0x1A, 0x2B };	// DeviceLocator, BankLocator, Manufacturer, SerialNumber, AssetTag, PartNumber, FirmwareVersion
		case 39: return { 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B };	// Location, DeviceName, Manufacturer, SerialNumber, AssetTag, ModelPartNumber, Revision
		default: return {};
		}
	}

	// ── validators ────────────────────────────────────────────────────────────────────────────────────────────────────────
	bool ProfileGenerator::check_filler( const std::string& s0 )
	{
		std::string s = s0;
		while ( !s.empty() && ( s.back() == ' ' || s.back() == '\t' ) ) s.pop_back();
		// the generic firmware filler of rule 2 / 2h fields (case-insensitive, trailing blanks ignored; empty = filler); the last
		// two are placeholder serial / asset-tag values (user decision 2026-10-16)
		static const char* k[] = { "Default string", "To Be Filled By O.E.M.", "None", "No Asset Tag", "N/A", "Not Specified", "Unknown", "NO DIMM",
			"Empty", "Not Available", "Not Installed", "Undefined", "00000000", "9876543210" };
		if ( s.empty() ) return true;
		for ( const char* f : k )
			if ( ieq( s, f ) ) return true;
		return false;
	}
	bool ProfileGenerator::check_usb_placeholder( const std::string& s )
	{
		if ( !s.empty() && std::all_of( s.begin(), s.end(), []( char c ) { return c == '0'; } ) ) return true;	// all '0'
		return s.size() > 6 && ieq( s.substr( 0, 6 ), "MSFT30" ) && std::all_of( s.begin() + 6, s.end(), []( char c ) { return c == '0'; } );
	}

	// ── ProfileGenerator ──────────────────────────────────────────────────────────────────────────────────────────────────
	ProfileGenerator::ProfileGenerator( BuildConfig c ) : c_( std::move( c ) ) {}

	Bytes ProfileGenerator::seed_bytes() const
	{
		if ( c_.seed.size() != 64 || !std::all_of( c_.seed.begin(), c_.seed.end(), is_hex ) ) fail( "the build config has no seed (64 hex digits)" );
		Bytes s;
		for ( size_t i = 0; i < 64; i += 2 ) s.push_back( uint8_t( std::stoul( c_.seed.substr( i, 2 ), nullptr, 16 ) ) );
		return s;
	}
	SyntheticStream ProfileGenerator::stream( const std::string& label ) const { return SyntheticStream( seed_bytes(), c_.env, label ); }

	Seed ProfileGenerator::gen_new_seed()
	{
		Seed s;
		Bytes b = random_bytes( 32 );
		std::copy( b.begin(), b.end(), s.bytes.begin() );
		s.hex = hex_of( b.data(), b.size(), true );
		c_.seed = s.hex;	// a fresh identity: every synthetic value of this generator changes
		return s;
	}

	// rule 1 (see the header for the exact byte ranges in both captures)
	SmbiosUuid ProfileGenerator::gen_system_uuid( const SmbiosUuid& real )
	{
		SyntheticStream s = stream( fmt( "smbios/t1/h%04X/uuid", real.handle ) );
		SmbiosUuid out = real;
		const uint8_t* r = real.bytes.data();
		uint8_t* o = out.bytes.data();
		for ( ;; )
		{
			for ( int i = 0; i < 16; i++ ) o[ i ] = s.next_byte();
			if ( c_.env == Environment::vmware_guest )
			{
				// VMware layout: VMware's fixed prefix 56h 4Dh ("VM") in bytes 0-1, the rest synthetic. Run 3 guest: type 1
				// handle 0x0001 UUID at smbios.bin 0x0066..0x0075, prefix bytes 0x0066..0x0067 kept.
				if ( r[ 0 ] != 0x56 || r[ 1 ] != 0x4D ) fail( "rule 1: the guest UUID does not have VMware's 56 4D layout" );
				o[ 0 ] = 0x56;
				o[ 1 ] = 0x4D;
			}
			else
			{
				// RFC 4122 layout: version nibble under both byte orders (byte 6 bits 7:4, byte 7 bits 7:4) and the variant bits
				// of byte 8 kept exactly as in the real UUID. Bare host: type 1 handle 0x0001 UUID at smbios.bin 0x0063..0x0072.
				o[ 6 ] = uint8_t( ( r[ 6 ] & 0xF0 ) | ( o[ 6 ] & 0x0F ) );
				o[ 7 ] = uint8_t( ( r[ 7 ] & 0xF0 ) | ( o[ 7 ] & 0x0F ) );
				uint8_t keep = r[ 8 ] < 0x80 ? 0x80 : r[ 8 ] < 0xC0 ? 0xC0 : 0xE0;
				o[ 8 ] = uint8_t( ( r[ 8 ] & keep ) | ( o[ 8 ] & ~keep ) );
			}
			if ( memcmp( o, r, 16 ) != 0 ) return out;
		}
	}

	// rules 2 / 2h: rule-2 classes on one SMBIOS string, stream "smbios/t<type>/h<handle>/o<offset>"
	SmbiosStringField ProfileGenerator::gen_smbios_string( const SmbiosStringField& real )
	{
		SyntheticStream s = stream( fmt( "smbios/t%u/h%04X/o%02X", real.type, real.handle, real.offset ) );
		SmbiosStringField out = real;
		out.value = draw_classes( real.value, s );
		return out;
	}

	// the text forms a UUID can take (leak guard): Windows order (Data1-3 little-endian), byte order, plain hex
	static void uuid_needles( const uint8_t u[ 16 ], std::vector<std::string>& out )
	{
		uint8_t le[ 16 ] = { u[ 3 ], u[ 2 ], u[ 1 ], u[ 0 ], u[ 5 ], u[ 4 ], u[ 7 ], u[ 6 ] };
		memcpy( le + 8, u + 8, 8 );
		const uint8_t* forms[ 2 ] = { le, u };
		for ( const uint8_t* p : forms )
		{
			std::string h = hex_of( p, 16, false );
			out.push_back( h );
			out.push_back( h.substr( 0, 8 ) + "-" + h.substr( 8, 4 ) + "-" + h.substr( 12, 4 ) + "-" + h.substr( 16, 4 ) + "-" + h.substr( 20 ) );
		}
	}

	// ── gen_smbios: rules 1, 2, 2h, 2o + guard on the RawSMBIOSData blob ──────────────────────────────────────────────────
	SmbiosGen ProfileGenerator::gen_smbios( const SmbiosInput& in )
	{
		const Bytes& raw = in.raw;
		const SmbiosTable t = parse_smbios( raw );
		SmbiosGen g;
		g.synthetic = raw;
		Bytes& out = g.synthetic;
		const RuleSet& rules = c_.rules;
		std::map<std::string, std::string> mapped;	// real value -> synthetic value, rules 2 / 2h (equal real values, equal synthetic values)
		int fid = 0;
		auto next_id = [ & ]() { return fmt( "F%02d", ++fid ); };
		auto str_at = [ & ]( const SmbiosStructure& s, uint8_t off ) -> const SmbiosString* {
			if ( off >= s.length ) return nullptr;
			uint8_t idx = raw[ s.offset + off ];
			return idx && idx <= s.strings.size() ? &s.strings[ idx - 1 ] : nullptr;
		};
		struct field { uint8_t type, off; const char* name; };
		// Guard: undecided identifier fields - filler or no string, else stop (both clean captures: filler / empty)
		static const field k_guard[] = { { 1, 0x07, "system SerialNumber" }, { 1, 0x19, "system SKUNumber" }, { 2, 0x08, "board AssetTag" },
			{ 3, 0x07, "chassis SerialNumber" }, { 3, 0x08, "chassis AssetTag" }, { 4, 0x20, "processor SerialNumber" },
			{ 4, 0x21, "processor AssetTag" }, { 39, 0x08, "power supply SerialNumber" }, { 39, 0x09, "power supply AssetTag" } };
		// Rule 2. Run 3 guest: type 17 handle 0x0031 SerialNumber (string 2, smbios.bin 0x09BB, 8 bytes); the board serial
		// (type 2 handle 0x0002) is filler. Bare host: type 2 handle 0x0002 SerialNumber (string 4, 0x012A, 18 bytes); the type
		// 17 serial / asset-tag strings hold placeholder filler.
		static const field k_rule2[] = { { 2, 0x07, "board SerialNumber" }, { 17, 0x18, "memory SerialNumber" }, { 17, 0x19, "memory AssetTag" } };
		// Rule 2h (bare_host ONLY): the type 17 name strings. Bare host: handles 0x003B (at 0x07A2) / 0x003D (at 0x08CC):
		// DeviceLocator string 1 (0x07FE / 0x0928, 18 bytes), BankLocator string 2 (0x0811 / 0x093B, 6 bytes); handles 0x003C
		// (at 0x0824) / 0x003E (at 0x094E): DeviceLocator string 1 (0x0880 / 0x09AA, 18), BankLocator string 2 (0x0893 / 0x09BD,
		// 6), Manufacturer string 3 (0x089A / 0x09C4, 7), PartNumber string 6 (0x08B6 / 0x09E0, 20). vmware_guest: not applied.
		static const field k_rule2h[] = { { 17, 0x10, "memory DeviceLocator" }, { 17, 0x11, "memory BankLocator" }, { 17, 0x17, "memory Manufacturer" },
			{ 17, 0x1A, "memory PartNumber" } };

		for ( const SmbiosStructure& s : t.structures )
		{
			// Rule 1: type 1 UUID, formatted offset 0x08, 16 bytes (Run 3 guest: 0x0066..0x0075; bare host: 0x0063..0x0072)
			if ( s.type == 1 && s.length >= 0x18 )
			{
				const uint8_t* real = &raw[ s.offset + 8 ];
				bool unset = std::all_of( real, real + 16, []( uint8_t b ) { return b == 0; } ) || std::all_of( real, real + 16, []( uint8_t b ) { return b == 0xFF; } );
				if ( !unset )
				{
					g.binary_needles.push_back( Bytes( real, real + 16 ) );	// leak guard, also when rule 1 is disabled
					uuid_needles( real, g.needles );
				}
				if ( !unset && rules.r1 )
				{
					SmbiosUuid ru;
					ru.handle = s.handle;
					std::copy( real, real + 16, ru.bytes.begin() );
					SmbiosUuid su = gen_system_uuid( ru );
					// UUID-MAC relation: the real UUID's last 6 bytes = an adapter MAC of the capture -> the synthetic UUID's last 6
					// bytes = that adapter's synthetic MAC. Bare host: bytes 10-15 (smbios.bin 0x006D..0x0072) = the onboard wired
					// NIC's MAC. Run 3 guest: no relation.
					std::string tied;
					for ( const MacPair& m : in.macs )
					{
						uint8_t rm[ 6 ], sm[ 6 ];
						if ( !parse_mac_text( m.real.text, rm ) || !parse_mac_text( m.synthetic.text, sm ) || memcmp( real + 10, rm, 6 ) != 0 ) continue;
						memcpy( su.bytes.data() + 10, sm, 6 );
						su.tail_is_mac = true;
						su.tail_adapter = m.real.adapter;
						tied = "; bytes 10-15 = the synthetic MAC of identity " + m.real.id + " (adapter \"" + m.real.adapter + "\"), the UUID-MAC relation of the capture kept";
						break;
					}
					std::copy( su.bytes.begin(), su.bytes.end(), out.begin() + s.offset + 8 );
					g.fields.push_back( { "smbios.bin", next_id(), c_.env == Environment::vmware_guest ? "1-vmware" : "1-rfc4122",
						fmt( "type 1 handle 0x%04X formatted offset 0x08: system UUID (%s%s)", s.handle,
							c_.env == Environment::vmware_guest ? "bytes 0-1 VMware prefix kept" : "version nibbles of bytes 6/7 and the variant bits of byte 8 kept", tied.c_str() ),
						s.offset + 8, 0, 16 } );
				}
			}
			for ( const field& gd : k_guard )
			{
				if ( gd.type != s.type ) continue;
				if ( const SmbiosString* p = str_at( s, gd.off ) )
					if ( !check_filler( to_string( raw, p->offset, p->length ) ) )
						fail( fmt( "undecided identifier field: SMBIOS type %u handle 0x%04X %s (formatted offset 0x%02X, %zu bytes at 0x%04zX) holds a non-filler value - it needs a field decision",
							s.type, s.handle, gd.name, gd.off, p->length, p->offset ) );
			}
			std::vector<std::pair<field, const char*>> decided;
			for ( const field& f : k_rule2 ) decided.emplace_back( f, "2" );
			if ( c_.env == Environment::bare_host )
				for ( const field& f : k_rule2h ) decided.emplace_back( f, "2h" );
			for ( auto& [ f, rule ] : decided )
			{
				if ( f.type != s.type ) continue;
				const SmbiosString* p = str_at( s, f.off );
				if ( !p ) continue;	// no string (empty slot / not present)
				std::string v = to_string( raw, p->offset, p->length );
				if ( check_filler( v ) ) continue;	// firmware filler: unchanged
				g.needles.push_back( v );	// leak guard, also when the rule is disabled
				if ( rule == std::string( "2" ) ? !rules.r2 : !rules.r2h ) continue;
				uint8_t idx = raw[ s.offset + f.off ];
				for ( uint8_t o : string_fields( s.type ) )
					if ( o != f.off && o < s.length && raw[ s.offset + o ] == idx )
						fail( fmt( "SMBIOS type %u handle 0x%04X: the %s string is shared with formatted offset 0x%02X - replacing it would change another field",
							s.type, s.handle, f.name, o ) );
				auto it = mapped.find( v );
				if ( it == mapped.end() ) it = mapped.emplace( v, gen_smbios_string( { s.type, f.off, s.handle, v } ).value ).first;
				memcpy( &out[ p->offset ], it->second.data(), p->length );
				g.fields.push_back( { "smbios.bin", next_id(), rule, fmt( "type %u handle 0x%04X formatted offset 0x%02X -> string %u: %s", s.type, s.handle, f.off, idx, f.name ),
					p->offset, 0, uint32_t( p->length ) } );
			}
		}
		// Rule 2o: unreferenced strings of type 2 / 17 structures equal to a rule-2 / 2h real value (no hit in either capture)
		for ( const SmbiosStructure& s : t.structures )
		{
			if ( !rules.r2o || ( s.type != 2 && s.type != 17 ) ) continue;
			std::set<uint8_t> referenced;
			for ( uint8_t o : string_fields( s.type ) )
				if ( o < s.length ) referenced.insert( raw[ s.offset + o ] );
			for ( size_t k = 0; k < s.strings.size(); k++ )
			{
				if ( referenced.count( uint8_t( k + 1 ) ) ) continue;
				auto it = mapped.find( to_string( raw, s.strings[ k ].offset, s.strings[ k ].length ) );
				if ( it == mapped.end() ) continue;
				memcpy( &out[ s.strings[ k ].offset ], it->second.data(), s.strings[ k ].length );
				g.fields.push_back( { "smbios.bin", next_id(), "2o", fmt( "type %u handle 0x%04X unreferenced string %zu: copy of a rule-2 / 2h value", s.type, s.handle, k + 1 ),
					s.strings[ k ].offset, 0, uint32_t( s.strings[ k ].length ) } );
			}
		}
		return g;
	}

	// CHID check: the SMBIOS fields Windows' ComputerHardwareIds are computed from (type 0 Vendor 0x04 / Version 0x05 /
	// System BIOS Major 0x14 / Minor 0x15 release, type 1 Manufacturer 0x04 / ProductName 0x05 / SKUNumber 0x19 / Family 0x1A,
	// type 2 Manufacturer 0x04 / Product 0x05, type 3 enclosure type 0x05; field list from Microsoft's ComputerHardwareIds
	// description, not a local document) must be unchanged in the synthetic table
	ProfileGenerator::ChidCheck ProfileGenerator::check_chid_fields( const Bytes& a, const Bytes& b )
	{
		struct cf { uint8_t type, off; bool str; };
		static const cf k[] = { { 0, 0x04, true }, { 0, 0x05, true }, { 0, 0x14, false }, { 0, 0x15, false }, { 1, 0x04, true }, { 1, 0x05, true },
			{ 1, 0x19, true }, { 1, 0x1A, true }, { 2, 0x04, true }, { 2, 0x05, true }, { 3, 0x05, false } };
		const SmbiosTable ta = parse_smbios( a ), tb = parse_smbios( b );
		ChidCheck r;
		for ( size_t i = 0; i < ta.structures.size() && i < tb.structures.size(); i++ )
		{
			const SmbiosStructure &x = ta.structures[ i ], &y = tb.structures[ i ];
			for ( const cf& f : k )
			{
				if ( f.type != x.type || f.off >= x.length ) continue;
				r.checked++;
				if ( a[ x.offset + f.off ] != b[ y.offset + f.off ] ) { r.changed++; continue; }
				uint8_t idx = a[ x.offset + f.off ];
				if ( f.str && idx && idx <= x.strings.size() && idx <= y.strings.size() )
					if ( to_string( a, x.strings[ idx - 1 ].offset, x.strings[ idx - 1 ].length ) != to_string( b, y.strings[ idx - 1 ].offset, y.strings[ idx - 1 ].length ) ) r.changed++;
			}
		}
		return r;
	}

	// ── single-value generators ──────────────────────────────────────────────────────────────────────────────────────────
	// rule 3: one base MAC - OUI (bytes 0-2) exact, bytes 3-5 synthetic; stream "identity/<id>"
	MacAddress ProfileGenerator::gen_mac( const MacAddress& real )
	{
		uint8_t m[ 6 ];
		if ( !parse_mac_text( real.text, m ) ) fail( "rule 3: not a MAC in XX:XX:XX:XX:XX:XX form" );
		SyntheticStream s = stream( "identity/" + real.id );
		for ( ;; )
		{
			uint8_t o[ 6 ] = { m[ 0 ], m[ 1 ], m[ 2 ], s.next_byte(), s.next_byte(), s.next_byte() };
			if ( memcmp( o, m, 6 ) == 0 ) continue;
			MacAddress out = real;
			out.text.clear();
			for ( int i = 0; i < 6; i++ ) out.text += ( i ? ":" : "" ) + hex_of( &o[ i ], 1, true );
			return out;
		}
	}

	// rule 3: the MAC family of one capture. Detects the relations generically - the locally administered variant (byte 0 bit
	// 1 toggled) of another MAC, or + delta (<= 16) within the same OUI from the lowest MAC (the base) - draws one synthetic
	// MAC per base (its tail leaves room for the largest delta) and re-derives every dependent with the same relation
	MacFamily ProfileGenerator::gen_mac_family( const MacFamily& real )
	{
		MacFamily f = real;
		std::vector<MacMember>& m = f.members;
		std::map<uint64_t, size_t> by_value;
		for ( size_t i = 0; i < m.size(); i++ ) by_value[ mac_value( m[ i ].mac.text ) ] = i;
		std::vector<size_t> universal;
		for ( size_t i = 0; i < m.size(); i++ )
		{
			uint64_t v = mac_value( m[ i ].mac.text );
			auto it = by_value.find( v ^ ( uint64_t( 2 ) << 40 ) );
			if ( ( v >> 41 & 1 ) && it != by_value.end() && !( mac_value( m[ it->second ].mac.text ) >> 41 & 1 ) ) { m[ i ].parent = int( it->second ); m[ i ].locally_administered = true; }
			else universal.push_back( i );
		}
		std::sort( universal.begin(), universal.end(), [ & ]( size_t a, size_t b ) { return mac_value( m[ a ].mac.text ) < mac_value( m[ b ].mac.text ); } );
		for ( size_t k = 0; k < universal.size(); k++ )
		{
			size_t i = universal[ k ];
			uint64_t v = mac_value( m[ i ].mac.text );
			for ( size_t j = 0; j < k; j++ )
			{
				size_t b = universal[ j ];
				uint64_t bv = mac_value( m[ b ].mac.text );
				if ( m[ b ].parent < 0 && ( bv >> 24 ) == ( v >> 24 ) && v - bv <= 16 ) { m[ i ].parent = int( b ); m[ i ].delta = (long long)( v - bv ); break; }
			}
		}
		std::vector<uint64_t> syn( m.size(), 0 );
		for ( size_t i : universal )
		{
			if ( m[ i ].parent >= 0 ) continue;
			long long maxd = 0;
			for ( size_t j : universal ) if ( m[ j ].parent == int( i ) ) maxd = std::max( maxd, m[ j ].delta );
			MacAddress probe = m[ i ].mac;
			for ( int attempt = 0;; attempt++ )
			{
				if ( attempt ) probe.id = m[ i ].mac.id + fmt( "/retry%d", attempt );
				uint64_t v = mac_value( gen_mac( probe ).text );
				if ( ( v & 0xFFFFFF ) + uint64_t( maxd ) <= 0xFFFFFF ) { syn[ i ] = v; break; }
			}
		}
		for ( size_t i : universal ) if ( m[ i ].parent >= 0 ) syn[ i ] = syn[ m[ i ].parent ] + uint64_t( m[ i ].delta );
		for ( size_t i = 0; i < m.size(); i++ ) if ( m[ i ].locally_administered ) syn[ i ] = syn[ m[ i ].parent ] ^ ( uint64_t( 2 ) << 40 );
		for ( size_t i = 0; i < m.size(); i++ )
		{
			if ( ieq( mac_text( syn[ i ] ), real.members[ i ].mac.text ) ) fail( "rule 3: a derived MAC equals its real value" );
			m[ i ].mac.text = mac_text( syn[ i ] );
		}
		return f;
	}

	// rule 3: the EUI-64 form of a NIC's MAC in an instance ID, re-derived from the NIC's synthetic MAC (the I226-V's PCI
	// instance ID on the bare host: OUI + FFFF + NIC part + 00)
	Eui64Id ProfileGenerator::gen_eui64_instance_id( const Eui64Id& real, const MacAddress& nic )
	{
		std::string sp = fmt( "%012llX", (unsigned long long)mac_value( nic.text ) );
		Eui64Id out = real;
		out.text = sp.substr( 0, 6 ) + ( real.ffff ? "FFFF" : "FFFE" ) + sp.substr( 6 ) + ( real.ffff ? "00" : "" );
		if ( has_lower_hex( real.text ) ) out.text = lower( out.text );
		return out;
	}

	// rule 4c: GUID text, braces / hyphens / case kept; hex digit 12 (the version) kept; digit 16 keeps its variant bits
	// (0xxx NCS: bit 3; 10xx RFC 4122: bits 3:2; 110x / 111x Microsoft / reserved: bits 3:1); all other digits synthetic.
	// Version-1 (time-based) GUIDs: when the node (last 12 hex digits) is an adapter MAC, the node becomes that adapter's
	// synthetic MAC; Windows' no-NIC node 806E6F6E6963 stays exact. MBR-style volume GUIDs {SSSSSSSS-0000-0000-0000-OFFSET}:
	// only the disk signature (the first 8 digits) is synthetic.
	GuidValue ProfileGenerator::gen_guid( const GuidValue& real, const NodeMap& nodes )
	{
		static const std::regex k_guid( R"(\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?)" );
		const std::string& g = real.text;
		if ( !std::regex_match( g, k_guid ) ) fail( "rule 4c: not a GUID" );
		SyntheticStream s = stream( real.label );
		bool up = !has_lower_hex( g );
		std::vector<size_t> hp;
		for ( size_t i = 0; i < g.size(); i++ ) if ( is_hex( g[ i ] ) ) hp.push_back( i );
		bool mbr = true;
		for ( size_t k = 8; k < 20; k++ ) mbr &= g[ hp[ k ] ] == '0';
		std::string node;
		for ( size_t k = 20; k < 32; k++ ) node += char( std::tolower( uint8_t( g[ hp[ k ] ] ) ) );
		for ( ;; )
		{
			std::string out = g;
			for ( size_t k = 0; k < 32; k++ )
			{
				char& c = out[ hp[ k ] ];
				if ( k == 12 || ( mbr && k >= 8 ) ) continue;
				char r = s.hex( up );
				if ( k == 16 )
				{
					unsigned v = unsigned( std::stoul( std::string( 1, g[ hp[ k ] ] ), nullptr, 16 ) ), x = unsigned( std::stoul( std::string( 1, r ), nullptr, 16 ) );
					unsigned keep = v < 8 ? 0x8 : v < 12 ? 0xC : 0xE;
					r = ( up ? "0123456789ABCDEF" : "0123456789abcdef" )[ ( v & keep ) | ( x & ~keep & 15 ) ];
				}
				c = r;
			}
			if ( !mbr && g[ hp[ 12 ] ] == '1' )
			{
				auto it = nodes.real_to_synthetic.find( node );
				if ( it != nodes.real_to_synthetic.end() )
					for ( size_t k = 20; k < 32; k++ ) out[ hp[ k ] ] = up ? char( std::toupper( uint8_t( it->second[ k - 20 ] ) ) ) : it->second[ k - 20 ];
				else if ( node == "806e6f6e6963" )
					for ( size_t k = 20; k < 32; k++ ) out[ hp[ k ] ] = g[ hp[ k ] ];
			}
			if ( !ieq( out, g ) ) return { out, real.label };
		}
	}

	// rule 4d: a USB / HID device serial. Hex-encoded ASCII (even length >= 8, every byte printable) stays hex-encoded ASCII of
	// the same shape (the decoded text gets the rule-2 classes, re-encoded in the original's hex case); any other all-hex
	// serial stays hex (digit -> digit, A-F -> A-F, a-f -> a-f); other serials get the rule-2 classes. table_index >= 0: the
	// stream shared by both profiles ("shared|usb/<index>"), else this environment's stream
	UsbSerial ProfileGenerator::gen_usb_serial( const UsbSerial& real )
	{
		SyntheticStream s = real.table_index >= 0 ? SyntheticStream::shared( seed_bytes(), fmt( "usb/%ld", real.table_index ) ) : stream( real.label );
		const std::string& r = real.text;
		UsbSerial out = real;
		bool hex = r.size() >= 2 && std::all_of( r.begin(), r.end(), is_hex );
		bool up = !has_lower_hex( r );
		if ( hex && r.size() % 2 == 0 && r.size() >= 8 )
		{
			std::string txt;
			for ( size_t i = 0; i < r.size(); i += 2 ) txt += char( std::stoul( r.substr( i, 2 ), nullptr, 16 ) );
			if ( std::all_of( txt.begin(), txt.end(), []( char c ) { return c >= 0x20 && c <= 0x7E; } ) && std::any_of( txt.begin(), txt.end(), is_alnum ) )
			{
				std::string t = draw_classes( txt, s );
				out.text.clear();
				for ( char c : t ) out.text += hex_of( reinterpret_cast<const uint8_t*>( &c ), 1, up );
				return out;
			}
		}
		out.text = hex ? draw_hex( r, s, true ) : draw_classes( r, s );
		return out;
	}

	// rule 4a: "DESKTOP-" + 7 synthetic characters [A-Z0-9]
	ComputerName ProfileGenerator::gen_computer_name( const ComputerName& real )
	{
		SyntheticStream s = stream( real.label );
		for ( ;; )
		{
			std::string n = "DESKTOP-";
			for ( int i = 0; i < 7; i++ ) n += s.alnum_upper();
			if ( !ieq( n, real.text ) ) return { n, real.label };
		}
	}

	TextValue ProfileGenerator::gen_class_string( const TextValue& real )
	{
		SyntheticStream s = stream( real.label );
		return { draw_classes( real.text, s ), real.label };
	}
	TextValue ProfileGenerator::gen_hex_token( const TextValue& real )
	{
		SyntheticStream s = stream( real.label );
		return { draw_hex( real.text, s, false ), real.label };
	}

	// rule 8a: the installation ID - same length, digits stay digits, the group layout (separators) kept; the internal check
	// digits are NOT modelled (no local, citable description of the scheme)
	WinInstallationId ProfileGenerator::gen_win_installation_id( const WinInstallationId& real )
	{
		if ( std::none_of( real.text.begin(), real.text.end(), []( char c ) { return c >= '0' && c <= '9'; } ) ) fail( "rule 8a: an installation ID without digits" );
		WinInstallationId out = real;
		SyntheticStream s = stream( real.label );
		out.text = draw_classes( real.text, s );
		out.group_lengths.clear();
		size_t run = 0;
		for ( char c : real.text + " " )
		{
			if ( c >= '0' && c <= '9' ) run++;
			else if ( run ) { out.group_lengths.push_back( run ); run = 0; }
		}
		out.check_digits_modelled = false;
		return out;
	}

	// rule 7a: ProductId "ddddd-ddddd-ddddd-AAddd": the first group (product / edition) and the last group (channel) exact,
	// the middle groups rule-2 classes
	ProductId ProfileGenerator::gen_product_id( const ProductId& real )
	{
		size_t a = real.text.find( '-' ), b = real.text.rfind( '-' );
		if ( a == std::string::npos || a == b ) fail( "rule 7a: a ProductId without middle groups" );
		SyntheticStream s = stream( real.label );
		return { real.text.substr( 0, a + 1 ) + draw_classes( real.text.substr( a + 1, b - a - 1 ), s ) + real.text.substr( b ), real.label };
	}

	// rule 7b: the window for a synthetic install instant, strictly between the release of the OS build and the capture
	// (unix seconds). The release dates of the Windows 10 builds are the general-availability dates of versions 2004 .. 22H2;
	// another build needs an entry here (the run stops).
	InstallWindow ProfileGenerator::parse_install_window( unsigned build, const std::string& capture_utc )
	{
		auto days = []( int y, unsigned m, unsigned d ) {	// days since 1970-01-01 (proleptic Gregorian)
			y -= m <= 2;
			const int era = ( y >= 0 ? y : y - 399 ) / 400;
			const unsigned yoe = unsigned( y - era * 400 );
			const unsigned doy = ( 153 * ( m > 2 ? m - 3 : m + 9 ) + 2 ) / 5 + d - 1;
			const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
			return int64_t( era ) * 146097 + int64_t( doe ) - 719468;
		};
		struct rel { unsigned build; int y; unsigned m, d; };
		static const rel k[] = { { 19041, 2020, 5, 27 }, { 19042, 2020, 10, 20 }, { 19043, 2021, 5, 18 }, { 19044, 2021, 11, 16 }, { 19045, 2022, 10, 18 } };
		InstallWindow w { -1, 0 };
		for ( const rel& r : k ) if ( r.build == build ) w.lo = days( r.y, r.m, r.d ) * 86400;
		if ( w.lo < 0 ) fail( fmt( "rule 7b: no release date for OS build %u (profile_generator.cpp parse_install_window)", build ) );
		int y = 0;
		unsigned mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
		if ( sscanf( capture_utc.c_str(), "%d-%u-%uT%u:%u:%u", &y, &mo, &d, &hh, &mi, &ss ) != 6 ) fail( "rule 7b: unreadable capture time" );
		w.hi = days( y, mo, d ) * 86400 + hh * 3600 + mi * 60 + ss;
		if ( w.hi - w.lo < 3 ) fail( "rule 7b: the capture is not after the OS build release" );
		return w;
	}
	// rule 7b: ONE synthetic instant strictly inside the window; InstallDate (unix seconds) and InstallTime (FILETIME) agree
	InstallTime ProfileGenerator::gen_install_time( const InstallTime& real )
	{
		const InstallWindow w = parse_install_window( real.os_build, real.capture_utc );
		SyntheticStream s = stream( "identity/install" );
		for ( ;; )
		{
			uint64_t r = s.next_u64(), q = s.next_u64();
			InstallTime out = real;
			out.unix_seconds = w.lo + 1 + int64_t( r % uint64_t( w.hi - w.lo - 1 ) );
			out.filetime = ( uint64_t( out.unix_seconds ) + 11644473600ull ) * 10000000ull + q % 10000000ull;
			if ( out.unix_seconds != real.unix_seconds ) return out;
		}
	}

	// rule 7d: the machine / account-domain SID prefix "S-1-5-21-a-b-c": structure kept; the three sub-authorities synthetic,
	// each a decimal of the same digit count (no leading zero, at most 4294967295)
	SidParts ProfileGenerator::gen_sid( const SidParts& real )
	{
		static const std::regex r( R"(^S-1-5-21-([0-9]+)-([0-9]+)-([0-9]+)$)", std::regex::icase );
		std::smatch m;
		if ( !std::regex_match( real.prefix, m, r ) ) fail( "rule 7d: not an S-1-5-21-a-b-c SID prefix" );
		SyntheticStream s = stream( real.label );
		for ( ;; )
		{
			std::string o = real.prefix.substr( 0, 9 );
			for ( int k = 1; k <= 3; k++ )
			{
				size_t n = size_t( m[ k ].length() );
				for ( ;; )
				{
					std::string d;
					while ( d.size() < n ) { char c = s.digit(); if ( d.empty() && n > 1 && c == '0' ) continue; d += c; }
					if ( std::stoull( d ) <= 0xFFFFFFFFull ) { o += d + ( k < 3 ? "-" : "" ); break; }
				}
			}
			if ( !ieq( o, real.prefix ) ) return { o, real.label };
		}
	}

	// rule 7e: a volume serial "XXXX-XXXX": 8 synthetic hex digits in the original's case, the form kept
	VolumeId ProfileGenerator::gen_volume_serial( const VolumeId& real )
	{
		SyntheticStream s = stream( real.label );
		return { draw_hex( real.serial, s, false ), real.label };
	}

	// rule 6: the EDID numeric serial (bytes 12-15, little-endian). Four printable letters / digits (some vendors store text
	// there) get the rule-2 classes; any other value a synthetic number of the same decimal digit count
	EdidSerial ProfileGenerator::gen_edid_serial( const EdidSerial& real )
	{
		SyntheticStream s = stream( real.label );
		const uint32_t v = real.value;
		char c[ 4 ] = { char( v & 0xFF ), char( v >> 8 & 0xFF ), char( v >> 16 & 0xFF ), char( v >> 24 & 0xFF ) };
		if ( std::all_of( c, c + 4, is_alnum ) )
		{
			std::string t = draw_classes( std::string( c, 4 ), s );
			return { uint32_t( uint8_t( t[ 0 ] ) ) | uint32_t( uint8_t( t[ 1 ] ) ) << 8 | uint32_t( uint8_t( t[ 2 ] ) ) << 16 | uint32_t( uint8_t( t[ 3 ] ) ) << 24, real.label };
		}
		size_t dc = std::to_string( v ).size();
		uint64_t lo = 1;
		for ( size_t i = 1; i < dc; i++ ) lo *= 10;
		uint64_t hi = std::min<uint64_t>( lo * 10 - 1, 0xFFFFFFFFull );
		for ( ;; )
		{
			uint32_t r = uint32_t( lo + s.next_u64() % ( hi - lo + 1 ) );
			if ( r != v ) return { r, real.label };
		}
	}
	// rule 6: byte 127 = the value that makes the 128-byte base block sum to 0 (mod 256)
	EdidBlock ProfileGenerator::gen_edid_checksum( const EdidBlock& block )
	{
		if ( block.bytes.size() < 128 ) fail( "EDID base block shorter than 128 bytes" );
		EdidBlock out = block;
		uint8_t sum = 0;
		for ( size_t i = 0; i < 127; i++ ) sum = uint8_t( sum + out.bytes[ i ] );
		out.bytes[ 127 ] = uint8_t( 0x100 - sum );
		return out;
	}
	// rule 6 on the EDID values (edid_<n>.bin): serial field + FFh serial string synthetic, ONE mapping per physical monitor
	// (key: manufacturer + product code + the real serial: the same monitor on several ports / instances gets one synthetic
	// serial), base-block checksum recomputed, extension blocks untouched (scanned for the real serials)
	EdidGen ProfileGenerator::gen_edid( const EdidInput& in )
	{
		static const uint8_t k_hdr[ 8 ] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
		EdidGen g;
		std::map<std::string, uint32_t> num;
		std::map<std::string, std::string> str;
		int nnum = 0, nstr = 0, row = 0;
		for ( const EdidBlock& blk : in.blocks )
		{
			const Bytes& raw = blk.bytes;
			if ( raw.size() < 128 || memcmp( raw.data(), k_hdr, 8 ) != 0 ) fail( blk.name + ": not an EDID base block" );
			EdidBlock b = blk;
			const std::string area = "edid\\" + blk.name, model = hex_of( &raw[ 8 ], 4, true );
			std::vector<Bytes> real_forms;
			uint32_t serial = uint32_t( raw[ 12 ] ) | uint32_t( raw[ 13 ] ) << 8 | uint32_t( raw[ 14 ] ) << 16 | uint32_t( raw[ 15 ] ) << 24;
			if ( serial )
			{
				auto it = num.find( model + fmt( "%08X", serial ) );
				if ( it == num.end() )
				{
					it = num.emplace( model + fmt( "%08X", serial ), gen_edid_serial( { serial, fmt( "edid/n%02d", ++nnum ) } ).value ).first;
					g.presets.push_back( { "edid_serial", "6", fmt( "0x%08X", serial ), fmt( "0x%08X", it->second ), { lower( fmt( "0x%08X", serial ) ), std::to_string( serial ) }, "edid.txt" } );
				}
				real_forms.push_back( Bytes( raw.begin() + 12, raw.begin() + 16 ) );
				if ( c_.rules.r6 )
				{
					for ( int i = 0; i < 4; i++ ) b.bytes[ 12 + i ] = uint8_t( it->second >> ( 8 * i ) );
					g.fields.push_back( { area, fmt( "D%02d", ++row ), "6", "EDID serial number field (bytes 12-15, little-endian)", 12, 0, 4 } );
				}
			}
			for ( size_t d : { size_t( 54 ), size_t( 72 ), size_t( 90 ), size_t( 108 ) } )
			{
				if ( raw[ d ] || raw[ d + 1 ] || raw[ d + 2 ] || raw[ d + 3 ] != 0xFF ) continue;	// tag FFh: monitor serial string
				size_t len = 0;
				while ( len < 13 && raw[ d + 5 + len ] != 0x0A ) len++;
				std::string s( raw.begin() + d + 5, raw.begin() + d + 5 + len );
				if ( check_filler( s ) || std::none_of( s.begin(), s.end(), is_alnum ) ) continue;
				auto it = str.find( model + s );
				if ( it == str.end() )
				{
					it = str.emplace( model + s, gen_class_string( { s, fmt( "edid/s%02d", ++nstr ) } ).text ).first;
					g.presets.push_back( { "edid_serial_string", "6", s, it->second, { lower( s ) }, "edid.txt" } );
				}
				real_forms.push_back( to_bytes( s ) );
				g.needles.push_back( s );
				if ( c_.rules.r6 )
				{
					memcpy( &b.bytes[ d + 5 ], it->second.data(), len );
					g.fields.push_back( { area, fmt( "D%02d", ++row ), "6", fmt( "EDID descriptor at offset %zu, tag FFh: monitor serial string", d ), d + 5, 0, uint32_t( len ) } );
				}
			}
			b = gen_edid_checksum( b );
			g.fields.push_back( { area, fmt( "D%02d", ++row ), "6c", "EDID base-block checksum (byte 127), recomputed", 127, 0, 1 } );
			for ( size_t e = 128; e + 128 <= raw.size(); e += 128 )	// extension blocks: never changed, scanned for the real serials
			{
				g.ext_blocks++;
				for ( const Bytes& f : real_forms )
					if ( std::search( raw.begin() + e, raw.begin() + e + 128, f.begin(), f.end() ) != raw.begin() + e + 128 ) g.ext_hits++;
			}
			g.synthetic.push_back( b );
		}
		return g;
	}

	// ── gen_text_identity: rules 3, 4a-4d, 7, 8 on the fwid text outputs ─────────────────────────────────────────────────
	namespace
	{
		struct Ident
		{
			std::string id, kind, rule, source, original, synthetic;
			std::vector<std::string> needles;	// lower-case search forms
			bool token = false;					// matched as "&token&" (rule 4b instance hashes; none since the 2026-10-16 decision)
			std::string desc;					// MAC: the adapter's Description
			int parent = -1;					// EUI-64: index of its MAC
			bool eui_ffff = false;
			std::string only_file;				// presets: search only this file
			bool preset = false;
			size_t family = size_t( -1 );		// MAC: index in the MacFamily
		};
		bool rule_on( const RuleSet& r, const std::string& rule )
		{
			if ( rule == "3" ) return r.r3;
			if ( rule == "4a" ) return r.r4a;
			if ( rule == "4b" ) return r.r4b;
			if ( rule == "4d" ) return r.r4d;
			if ( rule == "6" ) return r.r6;
			if ( !rule.empty() && rule[ 0 ] == '7' ) return r.r7;
			if ( !rule.empty() && rule[ 0 ] == '8' ) return r.r8;
			return r.r4c;
		}
	}

	std::vector<std::string> ProfileGenerator::parse_usb_serial_table( const std::vector<Bytes>& enum_registry_texts )
	{
		static const std::regex r_usb( R"(^## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\USB\\([^\\]+)\\([^\\&]+)$)" );
		std::vector<std::string> t;
		std::set<std::string> seen;
		for ( const Bytes& b : enum_registry_texts )
			for ( const std::string& l : split_lines( b ) )
			{
				std::smatch m;
				if ( l.rfind( "## ", 0 ) != 0 || !std::regex_match( l, m, r_usb ) || check_usb_placeholder( m[ 2 ].str() ) ) continue;
				if ( seen.insert( lower( m[ 2 ].str() ) ).second ) t.push_back( lower( m[ 2 ].str() ) );
			}
		return t;
	}

	TextIdentityGen ProfileGenerator::gen_text_identity( const TextIdentityInput& in )
	{
		const std::vector<TextFile>& files = in.files;
		const RuleSet& rules = c_.rules;
		TextIdentityGen out;
		std::vector<Ident> ids;
		std::map<std::string, size_t> by_key;	// lower-case value (GUIDs without braces) -> index
		std::map<std::string, std::string> mac_adapter;	// lower-case MAC -> description of its first adapter
		std::map<std::string, int> counters;
		std::set<std::string> keep_style;		// GUID keys whose own style (braces / case) identity.tsv keeps
		auto key_of = []( const std::string& v ) { std::string k = lower( v ); if ( !k.empty() && k.front() == '{' && k.size() == 38 ) k = k.substr( 1, 36 ); return k; };
		auto add = [ & ]( const std::string& kind, const std::string& rule, const std::string& value, const std::string& file, size_t line ) -> Ident& {
			std::string key = key_of( value );
			auto it = by_key.find( key );
			if ( it != by_key.end() ) return ids[ it->second ];
			Ident d;
			d.kind = kind;
			d.rule = rule;
			d.original = value;
			d.source = fmt( "%s:%zu", file.c_str(), line );
			const char* pre = kind == "mac_eui64" ? "E" : rule == "4a" ? "N" : rule == "3" ? "M" : rule == "4b" ? "S" : rule == "4d" ? "U" : rule == "6" ? "X" :
				rule[ 0 ] == '7' ? "O" : rule[ 0 ] == '8' ? "K" : "G";
			d.id = fmt( "%s%02d", pre, ++counters[ pre ] );
			by_key[ key ] = ids.size();
			ids.push_back( d );
			return ids.back();
		};
		auto find_file = [ & ]( const char* n ) -> const TextFile* {
			for ( const TextFile& f : files ) if ( f.name == n ) return &f;
			return nullptr;
		};
		const TextFile* f_info = find_file( "capture_info.txt" );
		const TextFile* f_net = find_file( "network_adapters.txt" );
		const TextFile* f_enum = find_file( "enum_registry.txt" );
		const TextFile* f_pci = find_file( "pci_devices.txt" );
		if ( !f_info || !f_net || !f_enum || !f_pci ) fail( "the capture lacks capture_info.txt / network_adapters.txt / enum_registry.txt / pci_devices.txt" );
		// the certificate stores are not part of the platform profile (user decision 2026-10-16): never searched here
		auto searched = []( const TextFile& f ) { return f.name != "certificates.txt"; };

		static const std::string k_null_container = "00000000-0000-0000-ffff-ffffffffffff";
		static const std::regex r_name( R"re(^os\.computer_name\s*=\s*NetBIOS "([^"]*)", DNS host "([^"]*)")re" );
		static const std::regex r_adapter( R"re(^AdapterName: "(\{[0-9A-Fa-f-]{36}\})")re" );
		static const std::regex r_mac( R"(^MAC \(6 bytes\): ([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}))" );
		static const std::regex r_scsi( R"(^## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SCSI\\([^\\]+)\\([^\\]+)$)" );
		static const std::regex r_scsi_inst( R"(^([0-9]+)&([0-9A-Fa-f]+)&([0-9]+)&([0-9A-Fa-f]+)$)" );
		static const std::regex r_storage( R"(^## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\STORAGE\\([^\\]+)\\([^\\]+)$)" );
		static const std::regex r_usb( R"(^## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\USB\\([^\\]+)\\([^\\&]+)$)" );
		static const std::regex r_swd( R"(^## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\([^\\]+)\\([^\\]+)$)" );
		static const std::regex r_bare_guid( R"(([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}))" );
		static const std::regex r_volume( R"(^(\{[0-9A-Fa-f-]{36}\})(#[0-9A-Fa-f]+)$)" );
		static const std::regex r_snapshot( R"(^HarddiskVolumeSnapshot[0-9]+$)" );
		static const std::regex r_container_enum( R"re(^\s*value "ContainerID"\s+REG_SZ.*: "(\{[0-9A-Fa-f-]{36}\})"$)re" );
		static const std::regex r_container_pci( R"((?:SPDRP_BASE_CONTAINERID\s+REG_SZ "|DEVPKEY_Device_(?:Base)?ContainerId .*type 0xD: )(\{[0-9A-Fa-f-]{36}\}))" );
		static const std::regex r_any_guid( R"(\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?)" );
		static const std::regex r_macish( R"(^([0-9A-Fa-f]{2})[:-]?([0-9A-Fa-f]{2})[:-]?([0-9A-Fa-f]{2})[:-]?([0-9A-Fa-f]{2})[:-]?([0-9A-Fa-f]{2})[:-]?([0-9A-Fa-f]{2})$)" );

		// rule 4a: computer name (capture_info.txt line "os.computer_name = NetBIOS "..", DNS host ".."")
		for ( const auto& [ i, l ] : [ & ] { std::vector<std::pair<size_t, std::string>> v; auto ls = split_lines( f_info->data ); for ( size_t k = 0; k < ls.size(); k++ ) v.emplace_back( k, ls[ k ] ); return v; }() )
		{
			std::smatch m;
			if ( l.rfind( "os.computer_name", 0 ) != 0 || !std::regex_search( l, m, r_name ) ) continue;
			add( "computer_name", "4a", m[ 1 ].str(), f_info->name, i + 1 );
			if ( !ieq( m[ 1 ].str(), m[ 2 ].str() ) ) add( "computer_name", "4a", m[ 2 ].str(), f_info->name, i + 1 );
		}
		if ( counters[ "N" ] == 0 ) fail( "capture_info.txt has no os.computer_name line" );
		// rule 3 + 4c: network_adapters.txt MACs (VMnet pattern excluded) and interface GUIDs, in file order
		{
			auto lines = split_lines( f_net->data );
			std::string desc;
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				std::smatch m;
				if ( lines[ i ].rfind( "=== adapter", 0 ) == 0 ) desc.clear();
				if ( lines[ i ].rfind( "Description: \"", 0 ) == 0 && lines[ i ].size() > 15 ) desc = lines[ i ].substr( 14, lines[ i ].size() - 15 );
				if ( std::regex_search( lines[ i ], m, r_adapter ) ) add( "interface_guid", "4c", m[ 1 ].str(), f_net->name, i + 1 );
				else if ( std::regex_search( lines[ i ], m, r_mac ) && !is_vmnet_mac( m[ 1 ].str() ) && mac_adapter.emplace( lower( m[ 1 ].str() ), desc ).second )
					add( "mac", "3", upper( m[ 1 ].str() ), f_net->name, i + 1 ).desc = desc;
			}
		}
		// rule 4b / 4c / 4d: enum_registry.txt storage instance IDs, volume GUIDs, SWD instance GUIDs, container IDs, USB serials
		// (file order)
		std::vector<std::pair<std::string, std::string>> volume_paths;	// (device ID, instance ID)
		{
			auto lines = split_lines( f_enum->data );
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				std::smatch m;
				if ( lines[ i ].rfind( "## ", 0 ) != 0 && lines[ i ].find( "\"ContainerID\"" ) == std::string::npos ) continue;
				if ( std::regex_match( lines[ i ], m, r_scsi ) )
				{
					std::string dev = m[ 1 ].str(), inst = m[ 2 ].str();
					// rule 4b (user decision 2026-10-16): the "N&hash&N&address" form is a PnP parent-prefix hash (derived from the
					// device topology, not unique): EXACT. Any other Enum\SCSI instance form (e.g. a drive serial) stops the run.
					if ( !std::regex_match( inst, r_scsi_inst ) )
						fail( fmt( "enum_registry.txt:%zu: an Enum\\SCSI instance ID of an unknown format - it needs a field decision", i + 1 ) );
					(void)dev;
				}
				else if ( std::regex_match( lines[ i ], m, r_storage ) )
				{
					std::string dev = m[ 1 ].str(), inst = m[ 2 ].str();
					std::smatch mv;
					if ( dev == "Volume" && std::regex_match( inst, mv, r_volume ) )
					{
						add( "volume_guid", "4c", mv[ 1 ].str(), f_enum->name, i + 1 );
						volume_paths.emplace_back( dev, inst );
					}
					else if ( !( dev == "VolumeSnapshot" && std::regex_match( inst, r_snapshot ) ) )
						fail( fmt( "enum_registry.txt:%zu: an Enum\\STORAGE instance of an unknown format - it needs a field decision", i + 1 ) );
				}
				else if ( std::regex_match( lines[ i ], m, r_swd ) )
				{
					// rule 4c (user decision 2026-10-16): every GUID inside a software-device instance ID (MMDEVAPI endpoints,
					// PRINTENUM, RADIO, DRIVERENUM, WPDBUSENUM, ...), braced or not; the all-zero / all-F GUIDs are no identifiers
					const std::string inst = m[ 2 ].str();
					for ( auto it = std::sregex_iterator( inst.begin(), inst.end(), r_bare_guid ); it != std::sregex_iterator(); ++it )
					{
						std::string g = ( *it )[ 1 ].str(), hex;
						for ( char c : g ) if ( c != '-' ) hex += char( std::tolower( uint8_t( c ) ) );
						if ( hex.find_first_not_of( '0' ) == std::string::npos || hex.find_first_not_of( 'f' ) == std::string::npos ) continue;
						add( "swd_guid", "4c", g, f_enum->name, i + 1 );
					}
				}
				else if ( std::regex_match( lines[ i ], m, r_usb ) )
				{
					if ( !check_usb_placeholder( m[ 2 ].str() ) ) add( "usb_serial", "4d", m[ 2 ].str(), f_enum->name, i + 1 );
				}
				else if ( std::regex_match( lines[ i ], m, r_container_enum ) && key_of( m[ 1 ].str() ) != k_null_container )
					add( "container_id", "4c", m[ 1 ].str(), f_enum->name, i + 1 );
			}
		}
		// rule 4c: pci_devices.txt container IDs
		{
			auto lines = split_lines( f_pci->data );
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				std::smatch m;
				if ( lines[ i ].find( "CONTAINERID" ) == std::string::npos && lines[ i ].find( "ContainerId" ) == std::string::npos ) continue;
				if ( std::regex_search( lines[ i ], m, r_container_pci ) && key_of( m[ 1 ].str() ) != k_null_container )
					add( "container_id", "4c", m[ 1 ].str(), f_pci->name, i + 1 );
			}
		}

		// ── optional OS identifier files: fwid 1.3 os_ids.txt; fwid 1.4 license / update_ids / disks / hid / gpu / bluetooth /
		// audio / chid / platform_security / environment / wifi / printers (absent: the section is omitted, gen_identity logs
		// it). Every line fwid marks "[IDENTIFIER" must be consumed by a rule below; an unconsumed one stops the run (it needs
		// a field decision). The fwid 1.4 formats are not released yet: values are taken as the last quoted string of the
		// marked line, else the text after its last ": ".
		auto unesc = []( std::string v ) {
			for ( size_t q; ( q = lower( v ).find( "\\x5c" ) ) != std::string::npos; ) v.replace( q, 4, "\\" );
			for ( size_t q; ( q = lower( v ).find( "\\x22" ) ) != std::string::npos; ) v.replace( q, 4, "\"" );
			return v;
		};
		auto field_value = [ & ]( const std::string& l ) -> std::string {
			size_t b = l.rfind( '"' );
			if ( b != std::string::npos && b > 0 )
			{
				size_t a = l.rfind( '"', b - 1 );
				if ( a != std::string::npos ) return unesc( l.substr( a + 1, b - a - 1 ) );
			}
			size_t p2 = l.rfind( ": " );
			std::string v = p2 == std::string::npos ? std::string() : l.substr( p2 + 2 );
			for ( const char* cut : { " (", " [" } ) if ( size_t c = v.find( cut ); c != std::string::npos ) v = v.substr( 0, c );
			while ( !v.empty() && ( v.back() == ' ' || v.back() == '\t' ) ) v.pop_back();
			return unesc( v );
		};
		auto undecided = []( const std::string& f, size_t line ) { fail( fmt( "%s:%zu: an identifier line no rule covers - it needs a field decision", f.c_str(), line ) ); };
		auto add_guid = [ & ]( const char* kind, const std::string& g, const std::string& file, size_t line, bool own_style ) {
			add( kind, "4c", g, file, line );
			if ( own_style ) keep_style.insert( key_of( g ) );
		};
		std::vector<WinInstallationId> win_ids;	// rule 8a, with the license data of the same file
		if ( const TextFile* f = find_file( "os_ids.txt" ) )
		{
			static const std::regex r_reg( R"re(^HKLM\\.* "([A-Za-z]+)" (REG_\w+), [0-9]+ bytes \[[^\]]*\]: (.*)$)re" );
			static const std::regex r_hex_dec( R"(0x([0-9A-Fa-f]+) \(([0-9]+)\))" );
			static const std::regex r_iso( R"(=\s*([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z))" );
			static const std::regex r_usid( R"(^user SID: (S-1-5-21-[0-9]+-[0-9]+-[0-9]+-[0-9]+))" );
			static const std::regex r_msid( R"(^account domain prefix: (S-1-5-21-[0-9]+-[0-9]+-[0-9]+))" );
			static const std::regex r_vser( R"(volume serial number: ([0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}))" );
			static const std::regex r_vguid( R"(volume GUID path: .*Volume(\{[0-9A-Fa-f-]{36}\}))" );
			auto lines = split_lines( f->data );
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				const std::string& l = lines[ i ];
				bool marked = l.find( "[IDENTIFIER" ) != std::string::npos, used = false;
				std::smatch m;
				if ( std::regex_match( l, m, r_reg ) )
				{
					std::string name = m[ 1 ].str(), v = field_value( l ), rest = m[ 3 ].str();
					used = true;
					if ( name == "MachineGuid" ) add_guid( "machine_guid", v, f->name, i + 1, true );
					else if ( name == "HwProfileGuid" ) add_guid( "hw_profile_guid", v, f->name, i + 1, true );
					else if ( name == "MachineId" ) add_guid( "sqm_machine_id", v, f->name, i + 1, true );
					else if ( name == "ProductId" ) add( "product_id", "7a", v, f->name, i + 1 );
					else if ( name == "InstallDate" || name == "InstallTime" )
					{
						std::smatch h;
						if ( !std::regex_search( rest, h, r_hex_dec ) ) fail( fmt( "os_ids.txt:%zu: unreadable %s", i + 1, name.c_str() ) );
						Ident& d = add( name == "InstallDate" ? "install_date" : "install_time", "7b", h[ 2 ].str(), f->name, i + 1 );
						d.needles = { lower( "0x" + h[ 1 ].str() ), h[ 2 ].str() };
						std::smatch iso;
						if ( name == "InstallDate" && i + 1 < lines.size() && std::regex_search( lines[ i + 1 ], iso, r_iso ) ) d.needles.push_back( lower( iso[ 1 ].str() ) );
					}
					else if ( name == "RegisteredOwner" ) { if ( !v.empty() ) add( "user_name", "7c", v, f->name, i + 1 ); }
					else if ( name == "RegisteredOrganization" ) { if ( !v.empty() ) undecided( f->name, i + 1 ); }	// no decision yet
					else if ( name == "BuildGUID" ) {}	// a build identifier, exact
					else used = false;
				}
				else if ( l.rfind( "GetUserNameW: ", 0 ) == 0 ) { add( "user_name", "7c", field_value( l ), f->name, i + 1 ); used = true; }
				else if ( l.rfind( "GetUserNameExW(NameSamCompatible): ", 0 ) == 0 )
				{
					std::string v = field_value( l );
					Ident& d = add( "name_sam_compatible", "7c", v, f->name, i + 1 );
					size_t bs = v.find( '\\' );
					d.needles = { lower( v ) };
					if ( bs != std::string::npos ) d.needles.push_back( lower( v.substr( 0, bs ) + "\\x5c" + v.substr( bs + 1 ) ) );
					used = true;
				}
				else if ( std::regex_search( l, m, r_usid ) ) { add( "user_sid", "7d", m[ 1 ].str(), f->name, i + 1 ); used = true; }
				else if ( std::regex_search( l, m, r_msid ) ) { add( "machine_sid", "7d", m[ 1 ].str(), f->name, i + 1 ); used = true; }
				else if ( std::regex_search( l, m, r_vser ) )
				{
					Ident& d = add( "volume_serial", "7e", m[ 1 ].str(), f->name, i + 1 );
					std::string plain = m[ 1 ].str();
					plain.erase( 4, 1 );
					d.needles = { lower( m[ 1 ].str() ), lower( "0x" + plain ) };
					used = true;
				}
				else if ( std::regex_search( l, m, r_vguid ) ) { add_guid( "volume_guid", m[ 1 ].str(), f->name, i + 1, false ); used = true; }
				if ( marked && !used ) undecided( f->name, i + 1 );
			}
		}
		for ( const char* fn : { "license.txt", "update_ids.txt", "disks.txt", "hid.txt", "gpu.txt", "bluetooth.txt", "audio.txt", "chid.txt", "platform_security.txt",
				  "environment.txt", "wifi.txt", "printers.txt" } )
		{
			const TextFile* f = find_file( fn );
			if ( !f ) continue;
			const std::string file = fn;
			auto lines = split_lines( f->data );
			WinLicenseInfo lic;	// license.txt: exact license data, carried with the installation ID
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				const std::string& l = lines[ i ];
				const std::string k = lower( l );
				if ( file == "license.txt" && l.find( "[IDENTIFIER" ) == std::string::npos )
				{
					if ( k.find( "licensestatus" ) != std::string::npos ) lic.license_status = field_value( l );
					else if ( k.find( "productkeychannel" ) != std::string::npos ) lic.product_key_channel = field_value( l );
					else if ( k.find( "description" ) != std::string::npos ) lic.description = field_value( l );
					else if ( k.find( "name" ) != std::string::npos ) lic.name = field_value( l );
				}
				if ( l.find( "[IDENTIFIER" ) == std::string::npos ) continue;
				const std::string v = field_value( l );
				std::smatch m;
				const bool guid = std::regex_search( v, m, r_any_guid );
				auto has = [ & ]( std::initializer_list<const char*> w ) { for ( const char* x : w ) if ( k.find( x ) != std::string::npos ) return true; return false; };
				if ( file == "chid.txt" ) continue;	// CHIDs are derived from SMBIOS fields that stay exact (check_chid_fields)
				else if ( file == "license.txt" && has( { "installationid" } ) )
				{
					add( "win_installation_id", "8a", v, file, i + 1 );
					WinInstallationId w;
					w.text = v;
					w.source = fmt( "license.txt:%zu", i + 1 );
					w.license = lic;
					win_ids.push_back( w );
				}
				else if ( file == "update_ids.txt" && has( { "susclientidvalidation" } ) ) add( "sus_client_id_validation", "8c", v, file, i + 1 );
				else if ( file == "update_ids.txt" && has( { "susclientid" } ) && guid ) add_guid( "sus_client_id", m[ 0 ].str(), file, i + 1, true );
				else if ( file == "update_ids.txt" && has( { "diagtrack", "telemetry", "deviceid", "machineid", "sqm" } ) )
				{
					if ( guid ) add_guid( "telemetry_id", m[ 0 ].str(), file, i + 1, true );
					else add( "telemetry_id", "8b", v, file, i + 1 );
				}
				else if ( file == "disks.txt" && has( { "serial" } ) ) add( "disk_serial", "8d", v, file, i + 1 );
				else if ( file == "hid.txt" && has( { "serial" } ) ) add( "hid_serial", "4d", v, file, i + 1 );	// = a USB serial: the same identity
				else if ( ( file == "hid.txt" || file == "bluetooth.txt" || file == "audio.txt" ) && has( { "manufacturer", "product", "name" } ) && !guid ) {}	// names exact
				else if ( file == "gpu.txt" && has( { "model", "driver", "bios", "memory", "chiptype", "dactype", "adapterstring" } ) && !guid ) {}	// exact
				else if ( file == "gpu.txt" ) { if ( guid ) add_guid( "gpu_guid", m[ 0 ].str(), file, i + 1, true ); else add( "gpu_unique", "8e", v, file, i + 1 ); }
				else if ( file == "bluetooth.txt" && std::regex_match( v, m, r_macish ) )
				{
					std::string mac = upper( m[ 1 ].str() + ":" + m[ 2 ].str() + ":" + m[ 3 ].str() + ":" + m[ 4 ].str() + ":" + m[ 5 ].str() + ":" + m[ 6 ].str() );
					if ( !by_key.count( key_of( mac ) ) ) add( "bt_device", "3", mac, file, i + 1 );	// the local radio = its adapter MAC identity
				}
				else if ( file == "audio.txt" && guid )
				{
					for ( auto it = std::sregex_iterator( l.begin(), l.end(), r_any_guid ); it != std::sregex_iterator(); ++it ) add_guid( "audio_endpoint", ( *it )[ 0 ].str(), file, i + 1, false );
				}
				else undecided( file, i + 1 );
			}
		}
		// fixed synthetic values from another gen_ (rule 6: the EDID serials of gen_edid)
		for ( const IdentityPreset& pr : in.presets )
		{
			Ident& d = add( pr.kind, pr.rule, pr.original, "-", 0 );
			d.synthetic = pr.synthetic;
			d.needles = pr.needles;
			d.only_file = pr.only_file;
			d.preset = true;
		}

		// the adapter MACs (kind "mac") and their lower-case plain hex
		std::vector<size_t> mac_ids;
		std::map<std::string, size_t> mac_by_plain;
		for ( size_t i = 0; i < ids.size(); i++ )
			if ( ids[ i ].kind == "mac" ) { mac_ids.push_back( i ); mac_by_plain[ mac_plain( ids[ i ].original ) ] = i; }
		// lower-case copies of the searched text files, with line starts
		struct TextLc { const TextFile* f; std::string lc; std::vector<size_t> starts; };
		std::vector<TextLc> texts;
		for ( const TextFile& f : files )
		{
			if ( !searched( f ) ) continue;
			TextLc t { &f, std::string( f.data.begin(), f.data.end() ), { 0 } };
			for ( char& c : t.lc ) c = char( std::tolower( uint8_t( c ) ) );
			for ( size_t i = 0; i < t.lc.size(); i++ )
				if ( t.lc[ i ] == '\n' ) t.starts.push_back( i + 1 );
			texts.push_back( std::move( t ) );
		}
		auto line_of = []( const TextLc& t, size_t pos ) { return size_t( std::upper_bound( t.starts.begin(), t.starts.end(), pos ) - t.starts.begin() ); };
		// rule 3 (derived): the EUI-64 forms of an adapter MAC in instance IDs
		for ( size_t mi : std::vector<size_t>( mac_ids ) )
		{
			std::string plain = mac_plain( ids[ mi ].original );
			for ( bool ffff : { true, false } )
			{
				std::string form = plain.substr( 0, 6 ) + ( ffff ? "ffff" : "fffe" ) + plain.substr( 6 ) + ( ffff ? "00" : "" );
				for ( const TextLc& t : texts )
				{
					size_t pos = t.lc.find( form );
					while ( pos != std::string::npos && ( ( pos && is_alnum( t.lc[ pos - 1 ] ) ) || ( pos + form.size() < t.lc.size() && is_alnum( t.lc[ pos + form.size() ] ) ) ) )
						pos = t.lc.find( form, pos + 1 );
					if ( pos == std::string::npos ) continue;
					Ident& e = add( "mac_eui64", "3", std::string( t.f->data.begin() + pos, t.f->data.begin() + pos + form.size() ), t.f->name, line_of( t, pos ) );
					e.parent = int( mi );
					e.eui_ffff = ffff;
					break;
				}
			}
		}
		// rule 4c: version-1 GUIDs whose node is an adapter MAC, wherever they occur (file order)
		for ( const TextLc& t : texts )
		{
			const std::string& s = t.lc;
			for ( size_t i = 0; i + 36 <= s.size(); i++ )
			{
				if ( s[ i + 8 ] != '-' || s[ i + 13 ] != '-' || s[ i + 14 ] != '1' || s[ i + 18 ] != '-' || s[ i + 23 ] != '-' ) continue;
				bool ok = ( !i || !is_alnum( s[ i - 1 ] ) ) && ( i + 36 == s.size() || !is_alnum( s[ i + 36 ] ) );
				for ( size_t k = 0; ok && k < 36; k++ ) ok = ( k == 8 || k == 13 || k == 18 || k == 23 ) || is_hex( s[ i + k ] );
				if ( !ok || !mac_by_plain.count( s.substr( i + 24, 12 ) ) ) continue;
				add( "mac_node_guid", "4c", "{" + std::string( t.f->data.begin() + i, t.f->data.begin() + i + 36 ) + "}", t.f->name, line_of( t, i ) );
			}
		}

		// ── synthetic values ──────────────────────────────────────────────────────────────────────────────────────────
		// 1. the adapter MACs: gen_mac_family (one synthetic MAC per physical NIC / radio, dependents re-derived)
		MacFamily fam;
		for ( size_t i : mac_ids )
		{
			ids[ i ].family = fam.members.size();
			fam.members.push_back( { { ids[ i ].original, ids[ i ].desc, ids[ i ].id } } );
		}
		const MacFamily sfam = gen_mac_family( fam );
		for ( size_t i : mac_ids )
		{
			Ident& d = ids[ i ];
			const MacMember& sm = sfam.members[ d.family ];
			d.synthetic = sm.mac.text;
			std::string l = lower( d.original ), h = l;
			std::replace( h.begin(), h.end(), ':', '-' );
			d.needles = { l, h, mac_plain( d.original ) };
			uint8_t m[ 6 ];
			parse_mac_text( d.original, m );
			out.binary_needles.push_back( Bytes( m, m + 6 ) );
			out.macs.push_back( { { d.original, d.desc, d.id }, { d.synthetic, d.desc, d.id } } );
			if ( sm.parent >= 0 )
			{
				const Ident& pd = ids[ mac_ids[ size_t( sm.parent ) ] ];
				out.fields.push_back( { f_net->name, d.id, "3r", sm.locally_administered
					? fmt( "relation: adapter \"%s\" = the locally administered variant (byte 0 bit 1) of adapter \"%s\"", d.desc.c_str(), pd.desc.c_str() )
					: fmt( "relation: adapter \"%s\" = adapter \"%s\" + %lld", d.desc.c_str(), pd.desc.c_str(), sm.delta ), 0, 0, 6 } );
			}
		}
		NodeMap nodes;
		for ( size_t i : mac_ids ) nodes.real_to_synthetic[ mac_plain( ids[ i ].original ) ] = mac_plain( ids[ i ].synthetic );
		// 2. everything else (dependent kinds - SIDs, NameSamCompatible, the install instant - in the pass after it)
		const std::vector<std::string> usb_table = in.usb_table.empty() ? parse_usb_serial_table( { f_enum->data } ) : in.usb_table;
		static const std::set<std::string> k_post = { "machine_sid", "user_sid", "name_sam_compatible", "install_date", "install_time" };
		for ( Ident& d : ids )
		{
			if ( d.kind == "mac" || d.preset || k_post.count( d.kind ) ) continue;
			const std::string label = "identity/" + d.id;
			if ( d.rule == "4a" ) d.synthetic = gen_computer_name( { d.original, label } ).text;
			else if ( d.kind == "mac_eui64" )
			{
				const Ident& nic = ids[ size_t( d.parent ) ];
				d.synthetic = gen_eui64_instance_id( { d.original, d.eui_ffff }, { nic.synthetic, nic.desc, nic.id } ).text;
				out.fields.push_back( { "network_adapters.txt", d.id, "3r", fmt( "relation: the EUI-64 form (OUI + %s + NIC part%s) of the MAC of adapter \"%s\" in instance IDs",
					d.eui_ffff ? "FFFF" : "FFFE", d.eui_ffff ? " + 00" : "", nic.desc.c_str() ), 0, 0, uint32_t( d.original.size() ) } );
			}
			else if ( d.rule == "3" )	// another device's MAC (a paired Bluetooth device): OUI kept
			{
				d.synthetic = gen_mac( { d.original, "", d.id } ).text;
				std::string l = lower( d.original ), h = l;
				std::replace( h.begin(), h.end(), ':', '-' );
				d.needles = { l, h, mac_plain( d.original ) };
			}
			else if ( d.rule == "4b" )
			{
				if ( !std::all_of( d.original.begin(), d.original.end(), is_hex ) ) fail( "rule 4b: the serial part is not a hex token" );
				d.synthetic = gen_hex_token( { d.original, label } ).text;
			}
			else if ( d.rule == "4d" )
			{
				// ONE mapping per physical device for both profiles: the shared table's index keys the stream (never the value). A
				// HID serial equal to a USB serial is that identity already; one outside the table gets a stream of this environment
				auto it = std::find( usb_table.begin(), usb_table.end(), lower( d.original ) );
				if ( it == usb_table.end() && d.kind == "usb_serial" ) fail( "rule 4d: a USB serial of the capture is missing from the shared serial table" );
				d.synthetic = gen_usb_serial( { d.original, it == usb_table.end() ? -1 : long( it - usb_table.begin() ), label } ).text;
			}
			else if ( d.rule == "7a" ) d.synthetic = gen_product_id( { d.original, label } ).text;
			else if ( d.rule == "7c" || d.rule == "8b" || d.rule == "8d" || d.rule == "8e" ) d.synthetic = gen_class_string( { d.original, label } ).text;
			else if ( d.rule == "7e" ) d.synthetic = gen_volume_serial( { d.original, label } ).serial;
			else if ( d.rule == "8a" )
			{
				for ( WinInstallationId& w : win_ids )
					if ( w.text == d.original )
					{
						w.label = label;
						WinInstallationId s = gen_win_installation_id( w );
						d.synthetic = s.text;
						out.installation_ids.push_back( w );
						out.installation_ids.push_back( s );
						break;
					}
			}
			else if ( d.rule == "8c" ) d.synthetic = gen_hex_token( { d.original, label } ).text;	// the identifier hash inside it is not modelled
			else
			{
				// GUIDs: canonical {UPPER} form in identity.tsv, or the original's own style (MachineGuid & co.); occurrences are
				// matched without braces, any case
				std::string g = d.original;
				if ( !keep_style.count( key_of( g ) ) )
				{
					if ( g.front() != '{' ) g = "{" + g + "}";
					g = upper( g );
				}
				d.original = g;
				d.synthetic = gen_guid( { g, label }, nodes ).text;
				const std::string bare = g.front() == '{' ? g.substr( 1, 36 ) : g;
				d.needles = { lower( bare ) };
				std::string node = lower( bare.substr( 24, 12 ) );
				if ( bare[ 14 ] == '1' && nodes.real_to_synthetic.count( node ) )
				{
					std::string desc;
					for ( size_t i : mac_ids ) if ( mac_plain( ids[ i ].original ) == node ) desc = ids[ i ].desc;
					out.fields.push_back( { "network_adapters.txt", d.id, "4cr", fmt( "relation: version-1 GUID (%s), node = the synthetic MAC of adapter \"%s\"", d.kind.c_str(), desc.c_str() ), 0, 0, 36 } );
				}
			}
			if ( d.needles.empty() ) d.needles = { lower( d.original ) };
		}
		// 3. dependent kinds: SIDs (7d) - one synthetic prefix per real prefix, the user SID = that prefix + its exact RID;
		// NameSamCompatible (7c) - DOMAIN = computer name -> the synthetic computer name, the user = the synthetic user name;
		// the install instant (7b) - one instant for InstallDate and InstallTime
		{
			std::map<std::string, std::string> sids;
			int nsid = 0;
			auto sid_of = [ & ]( const std::string& prefix ) -> std::string {
				std::string key = lower( prefix );
				if ( !sids.count( key ) ) sids[ key ] = gen_sid( { prefix, fmt( "identity/sid%02d", ++nsid ) } ).prefix;
				return sids[ key ];
			};
			auto syn_value = [ & ]( const std::string& v, const char* kind ) -> std::string {
				for ( const Ident& x : ids ) if ( x.kind == kind && ieq( x.original, v ) ) return x.synthetic;
				return {};
			};
			bool have_instant = false;
			InstallTime inst;
			for ( Ident& d : ids )
			{
				if ( !k_post.count( d.kind ) ) continue;
				if ( d.kind == "machine_sid" ) d.synthetic = sid_of( d.original );
				else if ( d.kind == "user_sid" ) d.synthetic = sid_of( d.original.substr( 0, d.original.rfind( '-' ) ) ) + d.original.substr( d.original.rfind( '-' ) );
				else if ( d.kind == "name_sam_compatible" )
				{
					size_t bs = d.original.find( '\\' );
					if ( bs == std::string::npos ) fail( "rule 7c: NameSamCompatible without DOMAIN\\user" );
					std::string dom = d.original.substr( 0, bs ), user = d.original.substr( bs + 1 );
					std::string sdom = syn_value( dom, "computer_name" ), suser = syn_value( user, "user_name" );
					if ( sdom.empty() ) sdom = gen_class_string( { dom, "identity/" + d.id + "/domain" } ).text;	// a domain account
					if ( suser.empty() ) suser = gen_class_string( { user, "identity/" + d.id + "/user" } ).text;
					d.synthetic = sdom + "\\" + suser;
				}
				else	// install_date / install_time
				{
					if ( !have_instant )
					{
						InstallTime real;
						for ( const std::string& l : split_lines( f_info->data ) )
						{
							std::smatch m;
							static const std::regex r_build( R"(CurrentBuild ([0-9]+))" ), r_utc( R"(^run\.start_utc\s*=\s*(\S+))" );
							if ( l.rfind( "os.registry", 0 ) == 0 && std::regex_search( l, m, r_build ) ) real.os_build = unsigned( std::stoul( m[ 1 ].str() ) );
							if ( std::regex_search( l, m, r_utc ) ) real.capture_utc = m[ 1 ].str();
						}
						for ( const Ident& x : ids )
						{
							if ( x.kind == "install_date" ) real.unix_seconds = std::stoll( x.original );
							if ( x.kind == "install_time" ) real.filetime = std::stoull( x.original );
						}
						inst = gen_install_time( real );
						have_instant = true;
					}
					d.synthetic = d.kind == "install_date" ? std::to_string( inst.unix_seconds ) : std::to_string( inst.filetime );
				}
				if ( d.needles.empty() ) d.needles = { lower( d.original ) };
			}
		}
		for ( Ident& d : ids )
			for ( const std::string& n : d.needles )
				if ( !( n.size() < 6 && std::all_of( n.begin(), n.end(), []( char c ) { return c >= '0' && c <= '9'; } ) ) )	// short pure numbers: too generic for the leak guard
					out.needles.push_back( n );

		// occurrences in every searched text file of the capture (fieldmap rows; 1-based line and byte column)
		std::map<std::string, size_t> occ;
		std::map<std::string, std::string> first_at;
		for ( const TextLc& t : texts )
		{
			const std::string& lc = t.lc;
			for ( const Ident& d : ids )
			{
				if ( !d.only_file.empty() && d.only_file != t.f->name ) continue;
				for ( const std::string& n0 : d.needles )
				{
					std::string n = d.token ? "&" + n0 + "&" : n0;
					std::boyer_moore_horspool_searcher srch( n.begin(), n.end() );
					for ( auto it = std::search( lc.begin(), lc.end(), srch ); it != lc.end(); it = std::search( it + 1, lc.end(), srch ) )
					{
						size_t pos = size_t( it - lc.begin() ), len = n.size();
						if ( d.token ) { pos++; len -= 2; }
						else
						{
							bool esc = pos >= 4 && lc.compare( pos - 4, 4, "\\x5c" ) == 0;	// fwid writes '\' as \x5C: a boundary
							char b = pos && !esc ? lc[ pos - 1 ] : ' ', a = pos + len < lc.size() ? lc[ pos + len ] : ' ';
							// a GUID-shaped needle (8-4-4-4-12) only needs letter / digit boundaries (it may follow a hyphenated prefix); other
							// needles (MACs, serials) must not sit inside a hyphenated token either (e.g. a MAC as a version-1 GUID node)
							const bool guid_shaped = n.size() == 36 && n[ 8 ] == '-' && n[ 13 ] == '-' && n[ 18 ] == '-' && n[ 23 ] == '-';
							bool word = d.rule == "4a" || guid_shaped ? ( is_alnum( b ) || is_alnum( a ) ) : ( is_alnum( b ) || is_alnum( a ) || b == '-' || a == '-' );
							if ( word ) continue;	// part of a longer token
						}
						size_t li = line_of( t, pos ) - 1;
						out.fields.push_back( { t.f->name, d.id, d.rule, d.kind, pos - t.starts[ li ] + 1, li + 1, uint32_t( len ) } );
						if ( !occ[ d.id ]++ ) first_at[ d.id ] = fmt( "%s:%zu", t.f->name.c_str(), li + 1 );
					}
				}
			}
		}
		for ( Ident& d : ids )
		{
			if ( !rule_on( rules, d.rule ) ) d.synthetic = d.original;	// rule disabled: the real value stays (the leak guard refuses a synthetic profile)
			if ( !occ[ d.id ] && !d.preset ) fail( "identity " + d.id + ": its anchor was not found again by the occurrence search" );
			if ( d.preset ) d.source = first_at.count( d.id ) ? first_at[ d.id ] : "-";
			out.identity.push_back( { d.id, d.kind, d.rule, d.source, occ[ d.id ], d.original, d.synthetic } );
		}
		// the Enum\STORAGE\Volume instance paths with the rule 4c replacement applied (device IDs exact)
		auto syn_of = [ & ]( const std::string& v ) -> const Ident& { return ids[ by_key.at( key_of( v ) ) ]; };
		std::set<std::string> seen;
		for ( auto& [ dev, inst ] : volume_paths )
		{
			std::smatch mv;
			std::regex_match( inst, mv, r_volume );
			std::string o = "STORAGE\\" + dev + "\\" + inst;
			if ( !seen.insert( lower( o ) ).second ) continue;
			std::string g = mv[ 1 ].str(), sg = syn_of( g ).synthetic;	// in the case of this occurrence
			if ( has_lower_hex( g ) ) sg = lower( sg );
			out.storage_paths.push_back( { o, "STORAGE\\" + dev + "\\" + sg + mv[ 2 ].str() } );
		}
		return out;
	}

	// ── output ────────────────────────────────────────────────────────────────────────────────────────────────────────────
	fs::path ProfileGenerator::check_git_work_tree( const fs::path& p0 )
	{
		std::error_code ec;
		fs::path p = fs::weakly_canonical( fs::absolute( p0, ec ), ec );
		if ( ec ) p = fs::absolute( p0 );
		for ( ;; )
		{
			if ( fs::exists( p / ".git", ec ) ) return p;
			fs::path parent = p.parent_path();
			if ( parent.empty() || parent == p ) return {};
			p = parent;
		}
	}

	void ProfileGenerator::save_profile( const fs::path& dir, const std::map<std::string, Bytes>& files, bool original )
	{
		if ( original )
		{
			fs::path wt = check_git_work_tree( dir );
			if ( !wt.empty() ) fail( "refusing to write the ORIGINAL profile (real identifiers) inside a git work tree: " + show( dir ) + " is under " + show( wt ) );
		}
		std::error_code ec;
		if ( fs::exists( dir, ec ) )
		{
			std::vector<std::string> stale;
			for ( auto& e : fs::recursive_directory_iterator( dir ) )
				if ( e.is_regular_file() && !files.count( fs::relative( e.path(), dir ).string() ) ) stale.push_back( fs::relative( e.path(), dir ).string() );
			if ( !stale.empty() ) fail( "the output folder " + show( dir ) + " holds " + std::to_string( stale.size() ) + " file(s) that are not part of the profile (first: " + stale[ 0 ] + "); remove them first" );
		}
		for ( auto& [ rel, data ] : files )
		{
			fs::path f = dir / rel;
			fs::create_directories( f.parent_path() );
			std::ofstream o( f, std::ios::binary | std::ios::trunc );
			o.write( reinterpret_cast<const char*>( data.data() ), std::streamsize( data.size() ) );
			if ( !o ) fail( "cannot write " + show( f ) );
		}
	}

	std::vector<ByteRange> ProfileGenerator::check_diff_ranges( const Bytes& a, const Bytes& b )
	{
		if ( a.size() != b.size() ) fail( "check_diff_ranges: different sizes" );
		std::vector<ByteRange> r;
		for ( size_t i = 0; i < a.size(); i++ )
		{
			if ( a[ i ] == b[ i ] ) continue;
			if ( !r.empty() && r.back().last == i ) r.back().last = i + 1;
			else r.push_back( { i, i + 1 } );
		}
		return r;
	}

	// the files of one profile: smbios.bin, acpi\*, edid\*, identity.tsv, fieldmap.tsv, build_config.txt, manifest.txt
	static std::map<std::string, Bytes> make_files( const Identity& p, bool original, const std::vector<std::pair<std::string, std::string>>& prov, const std::string& config_text )
	{
		std::map<std::string, Bytes> files;
		const char* env = env_dir_name( p.env );
		const char* kind = original ? "ORIGINAL: real values - LOCAL ONLY, never commit or push" : "SYNTHETIC: every replaced value is synthetic, not host data";
		files[ "smbios.bin" ] = original ? p.smbios_original : p.smbios_synthetic;
		for ( auto& [ name, data ] : p.acpi ) files[ "acpi\\" + name ] = data;
		for ( const EdidBlock& e : original ? p.edid_original : p.edid_synthetic ) files[ "edid\\" + e.name ] = e.bytes;

		std::ostringstream id;
		id << "# " << k_tool << " - identity of the fwid text outputs - " << env << " - " << kind << "\n"
		   << "# Rules (README.md / profile_generator.hpp): 3 MAC, 4a computer name, 4c GUID, 4d USB / HID serial (4b: PnP hashes exact),\n"
		   << "# 6 EDID serial, 7a ProductId, 7b install instant, 7c user name, 7d SID, 7e volume serial, 8a-8e fwid 1.4 identifiers.\n"
		   << "# columns: id, kind, rule, first anchor in the capture (file:line), occurrences in the capture's text files (fieldmap.tsv), value\n"
		   << "# fwid writes '\\' inside quoted strings as \\x5C; values and paths here use plain characters. GUIDs in {UPPER} form unless\n"
		   << "# the identifier keeps its own style (MachineGuid & co.). install_date = unix seconds, install_time = FILETIME (decimal).\n";
		for ( const IdentityEntry& r : p.identity )
			id << r.id << '\t' << r.kind << '\t' << r.rule << '\t' << r.source << '\t' << r.occurrences << '\t' << ( original ? r.original : r.synthetic ) << "\n";
		id << "# Enum\\STORAGE\\Volume instance paths (the volume GUID per rule 4c, the partition offset exact)\n";
		int k = 0;
		for ( const StoragePath& sp : p.storage_paths )
			id << fmt( "P%02d", ++k ) << "\tstorage_path\t4c\t-\t-\t" << ( original ? sp.original : sp.synthetic ) << "\n";
		files[ "identity.tsv" ] = to_bytes( id.str() );

		std::ostringstream fm;
		fm << "# " << k_tool << " - field map - " << env << " - every replaced field as LOCATION + LENGTH + RULE, never a value; identical in the synthetic and the original profile\n"
		   << "# columns: id, rule, file (smbios.bin / edid\\edid_<n>.bin of this profile, or the capture's fwid text file), offset (blobs: absolute byte offset, smbios.bin incl. the 8-byte RawSMBIOSData header) or line:column (text) or - (relation rows), length in bytes, location\n";
		for ( const FieldMapEntry& f : p.fields )
			fm << f.id << '\t' << f.rule << '\t' << f.area << '\t'
			   << ( f.rule.back() == 'r' ? std::string( "-" ) : f.line ? fmt( "%llu:%llu", (unsigned long long)f.line, (unsigned long long)f.offset ) : fmt( "0x%04llX", (unsigned long long)f.offset ) )
			   << '\t' << f.length << '\t' << f.location << "\n";
		files[ "fieldmap.tsv" ] = to_bytes( fm.str() );
		files[ BuildConfig::k_file ] = to_bytes( config_text );

		std::ostringstream mf;
		mf << "# " << k_tool << " - platform profile manifest - " << env << " (" << env_switch( p.env ) << ") - " << kind << "\n"
		   << "# Everything not listed in fieldmap.tsv is byte-exact from the capture. SHA-256 = uppercase hex (Get-FileHash).\n";
		auto line = [ & ]( const std::string& key, const std::string& val ) { mf << key << std::string( key.size() < 36 ? 36 - key.size() : 1, ' ' ) << "= " << val << "\n"; };
		line( "tool", k_tool );
		line( "profile.kind", original ? "original (real values, LOCAL ONLY)" : "synthetic (replaced fields: synthetic, not host data)" );
		line( "environment", env );
		for ( auto& [ key, val ] : prov ) line( key, val );
		mf << "# size_bytes  sha256  file\n";
		for ( auto& [ name, data ] : files ) mf << fmt( "%10zu  ", data.size() ) << sha256_hex( data ) << "  " << name << "\n";
		mf << "# " << files.size() << " file(s)\n";
		files[ "manifest.txt" ] = to_bytes( mf.str() );
		return files;
	}

	Seed ProfileGenerator::load_seed( const fs::path& file, bool allow_create, BuildLog& log )
	{
		std::error_code ec;
		Seed s;
		if ( fs::exists( file, ec ) )
		{
			Bytes raw = read_file( file );
			std::string t( raw.begin(), raw.end() );
			std::smatch m;
			static const std::regex r( R"((?:^|\n)seed\s*=\s*([0-9A-Fa-f]{64})\s*(?:\r?\n|$))" );
			if ( !std::regex_search( t, m, r ) ) fail( "seed file " + show( file ) + " has no \"seed = <64 hex digits>\" line" );
			s.hex = upper( m[ 1 ].str() );
			for ( size_t i = 0; i < 32; i++ ) s.bytes[ i ] = uint8_t( std::stoul( s.hex.substr( i * 2, 2 ), nullptr, 16 ) );
			log.info( "seed", show( file ) + " (existing, sha256 " + sha256_hex( raw ) + ")" );
			return s;
		}
		if ( !allow_create ) fail( "seed file " + show( file ) + " missing (a check never creates it)" );
		Bytes b = random_bytes( 32 );
		std::copy( b.begin(), b.end(), s.bytes.begin() );
		s.hex = hex_of( b.data(), b.size(), true );
		save_seed( file, s );
		log.info( "seed", show( file ) + " CREATED (BCryptGenRandom, 32 bytes)" );
		return s;
	}
	void ProfileGenerator::save_seed( const fs::path& file, const Seed& s )
	{
		std::string t = "# emu-profilegen seed (ledger U1510): 32 bytes from BCryptGenRandom (BCRYPT_USE_SYSTEM_PREFERRED_RNG).\n"
						"# Every synthetic identifier value of Emulator\\data\\platform\\<env>\\ is an HMAC-SHA256 stream keyed with this seed; the\n"
						"# stream labels name a field location (environment + field id), never a value. Not derived from host data. The same\n"
						"# seed gives the same identity; emu-profilegen --new-identity writes a fresh one (a different, equally consistent identity).\n"
						"seed = " + s.hex + "\n";
		fs::create_directories( file.parent_path() );
		std::ofstream o( file, std::ios::binary | std::ios::trunc );
		o << t;
		if ( !o ) fail( "cannot write the seed file " + show( file ) );
	}

	// ── RuleSet / BuildConfig / BuildLog ──────────────────────────────────────────────────────────────────────────────
	std::string RuleSet::to_string() const
	{
		std::string s;
		for ( auto [ on, name ] : { std::pair { r1, "1" }, { r2, "2" }, { r2h, "2h" }, { r2o, "2o" }, { r3, "3" }, { r4a, "4a" }, { r4b, "4b" }, { r4c, "4c" }, { r4d, "4d" },
				  { r6, "6" }, { r7, "7" }, { r8, "8" } } )
			if ( on ) s += ( s.empty() ? "" : " " ) + std::string( name );
		return s.empty() ? "none" : s;
	}
	RuleSet RuleSet::parse( const std::string& list )
	{
		RuleSet r { false, false, false, false, false, false, false, false, false, false, false, false };
		std::istringstream is( list );
		for ( std::string w; is >> w; )
		{
			if ( w == "1" ) r.r1 = true;
			else if ( w == "2" ) r.r2 = true;
			else if ( w == "2h" ) r.r2h = true;
			else if ( w == "2o" ) r.r2o = true;
			else if ( w == "3" ) r.r3 = true;
			else if ( w == "4a" ) r.r4a = true;
			else if ( w == "4b" ) r.r4b = true;
			else if ( w == "4c" ) r.r4c = true;
			else if ( w == "4d" ) r.r4d = true;
			else if ( w == "6" ) r.r6 = true;
			else if ( w == "7" ) r.r7 = true;
			else if ( w == "8" ) r.r8 = true;
			else if ( w != "none" ) fail( "rules: unknown rule \"" + w + "\" (known: 1 2 2h 2o 3 4a 4b 4c 4d 6 7 8)" );
		}
		return r;
	}

	const BuildConfig& BuildConfig::pinned( Environment e )
	{
		// the pinned clean captures: --guest is built ONLY from Run 3 (no VmLoader), --host ONLY from the clean host run
		static const BuildConfig guest = [] {
			BuildConfig c;
			c.env = Environment::vmware_guest;
			c.capture_parent = "fwid\\baseline";
			c.capture_folder = "Run3_clean_guest_*";
			c.capture_manifest_sha256 = "57268E68D23B488C74AC0ABA57EDD275DCEC9680153367A8C4A8786A30AE8E1C";
			c.companions = { { "baseline\\Run3_clean_guest_probes_20261015.sha256", "748A3515D7FC401B14C97E650996D15426E891C477B5CF75F28C6EAF59F89E8D" } };
			return c;
		}();
		static const BuildConfig host = [] {
			BuildConfig c;
			c.env = Environment::bare_host;
			c.capture_parent = "baseline\\Host_clean_20261016_fwid13";	// fwid 1.3 (firmware byte-identical to the fwid 1.2 capture)
			c.capture_folder = "*";
			c.capture_manifest_sha256 = "C9B008D860BEB95F3E7BFD5724FE6670A01EC4024E9C5471D7EF34851D706B89";
			c.companions = { { "baseline\\Host_clean_20261016_fwid13.sha256", "C2BD8A2B0AADA8A679D066AEA07126CFFA5B6B237512D2698761E6D7B964550A" },
				{ "baseline\\Host_clean_20261016.sha256", "1C8C498DB6CD26A57F8673375BF563868F12D3AEB79901363DF2EEEE8A8EF78C" },
				{ "baseline\\Host_clean_20261016_xinuse_all_lps.sha256", "CA726BF73851C9251D9DF30737B4CE171053B4814610798DA6551ECC03348F5C" } };
			return c;
		}();
		return e == Environment::vmware_guest ? guest : host;
	}
	BuildConfig BuildConfig::defaults( Environment e )
	{
		BuildConfig c = pinned( e );
		c.tool = k_tool;
		c.out = std::string( "Emulator\\data\\platform\\" ) + env_dir_name( e );
		c.original_out = std::string( "profiles_original\\" ) + env_dir_name( e );
		c.seed_file = "Emulator\\data\\platform\\profilegen_seed.txt";
		c.rules.r2h = e == Environment::bare_host;	// rule 2h is a bare-host rule; the guest keeps every type 17 string
		const BuildConfig& peer = pinned( e == Environment::vmware_guest ? Environment::bare_host : Environment::vmware_guest );
		c.peer_capture_parent = peer.capture_parent;
		c.peer_capture_folder = peer.capture_folder;
		c.peer_capture_manifest_sha256 = peer.capture_manifest_sha256;
		return c;
	}
	std::string BuildConfig::save() const
	{
		std::ostringstream o;
		o << "# emu-profilegen BuildConfig (format 1): every input of one platform-profile build. Rebuild with\n"
			 "#   emu-profilegen --config build_config.txt [--check]\n"
			 "# Paths: out / seed_file relative to repo_root; original_out, capture_parent and companion relative to capture_root;\n"
			 "# \"auto\" = NOVMP_CAPTURE_ROOT, else the first ancestor folder \"capture\" (capture_root) / the folder holding NoVmp.sln (repo_root).\n"
			 "# No identifier values: the capture folder is a name pattern plus its pinned manifest SHA-256. peer_capture_*: the other\n"
			 "# environment's clean capture, read only for the USB serial table both profiles share (rule 4d). seed: random (not host\n"
			 "# data); the same seed gives the same identity.\n";
		auto kv = [ & ]( const char* k, const std::string& v ) { o << k << std::string( strlen( k ) < 29 ? 29 - strlen( k ) : 1, ' ' ) << "= " << v << "\n"; };
		kv( "format", std::to_string( format ) );
		kv( "tool", tool );
		kv( "environment", env_dir_name( env ) );
		kv( "capture_root", capture_root );
		kv( "capture_parent", capture_parent );
		kv( "capture_folder", capture_folder );
		kv( "capture_manifest_sha256", capture_manifest_sha256 );
		for ( auto& [ f, h ] : companions ) kv( "companion", f + " " + h );
		kv( "peer_capture_parent", peer_capture_parent );
		kv( "peer_capture_folder", peer_capture_folder );
		kv( "peer_capture_manifest_sha256", peer_capture_manifest_sha256 );
		kv( "repo_root", repo_root );
		kv( "out", out );
		kv( "original_out", original_out );
		kv( "seed_file", seed_file );
		if ( !seed.empty() ) kv( "seed", seed );
		kv( "rules", rules.to_string() );
		return o.str();
	}
	BuildConfig BuildConfig::load( const std::string& text )
	{
		BuildConfig c;
		c.companions.clear();
		std::set<std::string> seen;
		std::istringstream is( text );
		size_t n = 0;
		for ( std::string l; std::getline( is, l ); )
		{
			n++;
			if ( !l.empty() && l.back() == '\r' ) l.pop_back();
			size_t a = l.find_first_not_of( " \t" );
			if ( a == std::string::npos || l[ a ] == '#' ) continue;
			size_t eq = l.find( '=' );
			if ( eq == std::string::npos ) fail( fmt( "build config line %zu: no \"key = value\"", n ) );
			std::string k = l.substr( a, eq - a ), v = l.substr( eq + 1 );
			while ( !k.empty() && ( k.back() == ' ' || k.back() == '\t' ) ) k.pop_back();
			v.erase( 0, std::min( v.size(), v.find_first_not_of( " \t" ) ) );
			while ( !v.empty() && ( v.back() == ' ' || v.back() == '\t' ) ) v.pop_back();
			if ( k != "companion" && !seen.insert( k ).second ) fail( fmt( "build config line %zu: key \"%s\" given twice", n, k.c_str() ) );
			if ( k == "format" ) { if ( v != "1" ) fail( fmt( "build config line %zu: format %s not supported (1)", n, v.c_str() ) ); }
			else if ( k == "tool" ) c.tool = v;
			else if ( k == "environment" )
			{
				if ( v == "vmware_guest" ) c.env = Environment::vmware_guest;
				else if ( v == "bare_host" ) c.env = Environment::bare_host;
				else fail( fmt( "build config line %zu: environment must be vmware_guest or bare_host", n ) );
			}
			else if ( k == "capture_root" ) c.capture_root = v;
			else if ( k == "capture_parent" ) c.capture_parent = v;
			else if ( k == "capture_folder" ) c.capture_folder = v;
			else if ( k == "capture_manifest_sha256" ) c.capture_manifest_sha256 = v;
			else if ( k == "companion" )
			{
				size_t sp = v.find_last_of( ' ' );
				if ( sp == std::string::npos ) fail( fmt( "build config line %zu: companion = <file> <sha256>", n ) );
				std::string f = v.substr( 0, sp ), h = v.substr( sp + 1 );
				while ( !f.empty() && f.back() == ' ' ) f.pop_back();
				c.companions.emplace_back( f, h );
			}
			else if ( k == "peer_capture_parent" ) c.peer_capture_parent = v;
			else if ( k == "peer_capture_folder" ) c.peer_capture_folder = v;
			else if ( k == "peer_capture_manifest_sha256" ) c.peer_capture_manifest_sha256 = v;
			else if ( k == "repo_root" ) c.repo_root = v;
			else if ( k == "out" ) c.out = v;
			else if ( k == "original_out" ) c.original_out = v;
			else if ( k == "seed_file" ) c.seed_file = v;
			else if ( k == "seed" )
			{
				if ( v.size() != 64 || !std::all_of( v.begin(), v.end(), is_hex ) ) fail( fmt( "build config line %zu: seed must be 64 hex digits", n ) );
				c.seed = upper( v );
			}
			else if ( k == "rules" ) c.rules = RuleSet::parse( v );
			else if ( k.rfind( "resolved.", 0 ) == 0 ) {}	// the local original copy's record of the resolved paths
			else fail( fmt( "build config line %zu: unknown key \"%s\"", n, k.c_str() ) );
		}
		for ( const char* k : { "environment", "capture_parent", "capture_folder", "capture_manifest_sha256", "out", "seed_file", "rules" } )
			if ( !seen.count( k ) ) fail( std::string( "build config: missing key \"" ) + k + "\"" );
		if ( c.env == Environment::vmware_guest && c.rules.r2h ) fail( "build config: rule 2h is a bare_host rule (the guest keeps every type 17 string)" );
		return c;
	}
	void BuildConfig::save_file( const fs::path& file ) const
	{
		std::ofstream o( file, std::ios::binary | std::ios::trunc );
		o << save();
		if ( !o ) fail( "cannot write the build config " + show( file ) );
	}
	BuildConfig BuildConfig::load_file( const fs::path& file )
	{
		Bytes b = read_file( file );
		return load( std::string( b.begin(), b.end() ) );
	}

	std::string BuildLog::text() const
	{
		std::string s;
		for ( const Entry& e : entries )
			s += std::string( "[profilegen] " ) + ( e.level == Level::error ? "ERROR " : e.level == Level::warning ? "WARNING " : "" ) + e.step + ": " + e.text + "\n";
		return s;
	}

	// ── load_capture / gen_identity / gen_build / run_build ─────────────────────────────────────────────────────────────
	namespace
	{
		fs::path exe_dir()
		{
			wchar_t buf[ MAX_PATH * 4 ];
			DWORD n = GetModuleFileNameW( nullptr, buf, DWORD( std::size( buf ) ) );
			return n ? fs::path( std::wstring( buf, n ) ).parent_path() : fs::current_path();
		}
		// occurrences of a value as a token: not embedded in a longer run of letters / digits (a value inside a longer
		// alphanumeric token, e.g. 8 digits inside a 16-digit partition offset, is not that value)
		size_t count_ci( const std::string& hay_lower, const std::string& needle_lower )
		{
			size_t n = 0;
			for ( size_t p = hay_lower.find( needle_lower ); p != std::string::npos; p = hay_lower.find( needle_lower, p + 1 ) )
			{
				bool b = p && is_alnum( hay_lower[ p - 1 ] ) && is_alnum( needle_lower.front() );
				bool a = p + needle_lower.size() < hay_lower.size() && is_alnum( hay_lower[ p + needle_lower.size() ] ) && is_alnum( needle_lower.back() );
				if ( !a && !b ) n++;
			}
			return n;
		}
		size_t count_raw( const std::string& hay_lower, const std::string& needle_lower )
		{
			size_t n = 0;
			for ( size_t p = hay_lower.find( needle_lower ); p != std::string::npos; p = hay_lower.find( needle_lower, p + 1 ) ) n++;
			return n;
		}
		size_t count_bin( const Bytes& hay, const Bytes& needle )
		{
			size_t n = 0;
			for ( auto it = std::search( hay.begin(), hay.end(), needle.begin(), needle.end() ); it != hay.end(); it = std::search( it + 1, hay.end(), needle.begin(), needle.end() ) ) n++;
			return n;
		}
		std::string redact( std::string s, const std::vector<IdentityEntry>& identity )
		{
			for ( const IdentityEntry& r : identity )
			{
				if ( r.rule != "4a" ) continue;
				for ( size_t p; ( p = lower( s ).find( lower( r.original ) ) ) != std::string::npos; ) s.replace( p, r.original.size(), "<computer-name>" );
			}
			return s;
		}
		bool matches_pattern( const std::string& name, const std::string& pat )
		{
			if ( !pat.empty() && pat.back() == '*' ) return name.rfind( pat.substr( 0, pat.size() - 1 ), 0 ) == 0;
			return name == pat;
		}
		// a path of the saved SYNTHETIC config: relative to base when inside it, else the %USERPROFILE% form
		std::string portable( const fs::path& p, const fs::path& base )
		{
			std::error_code ec;
			fs::path a = fs::weakly_canonical( fs::absolute( p ), ec ), b = fs::weakly_canonical( fs::absolute( base ), ec );
			fs::path r = a.lexically_relative( b );
			if ( !base.empty() && !r.empty() && r.native().rfind( L"..", 0 ) != 0 ) return r.string();
			return show( a );
		}
		fs::path expand( const std::string& s )
		{
			if ( s.rfind( "%USERPROFILE%", 0 ) != 0 ) return s;
			return env_var( "USERPROFILE" ) + s.substr( 13 );
		}
		fs::path find_one( const fs::path& parent, const std::string& pattern, const char* what )
		{
			std::vector<fs::path> hits;
			std::error_code ec;
			if ( fs::is_directory( parent, ec ) )
				for ( auto& e : fs::directory_iterator( parent ) )
					if ( e.is_directory() && matches_pattern( e.path().filename().string(), pattern ) ) hits.push_back( e.path() );
			if ( hits.size() != 1 ) fail( fmt( "expected exactly one %s folder %s under the capture root, found %zu", what, pattern.c_str(), hits.size() ) );
			return hits[ 0 ];
		}
		// the manifest entries (size, SHA-256, name) of a capture folder, each verified
		std::vector<std::string> verify_manifest( const fs::path& cap, const Bytes& manifest )
		{
			std::vector<std::string> names;
			for ( const std::string& l : split_lines( manifest ) )
			{
				if ( l.empty() || l[ 0 ] == '#' ) continue;
				std::istringstream is( l );
				unsigned long long size = 0;
				std::string sha, name;
				if ( !( is >> size >> sha >> name ) ) fail( "capture manifest: unreadable entry line" );
				Bytes b = read_file( cap / name );
				if ( b.size() != size || sha256_hex( b ) != sha ) fail( "capture manifest: " + name + " does not match its size / SHA-256" );
				names.push_back( name );
			}
			return names;
		}
	}

	CaptureInputs ProfileGenerator::load_capture( const fs::path& cap, const fs::path& root, const BuildConfig& c, BuildLog& log )
	{
		CaptureInputs in;
		in.folder_name = cap.filename().string();
		const Bytes manifest = read_file( cap / "manifest.txt" );
		in.manifest_sha256 = sha256_hex( manifest );
		if ( in.manifest_sha256 != c.capture_manifest_sha256 )
			fail( std::string( "the capture's manifest.txt SHA-256 is not the one of the build config (" ) + env_dir_name( c.env ) + "): got " + in.manifest_sha256 );
		auto lines = split_lines( manifest );
		if ( !lines.empty() ) in.tool_line = lines[ 0 ];
		std::vector<std::string> names = verify_manifest( cap, manifest );
		in.manifest_entries = names.size();
		log.info( "capture", fmt( "manifest SHA-256 %s = the pinned clean capture; %zu of %zu entries match (size + SHA-256)", in.manifest_sha256.c_str(), names.size(), names.size() ) );
		for ( auto& [ rel, want ] : c.companions )
		{
			std::string got = sha256_hex( read_file( root / rel ) );
			if ( got != want ) fail( "companion hash list " + rel + " is not the pinned one" );
			in.companions.emplace_back( rel, got );
		}
		log.info( "capture", fmt( "companion hash lists (CPUID / SMM / XINUSE probes and frozen hash lists of the same state, not consumed here): %zu pinned, match", in.companions.size() ) );
		std::sort( names.begin(), names.end() );
		in.rsmb = read_file( cap / "rsmb.bin" );
		for ( const std::string& n : names )
		{
			bool bin = n.size() > 4 && n.substr( n.size() - 4 ) == ".bin", txt = n.size() > 4 && n.substr( n.size() - 4 ) == ".txt";
			if ( bin && ( n.rfind( "acpi_", 0 ) == 0 || n.rfind( "hwacpi_", 0 ) == 0 ) ) in.acpi.emplace_back( n, read_file( cap / n ) );
			else if ( bin && n.rfind( "edid_", 0 ) == 0 ) in.edid.push_back( { n, read_file( cap / n ) } );
			else if ( txt ) in.texts.push_back( { n, read_file( cap / n ) } );
		}
		log.info( "capture", fmt( "inputs: rsmb.bin %zu bytes, %zu ACPI table files, %zu EDID values, %zu text files (manifest entries)", in.rsmb.size(), in.acpi.size(),
			in.edid.size(), in.texts.size() ) );

		// the shared USB serial table (rule 4d): the bare-host capture's enum_registry.txt, then the guest's; the peer capture is
		// verified the same way (pinned manifest SHA-256, the entry's size + SHA-256)
		Bytes own_enum;
		for ( const TextFile& f : in.texts ) if ( f.name == "enum_registry.txt" ) own_enum = f.data;
		const fs::path peer = find_one( root / c.peer_capture_parent, c.peer_capture_folder, "peer capture" );
		const Bytes pm = read_file( peer / "manifest.txt" );
		if ( sha256_hex( pm ) != c.peer_capture_manifest_sha256 ) fail( "the peer capture's manifest.txt is not the pinned one" );
		Bytes peer_enum;
		for ( const std::string& l : split_lines( pm ) )
		{
			std::istringstream is( l );
			unsigned long long size = 0;
			std::string sha, name;
			if ( l.empty() || l[ 0 ] == '#' || !( is >> size >> sha >> name ) || name != "enum_registry.txt" ) continue;
			peer_enum = read_file( peer / name );
			if ( peer_enum.size() != size || sha256_hex( peer_enum ) != sha ) fail( "peer capture: enum_registry.txt does not match its size / SHA-256" );
		}
		if ( peer_enum.empty() ) fail( "peer capture: no enum_registry.txt" );
		in.usb_sources = c.env == Environment::bare_host ? std::vector<Bytes> { own_enum, peer_enum } : std::vector<Bytes> { peer_enum, own_enum };
		log.info( "capture", "peer capture (the other environment) verified: pinned manifest, enum_registry.txt read for the shared USB serial table" );
		return in;
	}

	Identity ProfileGenerator::gen_identity( const CaptureInputs& in )
	{
		Identity p;
		p.env = c_.env;
		p.smbios_original = in.rsmb;
		p.acpi = in.acpi;
		p.edid_original = in.edid;
		// rule 6 first (its serials are presets for the edid.txt occurrences), then the text identity (the synthetic MACs are
		// needed for the UUID-MAC relation), then SMBIOS
		const EdidGen eg = gen_edid( { in.edid } );
		p.edid_synthetic = eg.synthetic;
		p.edid_ext_blocks = eg.ext_blocks;
		p.edid_ext_hits = eg.ext_hits;
		const std::vector<std::string> usb_table = in.usb_sources.empty() ? std::vector<std::string> {} : parse_usb_serial_table( in.usb_sources );
		TextIdentityGen tg = gen_text_identity( { in.texts, usb_table, eg.presets } );
		SmbiosGen sg = gen_smbios( { in.rsmb, c_.rules.r1 && c_.rules.r3 ? tg.macs : std::vector<MacPair> {} } );
		p.smbios_synthetic = sg.synthetic;
		p.fields = sg.fields;
		p.smbios_fields = sg.fields.size();
		p.fields.insert( p.fields.end(), eg.fields.begin(), eg.fields.end() );
		p.edid_fields = eg.fields.size();
		p.fields.insert( p.fields.end(), tg.fields.begin(), tg.fields.end() );
		p.identity = tg.identity;
		p.storage_paths = tg.storage_paths;
		p.macs = tg.macs;
		p.needles = sg.needles;
		p.needles.insert( p.needles.end(), eg.needles.begin(), eg.needles.end() );
		p.needles.insert( p.needles.end(), tg.needles.begin(), tg.needles.end() );
		p.binary_needles = sg.binary_needles;
		p.binary_needles.insert( p.binary_needles.end(), tg.binary_needles.begin(), tg.binary_needles.end() );

		// ── self-checks ────────────────────────────────────────────────────────────────────────────────────────────────
		// (a) the synthetic SMBIOS has the same layout: offsets, types, lengths, handles, string offsets / lengths
		const SmbiosTable t0 = parse_smbios( p.smbios_original ), t1 = parse_smbios( p.smbios_synthetic );
		bool same = t1.structures.size() == t0.structures.size();
		for ( size_t i = 0; same && i < t0.structures.size(); i++ )
		{
			const auto &a = t0.structures[ i ], &b = t1.structures[ i ];
			same = a.offset == b.offset && a.type == b.type && a.length == b.length && a.handle == b.handle && a.end == b.end && a.strings.size() == b.strings.size();
			for ( size_t k = 0; same && k < a.strings.size(); k++ ) same = a.strings[ k ].offset == b.strings[ k ].offset && a.strings[ k ].length == b.strings[ k ].length;
		}
		if ( !same ) fail( "self-check: the synthetic SMBIOS layout differs from the capture" );
		// (b) per blob (smbios.bin, every edid_<n>.bin): the byte diff lies exactly inside the mapped ranges, every mapped field
		// changed (the recomputed EDID checksum may coincide)
		auto check_blob = [ & ]( const std::string& area, const Bytes& a, const Bytes& b ) {
			std::vector<uint8_t> mapped( a.size(), 0 );
			for ( const FieldMapEntry& f : p.fields )
			{
				if ( f.area != area ) continue;
				for ( size_t k = 0; k < f.length; k++ ) mapped[ f.offset + k ] = 1;
				if ( f.rule != "6c" && std::equal( a.begin() + f.offset, a.begin() + f.offset + f.length, b.begin() + f.offset ) ) fail( "self-check: mapped field " + f.id + " did not change" );
			}
			size_t n = 0;
			for ( const ByteRange& r : check_diff_ranges( a, b ) )
				for ( size_t i = r.first; i < r.last; i++ )
				{
					if ( !mapped[ i ] ) fail( fmt( "self-check: %s byte 0x%04zX changed outside the field map", area.c_str(), i ) );
					n++;
				}
			return n;
		};
		check_blob( "smbios.bin", p.smbios_original, p.smbios_synthetic );
		for ( size_t i = 0; i < p.edid_original.size(); i++ )
		{
			check_blob( "edid\\" + p.edid_original[ i ].name, p.edid_original[ i ].bytes, p.edid_synthetic[ i ].bytes );
			uint8_t sum = 0;
			for ( size_t k = 0; k < 128; k++ ) sum = uint8_t( sum + p.edid_synthetic[ i ].bytes[ k ] );
			if ( sum ) fail( "self-check: a synthetic EDID base block has an invalid checksum" );
		}
		// (c) every identity value of an enabled rule changed and kept its length
		for ( const IdentityEntry& r : p.identity )
			if ( r.synthetic.size() != r.original.size() || ( rule_on( c_.rules, r.rule ) && ieq( r.synthetic, r.original ) ) ) fail( "self-check: identity " + r.id + " not replaced at the same length" );
		for ( const StoragePath& sp : p.storage_paths )
			if ( sp.original.size() != sp.synthetic.size() ) fail( "self-check: a storage path changed its length" );
		// (d) the CHID-relevant SMBIOS fields are unchanged
		ChidCheck cc = check_chid_fields( p.smbios_original, p.smbios_synthetic );
		if ( cc.changed ) fail( fmt( "self-check: %zu CHID-relevant SMBIOS field(s) changed - a CHID input became synthetic", cc.changed ) );
		return p;
	}

	GenResult ProfileGenerator::gen_build( const CaptureInputs& in, const std::string& resolved_root )
	{
		GenResult res;
		res.config = c_;
		BuildLog& log = res.log;
		try
		{
			const Environment env = c_.env;
			Identity& p = res.identity;
			p = gen_identity( in );
			const SmbiosTable t0 = parse_smbios( p.smbios_original );
			log.info( "smbios", fmt( "version %u.%u, table %u bytes, %zu structures", t0.major, t0.minor, t0.length, t0.structures.size() ) );
			for ( size_t i = 0; i < p.smbios_fields; i++ )
			{
				const FieldMapEntry& f = p.fields[ i ];
				log.info( "smbios", f.id + " rule " + f.rule + fmt( " smbios.bin 0x%04llX len %u: ", (unsigned long long)f.offset, f.length ) + f.location );
			}
			size_t smbios_diff = 0, mapped_bytes = 0;
			for ( const ByteRange& r : check_diff_ranges( p.smbios_original, p.smbios_synthetic ) ) smbios_diff += r.last - r.first;
			for ( size_t i = 0; i < p.smbios_fields; i++ ) mapped_bytes += p.fields[ i ].length;
			log.info( "self-check", fmt( "smbios layout identical; %zu bytes differ, all inside the %zu mapped ranges (%zu bytes); every mapped field changed", smbios_diff, p.smbios_fields, mapped_bytes ) );
			ChidCheck cc = check_chid_fields( p.smbios_original, p.smbios_synthetic );
			log.info( "self-check", fmt( "CHID-relevant SMBIOS fields: %zu compared, %zu changed", cc.checked, cc.changed ) );
			if ( in.edid.empty() ) log.info( "edid", "not in this capture (fwid < 1.3): section omitted" );
			else
				log.info( "edid", fmt( "%zu EDID values: %zu fields replaced (serial field / FFh string), checksums recomputed and valid; extension blocks %zu, with a real serial %zu",
					p.edid_original.size(), p.edid_fields - p.edid_original.size(), p.edid_ext_blocks, p.edid_ext_hits ) );
			std::map<std::string, size_t> per_kind;
			for ( const IdentityEntry& r : p.identity ) per_kind[ r.kind ]++;
			std::string kinds;
			for ( auto& [ k, n ] : per_kind ) kinds += ( kinds.empty() ? "" : ", " ) + k + " " + std::to_string( n );
			log.info( "identity", fmt( "%zu identifiers (%s), %zu occurrence + relation rows, %zu storage instance paths", p.identity.size(), kinds.c_str(),
				p.fields.size() - p.smbios_fields - p.edid_fields, p.storage_paths.size() ) );
			for ( const IdentityEntry& r : p.identity )
				log.info( "identity", fmt( "%s %s rule %s first at %s, %zu occurrence(s), length %zu", r.id.c_str(), r.kind.c_str(), r.rule.c_str(), r.source.c_str(), r.occurrences, r.original.size() ) );
			std::string optional;
			for ( const char* fn : { "os_ids.txt", "edid.txt", "license.txt", "update_ids.txt", "disks.txt", "hid.txt", "gpu.txt", "bluetooth.txt", "audio.txt", "chid.txt",
					  "platform_security.txt", "environment.txt", "wifi.txt", "printers.txt" } )
			{
				bool have = std::any_of( in.texts.begin(), in.texts.end(), [ & ]( const TextFile& f ) { return f.name == fn; } );
				optional += ( optional.empty() ? "" : ", " ) + std::string( fn ) + ( have ? " yes" : " no" );
				if ( !have ) log.info( "optional", std::string( fn ) + ": not in this capture, section omitted" );
			}
			const std::string cap_name = redact( in.folder_name, p.identity );
			log.info( "capture", "folder <capture root>\\" + c_.capture_parent + "\\" + cap_name );

			// ACPI: byte-exact; checksums; identifier search (report only, never change)
			size_t acpi_valid = 0, acpi_na = 0, acpi_bad = 0, acpi_hits = 0;
			for ( auto& [ name, b ] : p.acpi )
			{
				bool facs = b.size() >= 4 && memcmp( b.data(), "FACS", 4 ) == 0;
				uint32_t len = b.size() >= 8 ? uint32_t( b[ 4 ] | b[ 5 ] << 8 | b[ 6 ] << 16 | uint32_t( b[ 7 ] ) << 24 ) : 0;
				uint8_t sum = 0;
				for ( uint8_t x : b ) sum = uint8_t( sum + x );
				if ( facs ) acpi_na++;
				else if ( len == b.size() && sum == 0 ) acpi_valid++;
				else acpi_bad++;
				std::string lc( b.begin(), b.end() );
				for ( char& x : lc ) x = char( std::tolower( uint8_t( x ) ) );
				for ( const std::string& n : p.needles )
					if ( size_t k = count_ci( lc, lower( n ) ) ) { acpi_hits += k; log.warning( "acpi", fmt( "identifier hit (reported, not changed): %s, a value of length %zu, %zux", name.c_str(), n.size(), k ) ); }
				for ( const Bytes& n : p.binary_needles )
					if ( size_t k = count_bin( b, n ) ) { acpi_hits += k; log.warning( "acpi", fmt( "identifier hit (reported, not changed): %s, a binary value of length %zu, %zux", name.c_str(), n.size(), k ) ); }
			}
			log.info( "acpi", fmt( "%zu tables byte-exact; checksum valid %zu, not applicable (FACS) %zu, not valid %zu; identifier search %zu real values x %zu tables: %zu hit(s)",
				p.acpi.size(), acpi_valid, acpi_na, acpi_bad, p.needles.size() + p.binary_needles.size(), p.acpi.size(), acpi_hits ) );

			// provenance (no absolute paths; the computer name withheld from folder names)
			std::vector<std::pair<std::string, std::string>> prov;
			std::string tool = in.tool_line.find( "fwid_probe" ) != std::string::npos ? in.tool_line.substr( in.tool_line.find( "fwid_probe" ) ) : "unknown";
			prov.emplace_back( "provenance.capture", c_.capture_parent + "\\" + cap_name + " (relative to the capture root; the computer-name part of the folder name is withheld)" );
			prov.emplace_back( "provenance.capture_tool", tool + " (capture manifest header)" );
			prov.emplace_back( "provenance.capture_manifest_sha256", in.manifest_sha256 );
			prov.emplace_back( "provenance.capture_check", fmt( "%zu of %zu manifest entries match (size + SHA-256); manifest SHA-256 = the pinned clean capture", in.manifest_entries, in.manifest_entries ) );
			int ci = 0;
			for ( auto& [ rel, sha ] : in.companions ) prov.emplace_back( fmt( "provenance.companion_%d", ++ci ), rel + " sha256 " + sha + " (frozen hash list of the same state; not consumed)" );
			prov.emplace_back( "provenance.peer_capture", c_.peer_capture_parent + "\\" + c_.peer_capture_folder + " manifest " + c_.peer_capture_manifest_sha256 + " (enum_registry.txt only: the shared USB serial table)" );
			prov.emplace_back( "build_config", std::string( BuildConfig::k_file ) + " (this folder, seed included; rebuild: emu-profilegen --config " + BuildConfig::k_file + ")" );
			prov.emplace_back( "rules", c_.rules.to_string() );
			prov.emplace_back( "optional_inputs", optional );
			if ( env == Environment::bare_host )
			{
				// the certificate stores are not part of the platform profile; only this fact is recorded (user decision 2026-10-16)
				bool test_cert = false;
				for ( const TextFile& f : in.texts )
					if ( f.name == "certificates.txt" )
						for ( const std::string& l : split_lines( f.data ) )
							if ( l.rfind( "subject:", 0 ) == 0 && lower( l ).find( "test" ) != std::string::npos ) test_cert = true;
				prov.emplace_back( "test_signing_certificate_present", test_cert ? "yes" : "no" );
			}
			prov.emplace_back( "smbios", fmt( "version %u.%u, %zu structures, table %u bytes; %zu fields replaced (fieldmap.tsv F rows), %zu bytes differ from the capture; CHID-relevant fields %zu, changed %zu",
				t0.major, t0.minor, t0.structures.size(), t0.length, p.smbios_fields, smbios_diff, cc.checked, cc.changed ) );
			prov.emplace_back( "edid", in.edid.empty() ? std::string( "not in this capture" ) : fmt( "%zu values (edid\\), %zu fields + %zu checksums (fieldmap.tsv D rows); extension blocks %zu, with a real serial %zu",
				p.edid_original.size(), p.edid_fields - p.edid_original.size(), p.edid_original.size(), p.edid_ext_blocks, p.edid_ext_hits ) );
			prov.emplace_back( "acpi", fmt( "%zu table files byte-exact (acpi\\); checksum valid %zu, not applicable (FACS) %zu, not valid %zu; identifier search: %zu hit(s)",
				p.acpi.size(), acpi_valid, acpi_na, acpi_bad, acpi_hits ) );
			prov.emplace_back( "identity", fmt( "%zu identifiers, %zu occurrence + relation rows (identity.tsv, fieldmap.tsv)", p.identity.size(), p.fields.size() - p.smbios_fields - p.edid_fields ) );

			// the BuildConfig copies: synthetic = the configuration as used (portable, seed included); original adds the resolved
			// capture root and folder name (local only)
			std::string cfg_syn = c_.save();
			std::string cfg_org = cfg_syn + "# resolved (LOCAL ONLY copy, ignored by load):\nresolved.capture_root   = " + ( resolved_root.empty() ? "-" : resolved_root ) +
								  "\nresolved.capture_folder = " + in.folder_name + "\n";
			res.synthetic = make_files( p, false, prov, cfg_syn );
			res.original = make_files( p, true, prov, cfg_org );

			// (e) leak guard: no real value in any synthetic file except the byte-exact ACPI copies (reported above)
			size_t embedded = 0;
			for ( auto& [ name, data ] : res.synthetic )
			{
				if ( name.rfind( "acpi\\", 0 ) == 0 ) continue;
				std::string lc( data.begin(), data.end() );
				for ( char& x : lc ) x = char( std::tolower( uint8_t( x ) ) );
				for ( const std::string& n : p.needles )
				{
					const std::string nl = lower( n );
					size_t tok = count_ci( lc, nl );
					embedded += count_raw( lc, nl ) - tok;
					if ( tok )
						fail( "leak guard: a real identifier value (length " + std::to_string( n.size() ) + ") is in the synthetic " + name +
							( c_.rules == BuildConfig::defaults( c_.env ).rules ? "" : " (a field rule is disabled)" ) );
				}
				for ( const Bytes& n : p.binary_needles )
					if ( count_bin( data, n ) ) fail( "leak guard: a real binary identifier is in the synthetic " + name );
			}
			log.info( "leak guard", fmt( "%zu real values (text forms + raw UUID / MAC bytes) x %zu synthetic files: 0 hits (%zu substring match(es) only inside a longer alphanumeric token - not the value)",
				p.needles.size() + p.binary_needles.size(), res.synthetic.size() - p.acpi.size(), embedded ) );
			// (f) the original profile's blobs are the capture's bytes
			if ( res.original.at( "smbios.bin" ) != in.rsmb ) fail( "self-check: the original smbios.bin differs from rsmb.bin" );
			res.ok = true;
		}
		catch ( const std::exception& e )
		{
			res.ok = false;
			res.error = e.what();
			log.error( "build", e.what() );
		}
		return res;
	}

	GenResult ProfileGenerator::run_build( bool check )
	{
		GenResult res;
		BuildLog pre;
		try
		{
			BuildConfig c = c_;
			const char* envn = env_dir_name( c.env );
			pre.info( "start", std::string( k_tool ) + "; environment " + envn + " (" + env_switch( c.env ) + ")" + ( check ? "; check mode (nothing is written)" : "" ) );
			const BuildConfig& pin = BuildConfig::pinned( c.env );
			const BuildConfig& peer = BuildConfig::pinned( c.env == Environment::vmware_guest ? Environment::bare_host : Environment::vmware_guest );
			if ( c.capture_manifest_sha256 != pin.capture_manifest_sha256 || c.companions != pin.companions || c.capture_parent != pin.capture_parent )
				fail( std::string( "the build config does not name the pinned clean capture of " ) + envn + " (--guest is built only from Run 3 without VmLoader, --host only from the clean host run)" );
			if ( c.peer_capture_parent != peer.capture_parent || c.peer_capture_folder != peer.capture_folder || c.peer_capture_manifest_sha256 != peer.capture_manifest_sha256 )
				fail( "the build config's peer capture is not the pinned clean capture of the other environment" );

			// repository root
			fs::path repo;
			if ( c.repo_root != "auto" ) repo = expand( c.repo_root );
			else
				for ( fs::path p = exe_dir();; p = p.parent_path() )
				{
					if ( fs::exists( p / "NoVmp.sln" ) && fs::exists( p / "Emulator" / "data" ) ) { repo = p; break; }
					if ( p.parent_path() == p || p.empty() ) break;
				}
			auto in_repo = [ & ]( const std::string& s ) -> fs::path {
				fs::path x = expand( s );
				if ( x.is_absolute() ) return x;
				if ( repo.empty() ) fail( "no repository found above the exe: give repo_root (--repo-root)" );
				return repo / x;
			};
			const fs::path out = in_repo( c.out ), seed_file = in_repo( c.seed_file );

			// capture root
			fs::path root;
			std::string root_from = "build config";
			if ( c.capture_root != "auto" ) root = expand( c.capture_root );
			else
			{
				std::string ev = env_var( "NOVMP_CAPTURE_ROOT" );
				if ( !ev.empty() ) { root = ev; root_from = "NOVMP_CAPTURE_ROOT"; }
				if ( root.empty() )
					for ( fs::path p = repo.empty() ? exe_dir() : repo;; p = p.parent_path() )
					{
						if ( fs::exists( p / "capture" / c.capture_parent ) ) { root = p / "capture"; root_from = "ancestor of the repository"; break; }
						if ( p.parent_path() == p || p.empty() ) break;
					}
				if ( root.empty() ) fail( "no capture root: set capture_root (--capture-root) or NOVMP_CAPTURE_ROOT to the folder that holds " + c.capture_parent );
			}
			pre.info( "paths", "capture root " + show( root ) + " (" + root_from + "); synthetic out " + show( out ) + "; seed store " + show( seed_file ) );
			fs::path orig;
			if ( c.original_out != "none" && !c.original_out.empty() )
			{
				fs::path x = expand( c.original_out );
				orig = x.is_absolute() ? x : root / x;
			}
			const fs::path cap = find_one( root / c.capture_parent, c.capture_folder, "capture" );
			CaptureInputs in = load_capture( cap, root, c, pre );
			if ( c.seed.empty() ) c.seed = load_seed( seed_file, !check, pre ).hex;
			else pre.info( "seed", "from the build config" );

			// the configuration as saved next to the outputs: portable (roots "auto", paths relative to the repository / capture
			// root), so the synthetic copy holds no user-specific absolute path; the seed travels with it
			BuildConfig cp = c;
			cp.tool = k_tool;
			cp.capture_root = "auto";
			cp.repo_root = "auto";
			cp.out = repo.empty() ? show( out ) : portable( out, repo );
			cp.seed_file = repo.empty() ? show( seed_file ) : portable( seed_file, repo );
			cp.original_out = orig.empty() ? "none" : portable( orig, root );
			if ( c.capture_root != "auto" ) pre.info( "paths", "the saved build config records capture_root = auto (the root given here is a local path)" );
			c_ = cp;

			res = gen_build( in, root.string() );
			res.log.entries.insert( res.log.entries.begin(), pre.entries.begin(), pre.entries.end() );
			pre.entries.clear();
			if ( !res.ok ) return res;
			BuildLog& log = res.log;
			res.out = out;
			// no user-profile path (it holds the account name) in any synthetic file
			{
				std::string u = lower( env_var( "USERPROFILE" ) );
				if ( u.size() > 3 )
					for ( auto& [ name, data ] : res.synthetic )
					{
						std::string lc( data.begin(), data.end() );
						for ( char& x : lc ) x = char( std::tolower( uint8_t( x ) ) );
						if ( lc.find( u ) != std::string::npos ) fail( "leak guard: the user-profile path is in the synthetic " + name );
					}
			}
			std::error_code ec;
			if ( check )
			{
				size_t bad = 0;
				for ( auto& [ name, data ] : res.synthetic )
				{
					std::error_code e2;
					if ( !fs::exists( out / name, e2 ) || read_file( out / name ) != data ) { log.error( "check", name + " differs from the regenerated file" ); bad++; }
				}
				if ( fs::exists( out, ec ) )
					for ( auto& e : fs::recursive_directory_iterator( out ) )
						if ( e.is_regular_file() && !res.synthetic.count( fs::relative( e.path(), out ).string() ) ) { log.error( "check", "extra file " + fs::relative( e.path(), out ).string() ); bad++; }
				if ( bad ) fail( fmt( "check: %zu file(s) of %s differ from the regeneration", bad, show( out ).c_str() ) );
				res.checked = true;
				log.info( "check", fmt( "%zu files of %s are byte-identical to the regeneration (same build config incl. seed, same capture)", res.synthetic.size(), show( out ).c_str() ) );
				return res;
			}
			if ( !orig.empty() )
			{
				if ( fs::weakly_canonical( fs::absolute( orig ) ) == fs::weakly_canonical( fs::absolute( out ) ) ) fail( "original_out must differ from out" );
				fs::path wt = check_git_work_tree( orig );
				if ( !wt.empty() ) fail( "refusing to write the ORIGINAL profile (real identifiers) inside a git work tree: " + show( orig ) + " is under " + show( wt ) );
			}
			save_profile( out, res.synthetic, false );
			log.info( "write", fmt( "SYNTHETIC profile %s (%zu files)", show( out ).c_str(), res.synthetic.size() ) );
			if ( !orig.empty() )
			{
				save_profile( orig, res.original, true );
				res.original_out = orig;
				log.info( "write", fmt( "ORIGINAL profile (LOCAL ONLY, real values) %s (%zu files)", show( orig ).c_str(), res.original.size() ) );
			}
			else log.info( "write", "original profile: not written (original_out = none)" );
			// the seed store follows the seed of the last build (a new identity is kept for the next default build)
			BuildLog sl;
			std::string stored;
			try { stored = fs::exists( seed_file, ec ) ? load_seed( seed_file, false, sl ).hex : ""; } catch ( const std::exception& ) {}
			if ( !ieq( stored, c_.seed ) )
			{
				Seed s;
				s.hex = c_.seed;
				save_seed( seed_file, s );
				log.info( "seed", "seed store " + show( seed_file ) + " updated to this build's seed (rebuild the other environment with it to keep the shared USB mapping)" );
			}
			return res;
		}
		catch ( const std::exception& e )
		{
			res.log.entries.insert( res.log.entries.begin(), pre.entries.begin(), pre.entries.end() );
			res.ok = false;
			res.error = e.what();
			res.log.error( "run", e.what() );
			return res;
		}
	}
}
