// profile_generator.cpp - ProfileGenerator (plan 1.J.49, ledger U1510-U1512, U1513-U1514). The field rules and the exact locations
// they touch in the clean captures are documented in profile_generator.hpp; each gen_ below repeats the locations it
// changes. Nothing here ever prints or writes a real identifier value outside the ORIGINAL profile: log lines, errors, the
// field map and the manifests carry locations, lengths and counts only.
// Per-build context (plan 1.J.49.a): every gen_ step takes the build's BuildContext (config, identity maps, used keys); the
// singleton's own state (default config, value pool, shared/ registry) is guarded by leaf locks - see profile_generator.hpp.
#include "profile_generator.hpp"
#include "pg_randomize.hpp"

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
	static const char* const k_tool = "emu-profilegen 1.2 (ProfileGenerator, ledger U1510-U1514)";

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
	// the SHA-256 algorithm providers, opened once (magic statics: thread-safe initialization; a CNG algorithm handle may be
	// shared by threads - each hash object below is local to its call)
	static BCRYPT_ALG_HANDLE open_alg( bool hmac )
	{
		BCRYPT_ALG_HANDLE h = nullptr;
		if ( !BCRYPT_SUCCESS( BCryptOpenAlgorithmProvider( &h, BCRYPT_SHA256_ALGORITHM, nullptr, hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0 ) ) )
			fail( "BCryptOpenAlgorithmProvider(SHA256) failed" );
		return h;
	}
	static BCRYPT_ALG_HANDLE alg( bool hmac )
	{
		if ( hmac )
		{
			static const BCRYPT_ALG_HANDLE h_hmac = open_alg( true );
			return h_hmac;
		}
		static const BCRYPT_ALG_HANDLE h_plain = open_alg( false );
		return h_plain;
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
	// ── the single write-point conversions (text value -> blob bytes) ──────────────────────────────────────────────────────
	Bytes bytes_of_hex( const char* hex )
	{
		const std::string h = hex ? hex : "";
		if ( h.size() % 2 || !std::all_of( h.begin(), h.end(), is_hex ) ) fail( "bytes_of_hex: an even number of hex digits expected" );
		Bytes b;
		for ( size_t i = 0; i < h.size(); i += 2 ) b.push_back( uint8_t( std::stoul( h.substr( i, 2 ), nullptr, 16 ) ) );
		return b;
	}
	uint32_t le32_of_decimal( const char* dec )
	{
		const std::string d = dec ? dec : "";
		if ( d.empty() || !std::all_of( d.begin(), d.end(), []( char c ) { return c >= '0' && c <= '9'; } ) || std::stoull( d ) > 0xFFFFFFFFull ) fail( "le32_of_decimal: a 32-bit decimal expected" );
		return uint32_t( std::stoull( d ) );
	}
	// a synthetic text in the letter case of the real one, position by position (the character classes already agree)
	static std::string follow_case( std::string s, const std::string& real )
	{
		for ( size_t i = 0; i < s.size() && i < real.size(); i++ )
		{
			if ( std::isupper( uint8_t( real[ i ] ) ) ) s[ i ] = char( std::toupper( uint8_t( s[ i ] ) ) );
			else if ( std::islower( uint8_t( real[ i ] ) ) ) s[ i ] = char( std::tolower( uint8_t( s[ i ] ) ) );
		}
		return s;
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
		// one repeated character (length >= 2): a placeholder, not an identifier (user decision J 2026-10-16: the Sabrent
		// bridge's repeated-digit disk serial, generalized)
		return s.size() >= 2 && std::all_of( s.begin(), s.end(), [ & ]( char c ) { return std::tolower( uint8_t( c ) ) == std::tolower( uint8_t( s[ 0 ] ) ); } );
	}
	bool ProfileGenerator::check_usb_placeholder( const std::string& s )
	{
		// all '0', or one repeated character (length >= 2) - the rule-2 filler generalization of decision J
		if ( !s.empty() && std::all_of( s.begin(), s.end(), []( char c ) { return c == '0'; } ) ) return true;
		if ( s.size() >= 2 && std::all_of( s.begin(), s.end(), [ & ]( char c ) { return std::tolower( uint8_t( c ) ) == std::tolower( uint8_t( s[ 0 ] ) ); } ) ) return true;
		return s.size() > 6 && ieq( s.substr( 0, 6 ), "MSFT30" ) && std::all_of( s.begin() + 6, s.end(), []( char c ) { return c == '0'; } );
	}

	// ── ProfileGenerator: the singleton (design: the user's rid demo - private constructor, get_instance, the -> handle) ──
	// It holds only the state shared by every build (plan 1.J.49.a): the default BuildConfig (config_mutex_), the interned
	// value pool (pool_mutex_) and the shared/ registry (shared_mutex_). The three locks are leaves: no function below holds
	// two of them at once, none is held across Randomize-> or file I/O. Everything else of a build is in its BuildContext.
	ProfileGenerator& ProfileGenerator::get_instance()
	{
		static ProfileGenerator instance;	// magic static: thread-safe one-time construction (C++11)
		return instance;
	}
	ProfileGenerator::ProfileGenerator() : default_config_( BuildConfig::defaults( Environment::vmware_guest ) ) {}
	void ProfileGenerator::reset()
	{
		{
			std::unique_lock lock( config_mutex_ );
			default_config_ = BuildConfig::defaults( Environment::vmware_guest );
		}
		reset_shared_values();	// the value pool is kept: every const char* handed out stays valid for the process lifetime
	}
	void ProfileGenerator::reset_shared_values()
	{
		std::lock_guard lock( shared_mutex_ );
		shared_.clear();
	}
	size_t ProfileGenerator::check_shared_values() const
	{
		std::lock_guard lock( shared_mutex_ );
		return shared_.size();
	}
	void ProfileGenerator::load_config( const BuildConfig& c )
	{
		std::unique_lock lock( config_mutex_ );
		default_config_ = c;
	}
	BuildConfig ProfileGenerator::config() const
	{
		std::shared_lock lock( config_mutex_ );
		return default_config_;
	}
	BuildContext ProfileGenerator::new_context() const { return new_context( config() ); }
	BuildContext ProfileGenerator::new_context( const BuildConfig& c ) const
	{
		BuildContext ctx;
		ctx.config = c;
		return ctx;
	}
	void ProfileGenerator::load_identity_maps( BuildContext& ctx, const IdentityMap& own, const IdentityMap& peer )
	{
		ctx.identity_map = own;
		ctx.peer_map = peer;
		ctx.applying = !own.entries.empty();
		ctx.used_keys.clear();
	}
	IdentityMap ProfileGenerator::gen_new_identity( BuildContext& ctx )
	{
		ctx.identity_map = IdentityMap();	// the next build draws every value fresh from RDRAND (Randomize->gen_...)
		ctx.applying = false;
		ctx.fresh = true;
		ctx.used_keys.clear();
		return ctx.identity_map;
	}
	const char* ProfileGenerator::keep( const std::string& value )
	{
		std::lock_guard lock( pool_mutex_ );
		return pool_.insert( value ).first->c_str();	// std::set nodes never move: the pointer stays valid
	}

	// the identity map: key -> the synthetic value. Generation records the Randomizer's value, application returns the saved
	// one; a "shared/" key may come from the other environment's map, else (generation) from the shared/ registry of this
	// process (one USB serial per physical device in both profiles, also when both generate concurrently). Every result is
	// interned: it stays valid when ctx is destroyed or its map is replaced (load_identity_maps / gen_new_identity)
	const char* ProfileGenerator::draw( BuildContext& ctx, const std::string& key, const char* kind, const std::string& real, bool same_length, const std::function<const char*()>& fn )
	{
		ctx.used_keys.insert( key );
		auto fits = [ & ]( const std::string& e_kind, const std::string& e_value ) {
			if ( e_kind != kind || ( same_length && e_value.size() != real.size() ) )
				fail( "the identity map's value for " + key + " does not fit the capture (kind / length) - generate a new identity (--new-identity)" );
			if ( ieq( e_value, real ) ) fail( "the identity map's value for " + key + " equals the captured value" );
		};
		IdentityMap& own = ctx.identity_map;
		if ( auto it = own.entries.find( key ); it != own.entries.end() ) { fits( it->second.kind, it->second.value ); return keep( it->second.value ); }
		const bool shared = key.rfind( "shared/", 0 ) == 0;
		if ( shared )
			if ( auto p = ctx.peer_map.entries.find( key ); p != ctx.peer_map.entries.end() )
			{
				fits( p->second.kind, p->second.value );
				own.entries[ key ] = p->second;
				return keep( p->second.value );
			}
		if ( ctx.applying ) fail( "the identity map has no value for " + key + ": it does not fit this capture - generate a new identity (--new-identity)" );
		if ( !shared )
		{
			const std::string v = fn();	// a finished value of the Randomizer (no lock held)
			own.entries[ key ] = IdentityMapEntry { kind, v };
			return keep( v );
		}
		// a shared/ key while generating: the registry's value when another build of this process recorded one, else this
		// build's draw (drawn and interned without a lock, then registered insert-if-absent: the first writer wins)
		SharedValue sv;
		bool found = false;
		{
			std::lock_guard lock( shared_mutex_ );
			if ( auto r = shared_.find( key ); r != shared_.end() ) { sv = r->second; found = true; }
		}
		if ( !found )
		{
			const char* drawn = keep( fn() );
			std::lock_guard lock( shared_mutex_ );
			sv = shared_.try_emplace( key, SharedValue { kind, drawn } ).first->second;
		}
		fits( sv.kind, sv.value );
		own.entries[ key ] = IdentityMapEntry { sv.kind, sv.value };
		return sv.value;
	}

	// rule 1 (see the header for the exact byte ranges in both captures): the Randomizer's 32 hex digits with VMware's layout
	// (guest: bytes 0-1 = 56h 4Dh kept) or the RFC 4122 version / variant bits of the real UUID (bare host)
	SmbiosUuid ProfileGenerator::gen_system_uuid( BuildContext& ctx, const SmbiosUuid& real )
	{
		SmbiosUuid out = real;
		const bool vmware = ctx.config.env == Environment::vmware_guest;
		out.hex = draw( ctx, fmt( "smbios/t1/h%04X/uuid", real.handle ), "uuid", real.hex, true, [ & ] { return Randomize->gen_system_uuid_like( real.hex, vmware ); } );
		return out;
	}

	// rules 2 / 2h: rule-2 classes on one SMBIOS string, key "smbios/t<type>/h<handle>/o<offset>"
	SmbiosStringField ProfileGenerator::gen_smbios_string( BuildContext& ctx, const SmbiosStringField& real )
	{
		SmbiosStringField out = real;
		out.value = draw( ctx, fmt( "smbios/t%u/h%04X/o%02X", real.type, real.handle, real.offset ), "smbios_string", real.value, true, [ & ] { return Randomize->gen_like( real.value ); } );
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
	SmbiosGen ProfileGenerator::gen_smbios( BuildContext& ctx, const SmbiosInput& in )
	{
		const Bytes& raw = in.raw;
		const SmbiosTable t = parse_smbios( raw );
		SmbiosGen g;
		g.synthetic = raw;
		Bytes& out = g.synthetic;
		const RuleSet& rules = ctx.config.rules;
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
					const std::string real_hex = hex_of( real, 16, false );
					SmbiosUuid ru;
					ru.handle = s.handle;
					ru.hex = real_hex.c_str();
					SmbiosUuid su = gen_system_uuid( ctx, ru );
					Bytes ub = bytes_of_hex( su.hex );	// the single write point: text -> the 16 raw bytes
					// UUID-MAC relation: the real UUID's last 6 bytes = an adapter MAC of the capture -> the synthetic UUID's last 6
					// bytes = that adapter's synthetic MAC. Bare host: bytes 10-15 (smbios.bin 0x006D..0x0072) = the onboard wired
					// NIC's MAC. Run 3 guest: no relation.
					std::string tied;
					for ( const MacPair& m : in.macs )
					{
						uint8_t rm[ 6 ], sm[ 6 ];
						if ( !parse_mac_text( m.real.text, rm ) || !parse_mac_text( m.synthetic.text, sm ) || memcmp( real + 10, rm, 6 ) != 0 ) continue;
						memcpy( ub.data() + 10, sm, 6 );
						su.tail_is_mac = true;
						su.tail_adapter = m.real.adapter;
						tied = "; bytes 10-15 = the synthetic MAC of identity " + m.real.id + " (adapter \"" + m.real.adapter + "\"), the UUID-MAC relation of the capture kept";
						break;
					}
					std::copy( ub.begin(), ub.end(), out.begin() + s.offset + 8 );
					g.fields.push_back( { "smbios.bin", next_id(), ctx.config.env == Environment::vmware_guest ? "1-vmware" : "1-rfc4122",
						fmt( "type 1 handle 0x%04X formatted offset 0x08: system UUID (%s%s)", s.handle,
							ctx.config.env == Environment::vmware_guest ? "bytes 0-1 VMware prefix kept" : "version nibbles of bytes 6/7 and the variant bits of byte 8 kept", tied.c_str() ),
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
			if ( ctx.config.env == Environment::bare_host )
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
				if ( it == mapped.end() )
				{
					it = mapped.emplace( v, gen_smbios_string( ctx, { s.type, f.off, s.handle, v.c_str() } ).value ).first;
					g.strings.emplace_back( v, it->second );
				}
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

	// ── single-value generators: each asks the Randomizer (Randomize->gen_...) for a finished value through draw() - the
	//    identity map records it (generation) or returns the saved one (application); none draws randomness itself ────────
	// rule 3: one base MAC - OUI (bytes 0-2) exact, bytes 3-5 synthetic with room for max_delta; key "identity/<id>"
	MacAddress ProfileGenerator::gen_mac( BuildContext& ctx, const MacAddress& real ) { return gen_mac_with_room( ctx, real, 0 ); }
	MacAddress ProfileGenerator::gen_mac_with_room( BuildContext& ctx, const MacAddress& real, unsigned max_delta )
	{
		uint8_t m[ 6 ];
		if ( !real.text || !parse_mac_text( real.text, m ) ) fail( "rule 3: not a MAC in XX:XX:XX:XX:XX:XX form" );
		MacAddress out = real;
		out.text = draw( ctx, "identity/" + real.id, "mac", real.text, true, [ & ] { return Randomize->gen_mac_like( real.text, max_delta ); } );
		if ( ( mac_value( out.text ) & 0xFFFFFF ) + max_delta > 0xFFFFFF ) fail( "rule 3: the identity map's MAC of " + real.id + " leaves no room for its family" );
		return out;
	}

	// rule 3: the MAC family of one capture. Detects the relations generically - the locally administered variant (byte 0 bit
	// 1 toggled) of another MAC, or + delta (<= 16) within the same OUI from the lowest MAC (the base) - takes one synthetic
	// MAC per base (its tail leaves room for the largest delta) and re-derives every dependent with the same relation
	MacFamily ProfileGenerator::gen_mac_family( BuildContext& ctx, const MacFamily& real )
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
			syn[ i ] = mac_value( gen_mac_with_room( ctx, m[ i ].mac, unsigned( maxd ) ).text );
		}
		for ( size_t i : universal ) if ( m[ i ].parent >= 0 ) syn[ i ] = syn[ m[ i ].parent ] + uint64_t( m[ i ].delta );
		for ( size_t i = 0; i < m.size(); i++ ) if ( m[ i ].locally_administered ) syn[ i ] = syn[ m[ i ].parent ] ^ ( uint64_t( 2 ) << 40 );
		for ( size_t i = 0; i < m.size(); i++ )
		{
			if ( ieq( mac_text( syn[ i ] ), real.members[ i ].mac.text ) ) fail( "rule 3: a derived MAC equals its real value" );
			m[ i ].mac.text = keep( mac_text( syn[ i ] ) );	// derived (a base value or a relation of one): no randomness here
		}
		return f;
	}

	// rule 3: the EUI-64 form of a NIC's MAC in an instance ID, re-derived from the NIC's synthetic MAC (the I226-V's PCI
	// instance ID on the bare host: OUI + FFFF + NIC part + 00)
	Eui64Id ProfileGenerator::gen_eui64_instance_id( BuildContext& ctx, const Eui64Id& real, const MacAddress& nic )
	{
		std::string sp = fmt( "%012llX", (unsigned long long)mac_value( nic.text ) );
		std::string t = sp.substr( 0, 6 ) + ( real.ffff ? "FFFF" : "FFFE" ) + sp.substr( 6 ) + ( real.ffff ? "00" : "" );
		if ( has_lower_hex( real.text ) ) t = lower( t );
		Eui64Id out = real;
		out.text = keep( t );
		return out;
	}

	// rule 4c: GUID text, braces / hyphens / case kept; the version digit and the variant bits kept; all other digits
	// synthetic (Randomize->gen_guid_like). Version-1 (time-based) GUIDs: when the node (last 12 hex digits) is an adapter
	// MAC, the node becomes that adapter's synthetic MAC; Windows' no-NIC node 806E6F6E6963 stays exact. MBR-style volume
	// GUIDs {SSSSSSSS-0000-0000-0000-OFFSET}: only the disk signature (the first 8 digits) is synthetic.
	GuidV1 ProfileGenerator::parse_guid_v1( const char* text )
	{
		GuidV1 v;
		static const std::regex k_guid( R"(\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?)" );
		const std::string g = text ? text : "";
		if ( !std::regex_match( g, k_guid ) ) return v;
		std::string h;
		for ( char c : g ) if ( is_hex( c ) ) h += c;
		const unsigned variant = unsigned( std::stoul( h.substr( 16, 1 ), nullptr, 16 ) );
		const bool mbr = h.substr( 8, 12 ).find_first_not_of( '0' ) == std::string::npos;
		if ( mbr || h[ 12 ] != '1' || variant < 8 || variant >= 12 ) return v;	// version 1 and the RFC 4122 variant (10xx) only
		v.timestamp = ( std::stoull( h.substr( 13, 3 ), nullptr, 16 ) << 48 ) | ( std::stoull( h.substr( 8, 4 ), nullptr, 16 ) << 32 ) | std::stoull( h.substr( 0, 8 ), nullptr, 16 );
		v.clock_seq = h.substr( 16, 4 );
		v.node = h.substr( 20, 12 );
		v.ok = true;
		return v;
	}

	// rule 4c (see above); RFC 4122 version-1 GUIDs (U1516): the 60-bit timestamp = the real one + the install-history offset
	// (rule 7b, in 100-ns units; ctx's identity map holds the offset once gen_install_time ran), the clock sequence synthetic
	// (Randomize->gen_clock_seq_like, variant bits kept), the node = the adapter's synthetic MAC / Windows' no-NIC node kept /
	// any other node synthetic. Generation composes that value; application checks the saved one against the same rule
	GuidValue ProfileGenerator::gen_guid( BuildContext& ctx, const GuidValue& real, const NodeMap& nodes )
	{
		const std::string g = real.text ? real.text : "";
		std::string node;
		for ( char c : g ) if ( is_hex( c ) ) node += char( std::tolower( uint8_t( c ) ) );
		const char* syn_node = nullptr;
		std::string sn;
		if ( node.size() == 32 )
			if ( auto it = nodes.real_to_synthetic.find( node.substr( 20 ) ); it != nodes.real_to_synthetic.end() ) { sn = it->second; syn_node = sn.c_str(); }
		const GuidV1 rv = parse_guid_v1( g.c_str() );
		if ( !rv.ok ) return { draw( ctx, real.label, "guid", g, true, [ & ] { return Randomize->gen_guid_like( g.c_str(), syn_node ); } ), real.label };

		// version 1: the timestamp is derived (never drawn)
		if ( !ctx.identity_map.has_install_shift )
			fail( "rule 4c: the version-1 GUID " + real.label + " needs the install-history offset (rule 7b: os_ids.txt InstallDate) - this capture has none" );
		const int64_t ts = int64_t( rv.timestamp ) + ctx.identity_map.install_shift * 10000000LL;
		if ( ts < 0 || ts >= int64_t( 1 ) << 60 ) fail( "rule 4c: the shifted timestamp of the version-1 GUID " + real.label + " leaves the 60-bit range" );
		const std::string want_node = syn_node ? lower( sn ) : lower( rv.node ) == "806e6f6e6963" ? std::string( "806e6f6e6963" ) : std::string();	// empty: any synthetic node
		const bool up = !has_lower_hex( g );
		// the GUID text in the real one's style (braces, hyphens, case) from its three parts
		auto compose = [ & ]( uint64_t t, const std::string& clock, const std::string& nd ) {
			std::string hex = fmt( "%08llx%04llx%04llx", (unsigned long long)( t & 0xFFFFFFFFull ), (unsigned long long)( t >> 32 & 0xFFFF ), (unsigned long long)( 0x1000 | ( t >> 48 & 0x0FFF ) ) ) + lower( clock ) + lower( nd );
			if ( up ) hex = upper( hex );
			std::string o = g;
			size_t k = 0;
			for ( char& c : o ) if ( is_hex( c ) ) c = hex[ k++ ];
			return o;
		};
		const char* v = draw( ctx, real.label, "guid", g, true, [ & ] {
			const std::string clock = Randomize->gen_clock_seq_like( rv.clock_seq.c_str() );
			const std::string nd = want_node.empty() ? std::string( Randomize->gen_hex_like( rv.node.c_str(), false ) ) : want_node;
			return keep( compose( uint64_t( ts ), clock, nd ) );
		} );
		// the value (drawn now or saved) must follow the rule: version 1, the real variant class, the derived timestamp, the
		// derived node, the real one's style
		const GuidV1 sv = parse_guid_v1( v );
		auto variant_class = []( const std::string& c ) { const unsigned x = unsigned( std::stoul( c.substr( 0, 1 ), nullptr, 16 ) ); return x < 8 ? 0u : x < 12 ? 1u : x < 14 ? 2u : 3u; };
		const bool style = std::string( v ).size() == g.size() && compose( sv.ok ? sv.timestamp : 0, sv.ok ? sv.clock_seq : "0000", sv.ok ? sv.node : "000000000000" ) == v;
		const bool node_ok = sv.ok && ( want_node.empty() || lower( sv.node ) == want_node );
		const bool shape_ok = sv.ok && style && node_ok && variant_class( sv.clock_seq ) == variant_class( rv.clock_seq );
		if ( shape_ok && sv.timestamp == uint64_t( ts ) ) return { v, real.label };
		if ( !shape_ok || !ctx.applying || !ctx.upgrade_v1_time )
			fail( "the identity map's version-1 GUID for " + real.label + " does not fit the rule (version 1, variant, node, timestamp = real + the install-history offset)" +
				( shape_ok ? " - a map written before U1516: apply it once with --upgrade-v1-time, or generate a new identity (--new-identity)" : " - generate a new identity (--new-identity)" ) );
		// the upgrade of a pre-U1516 map: the derived timestamp, the saved clock sequence and node
		const std::string up_v = compose( uint64_t( ts ), sv.clock_seq, sv.node );
		if ( ieq( up_v, g ) ) fail( "rule 4c: the upgraded version-1 GUID " + real.label + " equals the captured value" );
		ctx.identity_map.entries[ real.label ].value = up_v;
		ctx.upgraded_keys.push_back( real.label );
		return { keep( up_v ), real.label };
	}

	// rule 4d: a USB / HID device serial (Randomize->gen_usb_serial_like: hex-encoded ASCII stays hex-encoded ASCII, other hex
	// stays hex with its digit classes, anything else rule-2 classes). table_index >= 0: key "shared/usb/<index>" - the
	// same value in both profiles; else this environment's key
	UsbSerial ProfileGenerator::gen_usb_serial( BuildContext& ctx, const UsbSerial& real )
	{
		UsbSerial out = real;
		const std::string key = real.table_index >= 0 ? fmt( "shared/usb/%ld", real.table_index ) : real.label;
		out.text = draw( ctx, key, "usb_serial", real.text, true, [ & ] { return Randomize->gen_usb_serial_like( real.text ); } );
		return out;
	}

	// rule 4a: "DESKTOP-" + 7 synthetic characters [A-Z0-9]
	ComputerName ProfileGenerator::gen_computer_name( BuildContext& ctx, const ComputerName& real )
	{
		return { draw( ctx, real.label, "computer_name", real.text, false, [ & ] { return Randomize->gen_computer_name( real.text ); } ), real.label };
	}

	TextValue ProfileGenerator::gen_class_string( BuildContext& ctx, const TextValue& real )
	{
		return { draw( ctx, real.label, "classes", real.text, true, [ & ] { return Randomize->gen_like( real.text ); } ), real.label };
	}
	TextValue ProfileGenerator::gen_hex_token( BuildContext& ctx, const TextValue& real )
	{
		return { draw( ctx, real.label, "hex", real.text, true, [ & ] { return Randomize->gen_hex_like( real.text, false ); } ), real.label };
	}

	// rule 8a: the installation ID - 63 digits = 9 groups of six random digits + their check digit (d0 + 2 d1 + d2 + 2 d3 +
	// d4 + 2 d5) mod 7 (user decision 2026-10-17, the demo's rule; checked on all 7 real IDs of the clean captures), the
	// real one's separators (none in fwid 1.4) kept
	WinInstallationId ProfileGenerator::gen_win_installation_id( BuildContext& ctx, const WinInstallationId& real )
	{
		WinInstallationId out = real;
		out.text = draw( ctx, real.label, "installation_id", real.text, true, [ & ] { return Randomize->gen_installation_id_like( real.text ); } );
		if ( !check_installation_id( out.text ) ) fail( "rule 8a: the identity map's installation ID of " + real.label + " fails its check digits" );
		out.group_lengths = std::vector<size_t>( 9, 7 );
		out.check_digits_modelled = true;
		return out;
	}
	bool ProfileGenerator::check_installation_id( const char* text )
	{
		std::string d;
		for ( const char* p = text; p && *p; p++ ) if ( *p >= '0' && *p <= '9' ) d += *p;
		if ( d.size() != 63 ) return false;
		for ( size_t g = 0; g < 63; g += 7 )
			if ( Randomizer::check_digit( d.substr( g, 6 ).c_str() ) != d[ g + 6 ] ) return false;
		return true;
	}

	// rule 7a: ProductId "ddddd-ddddd-ddddd-AAddd": the first group (product / edition) and the last group (channel) exact,
	// the middle groups rule-2 classes
	ProductId ProfileGenerator::gen_product_id( BuildContext& ctx, const ProductId& real )
	{
		const std::string t = real.text;
		const size_t a = t.find( '-' ), b = t.rfind( '-' );
		if ( a == std::string::npos || a == b ) fail( "rule 7a: a ProductId without middle groups" );
		const std::string mid = t.substr( a + 1, b - a - 1 );
		const char* m = draw( ctx, real.label, "product_id_middle", mid, true, [ & ] { return Randomize->gen_like( mid.c_str() ); } );
		return { keep( t.substr( 0, a + 1 ) + m + t.substr( b ) ), real.label };
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
	// rule 7b: ONE offset for the whole install history (user decision 2026-10-16: shift all together), whole seconds.
	// Generation: Randomize->gen_decimal_in_range over the window; the map key "identity/install" records the synthetic InstallDate
	// (the repository copy: no offset), the original copy also the offset. Application: offset = the saved synthetic
	// InstallDate - the real one. Every shifted event stays strictly after the build release and strictly before the capture;
	// the latest event (a device's last arrival, minutes before the capture) leaves no room forward, so the offset is at least
	// one day earlier. InstallDate and InstallTime move by the same offset (their real sub-second relation kept).
	InstallTime ProfileGenerator::gen_install_time( BuildContext& ctx, const InstallTime& real )
	{
		const InstallWindow w = parse_install_window( real.os_build, real.capture_utc );
		const int64_t first = real.earliest_event ? std::min( real.unix_seconds, real.earliest_event ) : real.unix_seconds;
		const int64_t last = std::max( real.unix_seconds, real.latest_event );
		const int64_t lo = w.lo - first + 1, hi = std::min<int64_t>( -86400, w.hi - last - 1 );
		if ( first <= w.lo || last >= w.hi ) fail( "rule 7b: an install-history event of the capture lies outside the build release .. capture window" );
		if ( hi < lo ) fail( "rule 7b: no room to shift the install history by at least one day inside the build release .. capture window" );
		const std::string real_text = std::to_string( real.unix_seconds );
		const char* s = draw( ctx, "identity/install", "install", real_text, false, [ & ] {
			return keep( std::to_string( real.unix_seconds + std::stoll( Randomize->gen_decimal_in_range( lo, hi ) ) ) );
		} );
		InstallTime out = real;
		out.shift_seconds = std::stoll( s ) - real.unix_seconds;
		if ( out.shift_seconds < lo || out.shift_seconds > hi ) fail( "rule 7b: the identity map's install instant does not fit this capture's window" );
		out.unix_seconds = real.unix_seconds + out.shift_seconds;
		out.filetime = uint64_t( int64_t( real.filetime ) + out.shift_seconds * 10000000LL );
		ctx.identity_map.has_install_shift = true;	// written only into the ORIGINAL (local) copy of the map
		ctx.identity_map.install_shift = out.shift_seconds;
		return out;
	}

	// ISO 8601 "YYYY-MM-DDTHH:MM:SSZ" <-> unix seconds (proleptic Gregorian, UTC)
	static int64_t days_from_civil( int y, unsigned m, unsigned d )
	{
		y -= m <= 2;
		const int era = ( y >= 0 ? y : y - 399 ) / 400;
		const unsigned yoe = unsigned( y - era * 400 );
		const unsigned doy = ( 153 * ( m > 2 ? m - 3 : m + 9 ) + 2 ) / 5 + d - 1;
		const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return int64_t( era ) * 146097 + int64_t( doe ) - 719468;
	}
	static bool unix_of_iso( const std::string& iso, int64_t& out )
	{
		int y = 0;
		unsigned mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
		if ( iso.size() != 20 || sscanf( iso.c_str(), "%d-%u-%uT%u:%u:%uZ", &y, &mo, &d, &hh, &mi, &ss ) != 6 ) return false;
		out = days_from_civil( y, mo, d ) * 86400 + hh * 3600 + mi * 60 + ss;
		return true;
	}
	static std::string iso_of_unix( int64_t t )
	{
		int64_t z = ( t >= 0 ? t : t - 86399 ) / 86400, sec = t - z * 86400;
		z += 719468;
		const int64_t era = ( z >= 0 ? z : z - 146096 ) / 146097;
		const unsigned doe = unsigned( z - era * 146097 );
		const unsigned yoe = ( doe - doe / 1460 + doe / 36524 - doe / 146096 ) / 365;
		const unsigned doy = doe - ( 365 * yoe + yoe / 4 - yoe / 100 ), mp = ( 5 * doy + 2 ) / 153;
		const unsigned d = doy - ( 153 * mp + 2 ) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
		const int64_t y = int64_t( yoe ) + era * 400 + ( m <= 2 );
		return fmt( "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ", (long long)y, m, d, (long long)( sec / 3600 ), (long long)( sec / 60 % 60 ), (long long)( sec % 60 ) );
	}

	// rule 7d: the machine / account-domain SID prefix "S-1-5-21-a-b-c": structure kept; the three sub-authorities synthetic,
	// each a decimal of the same digit count (no leading zero, at most 4294967295)
	SidParts ProfileGenerator::gen_sid( BuildContext& ctx, const SidParts& real )
	{
		return { draw( ctx, real.label, "sid", real.prefix, true, [ & ] { return Randomize->gen_sid_like( real.prefix ); } ), real.label };
	}

	// rule 7e: a volume serial "XXXX-XXXX": 8 synthetic hex digits in the original's case, the form kept
	VolumeId ProfileGenerator::gen_volume_serial( BuildContext& ctx, const VolumeId& real )
	{
		return { draw( ctx, real.label, "volume_serial", real.serial, true, [ & ] { return Randomize->gen_hex_like( real.serial, false ); } ), real.label };
	}

	// rule 6: the EDID numeric serial (bytes 12-15, little-endian) as its decimal value. Four printable letters / digits (some
	// vendors store text there) get the rule-2 classes; any other value a synthetic number of the same decimal digit count
	EdidSerial ProfileGenerator::gen_edid_serial( BuildContext& ctx, const EdidSerial& real )
	{
		return { draw( ctx, real.label, "edid_serial", real.text, false, [ & ] { return Randomize->gen_edid_serial_like( real.text ); } ), real.label };
	}
	// rule 6: byte 127 = the value that makes the 128-byte base block sum to 0 (mod 256)
	EdidBlock ProfileGenerator::gen_edid_checksum( BuildContext& ctx, const EdidBlock& block )
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
	EdidGen ProfileGenerator::gen_edid( BuildContext& ctx, const EdidInput& in )
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
					const std::string dec = std::to_string( serial );
					const uint32_t syn = le32_of_decimal( gen_edid_serial( ctx, { dec.c_str(), fmt( "edid/n%02d", ++nnum ) } ).text );	// the single write point
					it = num.emplace( model + fmt( "%08X", serial ), syn ).first;
					g.presets.push_back( { "edid_serial", "6", fmt( "0x%08X", serial ), fmt( "0x%08X", it->second ), { lower( fmt( "0x%08X", serial ) ), std::to_string( serial ) }, "edid.txt" } );
				}
				real_forms.push_back( Bytes( raw.begin() + 12, raw.begin() + 16 ) );
				if ( ctx.config.rules.r6 )
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
					it = str.emplace( model + s, gen_class_string( ctx, { s.c_str(), fmt( "edid/s%02d", ++nstr ) } ).text ).first;
					g.presets.push_back( { "edid_serial_string", "6", s, it->second, { lower( s ) }, "edid.txt" } );
				}
				real_forms.push_back( to_bytes( s ) );
				g.needles.push_back( s );
				if ( ctx.config.rules.r6 )
				{
					memcpy( &b.bytes[ d + 5 ], it->second.data(), len );
					g.fields.push_back( { area, fmt( "D%02d", ++row ), "6", fmt( "EDID descriptor at offset %zu, tag FFh: monitor serial string", d ), d + 5, 0, uint32_t( len ) } );
				}
			}
			b = gen_edid_checksum( ctx, b );
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

	// ── rule 8d: the raw STORAGE_DEVICE_DESCRIPTORs (disk_<n>_descriptor.bin, fwid >= 1.4) ───────────────────────────────
	// The serial string at SerialNumberOffset (offset 24; NUL-terminated ASCII), without its blank padding, becomes its disk-
	// serial identity (disks.txt, or the USB serial it equals); a placeholder (one repeated character) and a descriptor without
	// a serial (offset 0: the VMware guest's virtual disks) stay exact, like vendor / product / revision and every other byte.
	// The real serial anywhere else in the descriptor (raw properties) stops the run.
	DiskGen ProfileGenerator::gen_disk_descriptors( BuildContext& ctx, const DiskInput& in )
	{
		DiskGen g;
		int row = 0;
		auto dword = []( const Bytes& b, size_t o ) { return uint32_t( b[ o ] ) | uint32_t( b[ o + 1 ] ) << 8 | uint32_t( b[ o + 2 ] ) << 16 | uint32_t( b[ o + 3 ] ) << 24; };
		for ( const DiskDescriptor& d : in.descriptors )
		{
			DiskDescriptor s = d;
			const Bytes& b = d.bytes;
			if ( b.size() < 36 ) fail( d.name + ": shorter than a STORAGE_DEVICE_DESCRIPTOR" );
			const size_t size = std::min<size_t>( dword( b, 4 ), b.size() ), so = dword( b, 24 );
			if ( !so ) { g.none++; g.synthetic.push_back( s ); continue; }
			if ( so < 36 || so >= size ) fail( fmt( "%s: SerialNumberOffset %zu outside the descriptor", d.name.c_str(), so ) );
			size_t e = so;
			while ( e < size && b[ e ] ) e++;
			size_t a = so, z = e;
			while ( a < z && b[ a ] == ' ' ) a++;
			while ( z > a && b[ z - 1 ] == ' ' ) z--;
			const std::string core( b.begin() + a, b.begin() + z );
			if ( check_filler( core ) ) { g.filler++; g.synthetic.push_back( s ); continue; }
			const IdentityEntry* id = nullptr;
			for ( const IdentityEntry& r : in.identity )
				if ( ( r.kind == "disk_serial" || r.kind == "usb_serial" ) && ieq( r.original, core ) ) { id = &r; break; }
			if ( !id ) fail( d.name + ": its serial has no disk-serial identity (disks.txt) - it needs a field decision" );
			g.needles.push_back( core );
			const Bytes cb( core.begin(), core.end() );
			for ( auto it = std::search( b.begin(), b.end(), cb.begin(), cb.end() ); it != b.end(); it = std::search( it + 1, b.end(), cb.begin(), cb.end() ) )
				if ( size_t( it - b.begin() ) != a ) fail( d.name + ": the disk serial also occurs outside the serial string - it needs a field decision" );
			if ( ctx.config.rules.r8 )
			{
				const std::string syn = follow_case( id->synthetic, core );
				memcpy( &s.bytes[ a ], syn.data(), core.size() );
				g.fields.push_back( { "disk\\" + d.name, fmt( "R%02d", ++row ), "8d", fmt( "STORAGE_DEVICE_DESCRIPTOR serial number string (SerialNumberOffset %zu%s): identity %s", so,
					a != so || z != e ? ", blank padding kept" : "", id->id.c_str() ), a, 0, uint32_t( core.size() ) } );
			}
			g.serials++;
			g.synthetic.push_back( s );
		}
		return g;
	}

	// ── rule 8c: SusClientIdValidation ─────────────────────────────────────────────────────────────────────────────────────
	// The layout of both clean fwid 1.4 captures (Run3b guest: 29 bytes; bare host: 117 bytes): 06h 02h len1 01h len2 len3,
	// string 1 (UTF-16LE, len1 bytes), 06h + 6 MAC bytes, string 2 (len2), string 3 (len3); the lengths add up to the blob.
	// The meaning of the components comes from the captures (string 1 = the system disk's serial of disks.txt, the MAC = an
	// adapter MAC, string 2 = the SMBIOS baseboard serial, string 3 = SMBIOS filler); no local document describes the value.
	SusValidationLayout ProfileGenerator::parse_sus_client_id_validation( const Bytes& b )
	{
		SusValidationLayout l;
		if ( b.size() < 13 || b[ 0 ] != 0x06 || b[ 1 ] != 0x02 || b[ 3 ] != 0x01 ) return l;
		const size_t n1 = b[ 2 ], n2 = b[ 4 ], n3 = b[ 5 ];
		if ( n1 % 2 || n2 % 2 || n3 % 2 || 6 + n1 + 7 + n2 + n3 != b.size() || b[ 6 + n1 ] != 6 ) return l;
		auto u16 = [ & ]( size_t o, size_t n, std::string& out ) {
			for ( size_t i = 0; i < n; i += 2 )
			{
				if ( b[ o + i + 1 ] || b[ o + i ] < 0x20 || b[ o + i ] > 0x7E ) return false;
				out += char( b[ o + i ] );
			}
			return true;
		};
		if ( !u16( 6, n1, l.disk_serial ) ) return l;
		l.mac_offset = 6 + n1 + 1;
		std::copy( b.begin() + l.mac_offset, b.begin() + l.mac_offset + 6, l.mac.begin() );
		if ( !u16( l.mac_offset + 6, n2, l.board_serial ) || !u16( l.mac_offset + 6 + n2, n3, l.other_serial ) ) return l;
		l.ok = true;
		return l;
	}
	SusValidation ProfileGenerator::gen_sus_client_id_validation( BuildContext& ctx, const SusValidationInput& in )
	{
		SusValidation out;
		out.layout = parse_sus_client_id_validation( in.real );
		if ( !out.layout.ok ) fail( "rule 8c: SusClientIdValidation has another layout than the clean captures - it needs a field decision" );
		Bytes bytes = in.real;	// assembled from the components' synthetic texts (no randomness of its own)
		auto put = [ & ]( size_t off, const std::string& real, const char* what ) {
			if ( real.empty() || check_filler( real ) ) { out.components.push_back( std::string( what ) + ": empty / filler, exact" ); return; }
			const std::string* syn = nullptr;
			for ( auto& [ r, s ] : in.strings ) if ( ieq( r, real ) ) { syn = &s; break; }
			if ( !syn ) fail( std::string( "rule 8c: SusClientIdValidation " ) + what + " is no identifier of the capture - it needs a field decision" );
			const std::string s2 = follow_case( *syn, real );
			for ( size_t i = 0; i < s2.size(); i++ ) bytes[ off + 2 * i ] = uint8_t( s2[ i ] );
			out.components.push_back( std::string( what ) + ": its synthetic identifier" );
		};
		const SusValidationLayout& l = out.layout;
		put( 6, l.disk_serial, "string 1 (the system disk serial)" );
		std::string mt;
		for ( int i = 0; i < 6; i++ ) mt += ( i ? ":" : "" ) + hex_of( &l.mac[ i ], 1, true );
		bool mac = false;
		for ( const MacPair& m : in.macs )
		{
			uint8_t sm[ 6 ];
			if ( !ieq( m.real.text, mt ) || !parse_mac_text( m.synthetic.text, sm ) ) continue;
			memcpy( &bytes[ l.mac_offset ], sm, 6 );
			out.components.push_back( "MAC: the synthetic MAC of identity " + m.real.id + " (adapter \"" + m.real.adapter + "\")" );
			mac = true;
			break;
		}
		if ( !mac ) fail( "rule 8c: the MAC inside SusClientIdValidation is no adapter MAC of the capture - it needs a field decision" );
		put( l.mac_offset + 6, l.board_serial, "string 2 (the SMBIOS baseboard serial)" );
		put( l.mac_offset + 6 + 2 * l.board_serial.size(), l.other_serial, "string 3" );
		if ( bytes == in.real ) out.components.push_back( "no component is an identifier: exact" );
		out.hex = keep( hex_of( bytes.data(), bytes.size(), true ) );
		return out;
	}

	// ── rule 8a: license.txt rows (fwid 1.4: "=== SoftwareLicensingProduct row N", then "Key  value" lines) ──────────────
	std::vector<WinInstallationId> ProfileGenerator::parse_license_rows( const TextFile& f )
	{
		std::vector<WinInstallationId> rows;
		WinLicenseInfo lic;
		static const std::regex r_kv( R"re(^([A-Za-z]+)\s+(.*)$)re" );
		const auto lines = split_lines( f.data );
		for ( size_t i = 0; i < lines.size(); i++ )
		{
			const std::string& l = lines[ i ];
			if ( l.empty() || l[ 0 ] == '#' ) continue;
			if ( l.rfind( "=== ", 0 ) == 0 ) { lic = WinLicenseInfo(); continue; }
			std::smatch m;
			if ( !std::regex_match( l, m, r_kv ) ) continue;
			const std::string key = m[ 1 ].str(), rest = m[ 2 ].str();
			std::string v;
			if ( !rest.empty() && rest[ 0 ] == '"' )
			{
				const size_t q = rest.find( '"', 1 );
				v = rest.substr( 1, q == std::string::npos ? std::string::npos : q - 1 );
			}
			else v = rest.substr( 0, rest.find( ' ' ) );
			if ( key == "ID" ) lic.sku_id = v;
			else if ( key == "Name" ) lic.name = v;
			else if ( key == "Description" ) lic.description = v;
			else if ( key == "LicenseStatus" ) lic.license_status = v;
			else if ( key == "ProductKeyChannel" ) lic.product_key_channel = v;
			else if ( key == "OfflineInstallationId" )
			{
				WinInstallationId w;
				w.text = keep( v );
				w.source = fmt( "license.txt:%zu", i + 1 );
				w.license = lic;
				rows.push_back( w );
			}
		}
		return rows;
	}

	// ── tool artifacts (BuildConfig "exclude"; user decision J 2026-10-16) ───────────────────────────────────────────────
	const ExclusionRange* ExclusionScan::range_of( const std::string& file, size_t line ) const
	{
		for ( const ExclusionRange& r : ranges )
			if ( r.file == file && line >= r.first_line && line <= r.last_line ) return &r;
		return nullptr;
	}
	bool ExclusionScan::excluded( const std::string& file, size_t line ) const { return range_of( file, line ) != nullptr; }

	// the unit of a pattern hit: a quoted list item / numbered list line alone; else its "## " / "=== " block (a registry key
	// block with all its subkey blocks and the parent block's subkey line); a block recording the item's absence is skipped
	ExclusionScan ProfileGenerator::parse_tool_exclusions( const std::vector<TextFile>& files, const std::vector<ToolExclusion>& items )
	{
		ExclusionScan s;
		static const std::regex r_list( R"re(^\s*"[^"]*"\s*$)re" ), r_numbered( R"(^\s*[0-9]+\s{2,}\S.*$)" ), r_key( R"(^## (HK[A-Z_]+\\.+)$)" );
		for ( const TextFile& f : files )
		{
			if ( f.name == "capture_info.txt" || f.name == "certificates.txt" ) continue;	// run metadata / not part of the profile
			s.scanned_files++;
			const auto lines = split_lines( f.data );
			std::vector<std::string> lc;
			for ( const std::string& l : lines ) lc.push_back( lower( l ) );
			auto header = [ & ]( size_t i ) { const std::string& l = lines[ i ]; return l.rfind( "## ", 0 ) == 0 || l.rfind( "=== ", 0 ) == 0 || l.rfind( "##########", 0 ) == 0; };
			auto comment = [ & ]( size_t i ) { return !lines[ i ].empty() && lines[ i ][ 0 ] == '#' && !header( i ); };
			auto block_of = [ & ]( size_t i, size_t& a, size_t& z ) {
				size_t k = i + 1;
				while ( k-- > 0 ) if ( header( k ) ) break;
				if ( k == size_t( -1 ) ) return false;
				a = k;
				z = k;
				while ( z + 1 < lines.size() && !header( z + 1 ) ) z++;
				while ( z > a && lines[ z ].empty() ) z--;
				return true;
			};
			auto add = [ & ]( const ToolExclusion& it, const char* unit, size_t a, size_t z ) {
				for ( const ExclusionRange& r : s.ranges )
					if ( r.file == f.name && r.first_line <= a + 1 && r.last_line >= z + 1 ) return;	// already inside an excluded range
				if ( z - a > 2000 ) fail( fmt( "exclusion \"%s\": the unit %s:%zu-%zu is larger than 2000 lines - review the patterns", it.name.c_str(), f.name.c_str(), a + 1, z + 1 ) );
				s.ranges.push_back( { it.name, f.name, unit, a + 1, z + 1 } );
			};
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				if ( lines[ i ].empty() || comment( i ) ) continue;
				auto hits = [ & ]( const ToolExclusion& x, size_t k ) {
					for ( const std::string& p : x.patterns ) if ( !p.empty() && lc[ k ].find( lower( p ) ) != std::string::npos ) return true;
					return false;
				};
				for ( size_t ii = 0; ii < items.size(); ii++ )
				{
					if ( !hits( items[ ii ], i ) ) continue;
					size_t a = 0, z = 0;
					if ( std::regex_match( lines[ i ], r_list ) || std::regex_match( lines[ i ], r_numbered ) || !block_of( i, a, z ) ) { add( items[ ii ], "line", i, i ); continue; }
					// a unit is named after the first list item any of its lines matches (the printer's key holds "RustDesk" as Mfg
					// before "RustDesk Printer": it stays the printer's)
					size_t owner = ii;
					for ( size_t jj = 0; jj < ii; jj++ )
						for ( size_t k = a; k <= z && owner == ii; k++ )
							if ( hits( items[ jj ], k ) ) owner = jj;
					const ToolExclusion& it = items[ owner ];
					bool absent = false;
					for ( size_t k = a; k <= z; k++ )
						absent |= lc[ k ].find( "exists: no" ) != std::string::npos || lc[ k ].find( "not present" ) != std::string::npos || lc[ k ].find( "(not found)" ) != std::string::npos;
					if ( absent ) { s.absence_records++; continue; }
					std::smatch m;
					if ( !std::regex_match( lines[ a ], m, r_key ) ) { add( it, "block", a, z ); continue; }
					const std::string key = m[ 1 ].str(), klc = lower( key ) + "\\";
					add( it, "registry key", a, z );
					for ( size_t k = z + 1; k < lines.size(); k++ )	// its subkey blocks
					{
						std::smatch m2;
						size_t a2 = 0, z2 = 0;
						if ( std::regex_match( lines[ k ], m2, r_key ) && lower( m2[ 1 ].str() ).rfind( klc, 0 ) == 0 && block_of( k, a2, z2 ) ) add( it, "registry subkey", a2, z2 );
					}
					const size_t bs = key.rfind( '\\' );
					const std::string parent = lower( key.substr( 0, bs ) ), sub = "subkey \"" + lower( key.substr( bs + 1 ) ) + "\"";
					for ( size_t k = 0; k < lines.size(); k++ )	// the parent block's subkey line
					{
						std::smatch m2;
						size_t a2 = 0, z2 = 0;
						if ( !std::regex_match( lines[ k ], m2, r_key ) || lower( m2[ 1 ].str() ) != parent || !block_of( k, a2, z2 ) ) continue;
						for ( size_t q = a2 + 1; q <= z2; q++ )
						{
							const size_t first = lc[ q ].find_first_not_of( " \t" );
							if ( first != std::string::npos && lc[ q ].compare( first, sub.size(), sub ) == 0 ) add( it, "parent subkey line", q, q );
						}
					}
				}
			}
		}
		return s;
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
			std::vector<FieldMapEntry> manual;	// occurrences no needle finds (the SusClientIdValidation hex dump), id set later
			Bytes blob;							// rule 8c: the real SusClientIdValidation bytes
			std::vector<std::string> line_markers;	// non-empty: an occurrence counts only on a line holding one of them (rule 7b ISO forms)
		};
		// rule 7b: the lines on which an ISO form of an install-history event is that event (not a registry last-write time)
		const std::vector<std::string> k_event_iso_markers = { "devpkey_device_firstinstalldate", "devpkey_device_installdate", "devpkey_device_lastarrivaldate",
			"(filetime)", "(seconds since" };
		// rule 7b: the install-history kinds (FILETIME decimal values unless noted)
		const std::set<std::string> k_history = { "install_date", "install_time", "device_install_time", "device_arrival_time", "sqm_first_session", "diagtrack_time",
			"bt_event_time", "event_iso" };
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

	TextIdentityGen ProfileGenerator::gen_text_identity( BuildContext& ctx, const TextIdentityInput& in )
	{
		const std::vector<TextFile>& files = in.files;
		const RuleSet& rules = ctx.config.rules;
		TextIdentityGen out;
		std::vector<Ident> ids;
		std::map<std::string, size_t> by_key;	// lower-case value (GUIDs without braces) -> index
		std::map<std::string, std::string> mac_adapter;	// lower-case MAC -> description of its first adapter
		std::map<std::string, int> counters;
		std::set<std::string> keep_style;		// GUID keys whose own style (braces / case) identity.tsv keeps
		auto key_of = []( const std::string& v ) { std::string k = lower( v ); if ( !k.empty() && k.front() == '{' && k.size() == 38 ) k = k.substr( 1, 36 ); return k; };
		// identifier anchors inside the ranges of an excluded tool artifact (parse_tool_exclusions) create no identity; their
		// values are checked below never to occur outside the excluded ranges, and stay in the leak guard
		struct ExcludedAnchor { std::string kind, value, file; size_t line; std::vector<std::string> markers; };
		std::vector<ExcludedAnchor> excluded_anchors;
		Ident scratch;
		auto add = [ & ]( const std::string& kind, const std::string& rule, const std::string& value, const std::string& file, size_t line ) -> Ident& {
			if ( line && in.exclusions.excluded( file, line ) )
			{
				excluded_anchors.push_back( { kind, value, file, line } );
				scratch = Ident();
				return scratch;
			}
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
				const bool marked = lines[ i ].find( "[IDENTIFIER" ) != std::string::npos && lines[ i ][ 0 ] != '#';
				if ( lines[ i ].find( "CONTAINERID" ) == std::string::npos && lines[ i ].find( "ContainerId" ) == std::string::npos )
				{
					// fwid marks only the container-ID properties in pci_devices.txt; any other marked line needs a decision
					if ( marked ) fail( fmt( "pci_devices.txt:%zu: an identifier line no rule covers - it needs a field decision", i + 1 ) );
					continue;
				}
				if ( std::regex_search( lines[ i ], m, r_container_pci ) )
				{
					if ( key_of( m[ 1 ].str() ) != k_null_container ) add( "container_id", "4c", m[ 1 ].str(), f_pci->name, i + 1 );
				}
				else if ( marked ) fail( fmt( "pci_devices.txt:%zu: an identifier line no rule covers - it needs a field decision", i + 1 ) );
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
				if ( !l.empty() && l[ 0 ] == '#' ) continue;	// fwid's file comments describe the marks, they hold no value
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
						if ( i + 1 < lines.size() && std::regex_search( lines[ i + 1 ], iso, r_iso ) )	// "    = <ISO> (seconds since ...)" / "(FILETIME)"
						{
							Ident& e = add( "event_iso", "7b", iso[ 1 ].str(), f->name, i + 2 );
							e.line_markers = k_event_iso_markers;
							if ( &e == &scratch ) excluded_anchors.back().markers = k_event_iso_markers;
						}
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
		// rule 8a: one installation ID per SoftwareLicensingProduct row of license.txt, with that row's exact license data
		if ( const TextFile* f = find_file( "license.txt" ) ) win_ids = parse_license_rows( *f );
		static const std::regex r_inline_hex( R"(\]: ([0-9A-Fa-f]+) \([0-9]+ bytes hex\))" );
		static const std::regex r_dump( R"(^(\s+[0-9A-Fa-f]{4}: )((?:[0-9A-Fa-f]{2} )+)\s*\|(.*)\|$)" );
		for ( const char* fn : { "license.txt", "update_ids.txt", "disks.txt", "hid.txt", "gpu.txt", "bluetooth.txt", "audio.txt", "chid.txt", "platform_security.txt",
				  "environment.txt", "wifi.txt", "printers.txt", "edid.txt" } )
		{
			const TextFile* f = find_file( fn );
			if ( !f ) continue;
			const std::string file = fn;
			auto lines = split_lines( f->data );
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				const std::string& l = lines[ i ];
				if ( l.find( "[IDENTIFIER" ) == std::string::npos || l[ 0 ] == '#' ) continue;	// '#': fwid's file comments describe the marks
				// "[IDENTIFIER?] review: ..." on a line of its own marks the value line above it (gpu.txt)
				const bool review = i > 0 && l.find_first_not_of( " \t" ) == l.find( "[IDENTIFIER?]" );
				const std::string& subject = review ? lines[ i - 1 ] : l;
				const size_t sline = review ? i : i + 1;	// 1-based line of the marked value
				const std::string k = lower( subject );
				const std::string v = field_value( subject );
				std::smatch m;
				const bool guid = std::regex_search( subject, m, r_any_guid );
				auto has = [ & ]( std::initializer_list<const char*> w ) { for ( const char* x : w ) if ( k.find( x ) != std::string::npos ) return true; return false; };
				if ( file == "chid.txt" || file == "platform_security.txt" ) continue;	// CHIDs: from SMBIOS fields that stay exact (check_chid_fields); Secure Boot / TPM exact
				else if ( file == "license.txt" && has( { "installationid" } ) )
				{
					bool row = false;
					for ( const WinInstallationId& w : win_ids ) row |= w.text == v && w.source == fmt( "license.txt:%zu", i + 1 );
					if ( !row ) undecided( file, i + 1 );	// a marked installation ID outside a SoftwareLicensingProduct row
					add( "win_installation_id", "8a", v, file, i + 1 );
				}
				else if ( file == "update_ids.txt" && has( { "\"susclientidvalidation\"" } ) )
				{
					// rule 8c: the blob inline ("...]: <hex> (N bytes hex)") or as a hex dump on the lines below (bytes + ASCII column)
					Bytes blob;
					std::vector<FieldMapEntry> rows;
					std::smatch hm;
					if ( std::regex_search( l, hm, r_inline_hex ) && hm[ 1 ].length() % 2 == 0 )
						for ( size_t q = 0; q < size_t( hm[ 1 ].length() ); q += 2 ) blob.push_back( uint8_t( std::stoul( hm[ 1 ].str().substr( q, 2 ), nullptr, 16 ) ) );
					else
						for ( size_t q = i + 1; q < lines.size(); q++ )
						{
							std::smatch dm;
							if ( !std::regex_match( lines[ q ], dm, r_dump ) ) break;
							std::istringstream hs( dm[ 2 ].str() );
							for ( std::string h; hs >> h; ) blob.push_back( uint8_t( std::stoul( h, nullptr, 16 ) ) );
							rows.push_back( { file, "", "8c", "sus_client_id_validation (hex dump: bytes)", uint64_t( dm.position( 2 ) + 1 ), q + 1, uint32_t( dm[ 2 ].length() - 1 ) } );
							rows.push_back( { file, "", "8c", "sus_client_id_validation (hex dump: ASCII column)", uint64_t( dm.position( 3 ) + 1 ), q + 1, uint32_t( dm[ 3 ].length() ) } );
						}
					if ( blob.empty() ) fail( fmt( "update_ids.txt:%zu: SusClientIdValidation in an unknown form - it needs a field decision", i + 1 ) );
					Ident& d = add( "sus_client_id_validation", "8c", hex_of( blob.data(), blob.size(), true ), file, i + 1 );
					d.manual = rows;
					d.blob = blob;
				}
				else if ( file == "update_ids.txt" && has( { "\"susclientid\"", "susclientid " } ) && guid ) add_guid( "sus_client_id", m[ 0 ].str(), file, i + 1, true );
				else if ( file == "update_ids.txt" && has( { "diagtrack", "telemetry", "deviceid", "machineid", "sqm" } ) )
				{
					if ( guid ) add_guid( "telemetry_id", m[ 0 ].str(), file, i + 1, true );
					else add( "telemetry_id", "8b", v, file, i + 1 );
				}
				else if ( file == "disks.txt" && has( { "serial" } ) )
				{
					// rule 8d: the serial without its blank padding; a placeholder (one repeated character: the Sabrent bridge) is exact
					std::string core = v;
					while ( !core.empty() && core.back() == ' ' ) core.pop_back();
					core.erase( 0, std::min( core.size(), core.find_first_not_of( ' ' ) ) );
					if ( check_filler( core ) ) { out.filler_serials++; continue; }
					add( "disk_serial", "8d", core, file, i + 1 );
				}
				else if ( file == "hid.txt" && has( { "serial" } ) )
				{
					// rule 4d: = a USB serial -> that identity; a placeholder or no serial -> exact (decision J: a real HID serial of a
					// device whose USB instance serial is a placeholder is synthetic, the placeholder stays)
					if ( v.empty() ) continue;
					if ( check_usb_placeholder( v ) ) { out.filler_serials++; continue; }
					add( "hid_serial", "4d", v, file, i + 1 );
				}
				else if ( file == "hid.txt" && has( { "path:" } ) ) {}	// a HID device path: PnP instance IDs (hashes exact, rule 4b) + the HID class GUID; identifiers inside it match their own identities
				else if ( ( file == "hid.txt" || file == "bluetooth.txt" || file == "audio.txt" ) && has( { "manufacturer", "product", "name" } ) && !guid ) {}	// names exact
				else if ( file == "gpu.txt" && has( { "\"usermodedriverguid\"", "driverstore" } ) ) {}	// driver-package constants: exact (user decision J)
				else if ( file == "gpu.txt" && has( { "model", "driver", "bios", "memory", "chiptype", "dactype", "adapterstring" } ) && !guid ) {}	// exact
				else if ( file == "gpu.txt" ) { if ( guid ) add_guid( "gpu_guid", m[ 0 ].str(), file, sline, true ); else add( "gpu_unique", "8e", v, file, sline ); }
				else if ( file == "bluetooth.txt" && std::regex_match( v, m, r_macish ) )
				{
					std::string mac = upper( m[ 1 ].str() + ":" + m[ 2 ].str() + ":" + m[ 3 ].str() + ":" + m[ 4 ].str() + ":" + m[ 5 ].str() + ":" + m[ 6 ].str() );
					if ( !by_key.count( key_of( mac ) ) ) add( "bt_device", "3", mac, file, i + 1 );	// the local radio = its adapter MAC identity
				}
				else if ( file == "audio.txt" && guid )
				{
					for ( auto it = std::sregex_iterator( l.begin(), l.end(), r_any_guid ); it != std::sregex_iterator(); ++it ) add_guid( "audio_endpoint", ( *it )[ 0 ].str(), file, i + 1, false );
				}
				else if ( file == "wifi.txt" && guid ) add_guid( "wlan_interface_guid", m[ 0 ].str(), file, i + 1, false );	// = the network interface GUID
				else if ( file == "environment.txt" && has( { "workgroup", "domain" } ) ) {}	// the workgroup / domain name: exact
				else if ( file == "edid.txt" && ( has( { "serial" } ) || ( i > 0 && lower( lines[ i - 1 ] ).find( "tag 0xff" ) != std::string::npos ) ) ) {}	// the EDID serials (the
					// serial field, the FFh descriptor and its string): identities from gen_edid (presets)
				else undecided( file, sline );
			}
		}
		// rule 3: Bluetooth addresses (bluetooth.txt): the paired-device subkey names of BTHPORT\Parameters\Devices and the
		// "ServicesFor<address>" subkey names (the local radio), 12 hex digits each - fwid marks them in its file comment
		if ( const TextFile* f = find_file( "bluetooth.txt" ) )
		{
			static const std::regex r_dev_key( R"(^## HKLM\\SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters\\(?:Devices|LocalInfo)\\([0-9A-Fa-f]{12})$)" );
			static const std::regex r_dev_sub( R"re(^\s+subkey "([0-9A-Fa-f]{12})")re" ), r_svc( R"(ServicesFor([0-9A-Fa-f]{12})(?![0-9A-Za-z]))" );
			auto lines = split_lines( f->data );
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				std::smatch m;
				const std::string& l = lines[ i ];
				if ( l.empty() || ( l[ 0 ] == '#' ) ) continue;
				if ( !std::regex_match( l, m, r_dev_key ) && !std::regex_search( l, m, r_dev_sub ) && !std::regex_search( l, m, r_svc ) ) continue;
				const std::string a = upper( m[ 1 ].str() );
				std::string mac;
				for ( size_t q = 0; q < 12; q += 2 ) mac += ( q ? ":" : "" ) + a.substr( q, 2 );
				if ( !by_key.count( key_of( mac ) ) ) add( "bt_device", "3", mac, f->name, i + 1 );	// an adapter MAC (the local radio) is that identity
			}
		}
		// rule 4c: a WSD printer port "WSD-<GUID>" (printers.txt; printer / driver names exact)
		if ( const TextFile* f = find_file( "printers.txt" ) )
		{
			static const std::regex r_wsd( R"re(^port "WSD-(\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?)")re" );
			auto lines = split_lines( f->data );
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				std::smatch m;
				if ( std::regex_search( lines[ i ], m, r_wsd ) ) add_guid( "printer_wsd_port", m[ 1 ].str(), f->name, i + 1, false );
			}
		}
		// rule 7b: the install-history timestamps shifted with the OS install (user decision 2026-10-16): the per-device
		// install / first-install / last-arrival dates (pci_devices.txt, "[machine-specific]", FILETIME + ISO), the SQM first
		// session, the DiagTrack FILETIME timestamps, the Bluetooth device events (REG_QWORD FILETIMEs, hex + decimal)
		{
			static const std::regex r_dev( R"(DEVPKEY_Device_(FirstInstallDate|InstallDate|LastArrivalDate) \[machine-specific\] type 0x10: ([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z) \(0x([0-9A-Fa-f]{16})\))" );
			static const std::regex r_qword( R"re(value "(WinSqmFirstSessionStartTime|LastFreeNetworkLossTime|LastConnectivityHeartBeatTime|LastPersistedEventTime|LastSeen|LastConnected|FingerprintTimestamp)"\s+REG_QWORD \(11\)\s+8 bytes: 0x([0-9A-Fa-f]{16}) \(([0-9]+)\))re" );
			auto add_ft = [ & ]( const char* kind, uint64_t v, const std::string& file, size_t line ) {
				if ( !v ) return;	// not set
				Ident& d = add( kind, "7b", std::to_string( v ), file, line );
				d.needles = { fmt( "0x%016llx", (unsigned long long)v ), std::to_string( v ) };
			};
			for ( const TextFile& f : files )
			{
				if ( f.name != "pci_devices.txt" && f.name != "update_ids.txt" && f.name != "bluetooth.txt" ) continue;
				const auto lines = split_lines( f.data );
				for ( size_t i = 0; i < lines.size(); i++ )
				{
					std::smatch m;
					if ( f.name == "pci_devices.txt" && std::regex_search( lines[ i ], m, r_dev ) )
					{
						add_ft( m[ 1 ].str() == "LastArrivalDate" ? "device_arrival_time" : "device_install_time", std::stoull( m[ 3 ].str(), nullptr, 16 ), f.name, i + 1 );
						Ident& e = add( "event_iso", "7b", m[ 2 ].str(), f.name, i + 1 );
						e.line_markers = k_event_iso_markers;
						if ( &e == &scratch ) excluded_anchors.back().markers = k_event_iso_markers;
					}
					else if ( f.name != "pci_devices.txt" && std::regex_search( lines[ i ], m, r_qword ) )
					{
						const std::string n = m[ 1 ].str();
						if ( std::stoull( m[ 2 ].str(), nullptr, 16 ) != std::stoull( m[ 3 ].str() ) ) fail( fmt( "%s:%zu: a FILETIME whose hex and decimal forms differ", f.name.c_str(), i + 1 ) );
						add_ft( n == "WinSqmFirstSessionStartTime" ? "sqm_first_session" : f.name == "bluetooth.txt" ? "bt_event_time" : "diagtrack_time", std::stoull( m[ 3 ].str() ), f.name, i + 1 );
					}
				}
			}
		}
		// volatile.tsv: every line fwid marks "[volatile" and the DiagTrack counters / timestamps (update_ids.txt; user decision
		// J: captured values kept as a plausible snapshot the emulator may advance) - names and locations, never values
		for ( const TextFile& f : files )
		{
			if ( !searched( f ) || f.name == "capture_info.txt" ) continue;
			auto lines = split_lines( f.data );
			bool diag = false;
			static const std::regex r_val( R"re(^\s+value "([^"]+)")re" );
			for ( size_t i = 0; i < lines.size(); i++ )
			{
				const std::string& l = lines[ i ];
				if ( l.empty() || ( l[ 0 ] == '#' && l.rfind( "## ", 0 ) != 0 ) ) continue;
				if ( l.rfind( "## ", 0 ) == 0 ) diag = f.name == "update_ids.txt" && lower( l ).find( "\\diagnostics\\diagtrack" ) != std::string::npos;
				std::smatch m;
				if ( diag && std::regex_search( l, m, r_val ) )
				{
					const std::string n = m[ 1 ].str(), nl = lower( n );
					const bool timestamp = nl.size() > 4 && nl.compare( nl.size() - 4, 4, "time" ) == 0;	// not TimeStampInterval (a setting)
					if ( timestamp || nl.find( "count" ) != std::string::npos || nl.find( "latency" ) != std::string::npos )
						out.volatiles.push_back( { f.name, i + 1, n, timestamp && l.find( "0x01" ) != std::string::npos
							? "DiagTrack timestamp (user decision J: a captured snapshot the emulator may advance; FILETIME shifted with the install history, rule 7b)"
							: "DiagTrack counter / timestamp (user decision J: a captured snapshot the emulator may advance)" } );
					continue;
				}
				const size_t vp = lower( l ).find( "[volatile" );
				if ( vp == std::string::npos ) continue;
				std::string head = l.substr( 0, vp ), name;
				if ( head.rfind( "## ", 0 ) == 0 ) name = head.substr( 3, head.find( " (" ) == std::string::npos ? std::string::npos : head.find( " (" ) - 3 );
				else if ( size_t c = head.find( ": " ); c != std::string::npos ) name = head.substr( 0, c );
				else
				{
					std::istringstream parts( head );
					for ( std::string part; std::getline( parts, part, ',' ); )
					{
						std::istringstream w( part );
						std::string first;
						w >> first;
						if ( !first.empty() && std::isalpha( uint8_t( first[ 0 ] ) ) ) name += ( name.empty() ? "" : ", " ) + first;
					}
				}
				while ( !name.empty() && name.back() == ' ' ) name.pop_back();
				const size_t ve = l.find( ']', vp );
				out.volatiles.push_back( { f.name, i + 1, name, l.substr( vp + 1, ve == std::string::npos ? std::string::npos : ve - vp - 1 ) } );
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
		// an occurrence is a token: not part of a longer letter / digit run (other needles than names and GUIDs: not inside a
		// hyphenated token either, e.g. a MAC as a version-1 GUID node); fwid writes '\' as \x5C (a boundary); in bluetooth.txt
		// "ServicesFor<address>" names a subkey after the local radio address (a boundary before the 12 hex digits)
		auto is_token = []( const std::string& lc, size_t pos, size_t len, bool name_rule ) {
			const bool esc = pos >= 4 && lc.compare( pos - 4, 4, "\\x5c" ) == 0;
			const bool svc = len == 12 && pos >= 11 && lc.compare( pos - 11, 11, "servicesfor" ) == 0;
			const char b = pos && !esc && !svc ? lc[ pos - 1 ] : ' ', a = pos + len < lc.size() ? lc[ pos + len ] : ' ';
			const bool guid_shaped = len == 36 && lc[ pos + 8 ] == '-' && lc[ pos + 13 ] == '-' && lc[ pos + 18 ] == '-' && lc[ pos + 23 ] == '-';
			const bool word = name_rule || guid_shaped ? ( is_alnum( b ) || is_alnum( a ) ) : ( is_alnum( b ) || is_alnum( a ) || b == '-' || a == '-' );
			return !word;
		};
		auto short_number = []( const std::string& n ) { return n.size() < 6 && std::all_of( n.begin(), n.end(), []( char c ) { return c >= '0' && c <= '9'; } ); };
		// excluded tool artifacts: a value anchored only inside excluded ranges must not occur outside them (a capture_info.txt
		// line - run metadata - is excluded with it); otherwise it would need its own decision
		ExclusionScan excl = in.exclusions;
		for ( const ExcludedAnchor& x : excluded_anchors )
		{
			const std::string n = key_of( x.value );
			if ( by_key.count( n ) ) continue;	// also anchored outside the ranges: a kept identity, its rule applies
			if ( !short_number( n ) ) out.needles.push_back( n );	// leak guard
			const ExclusionRange* src = in.exclusions.range_of( x.file, x.line );
			const std::string item = src ? src->item : "?";
			for ( const TextLc& t : texts )
				for ( size_t p = t.lc.find( n ); p != std::string::npos; p = t.lc.find( n, p + 1 ) )
				{
					if ( !is_token( t.lc, p, n.size(), false ) ) continue;
					const size_t li = line_of( t, p );
					if ( excl.excluded( t.f->name, li ) ) continue;
					if ( !x.markers.empty() )	// an ISO event form: only on the event lines (not a registry last-write time)
					{
						const size_t ls = t.starts[ li - 1 ], le = li < t.starts.size() ? t.starts[ li ] : t.lc.size();
						const std::string line = t.lc.substr( ls, le - ls );
						if ( std::none_of( x.markers.begin(), x.markers.end(), [ & ]( const std::string& mk ) { return line.find( mk ) != std::string::npos; } ) ) continue;
					}
					if ( t.f->name == "capture_info.txt" )
					{
						excl.ranges.push_back( { item, t.f->name, "run-metadata line", li, li } );
						out.extra_exclusions.push_back( excl.ranges.back() );
						continue;
					}
					fail( fmt( "%s:%zu: a value of the excluded tool artifact \"%s\" (%s, anchored at %s:%zu) also occurs outside its excluded ranges - it needs a decision",
						t.f->name.c_str(), li, item.c_str(), x.kind.c_str(), x.file.c_str(), x.line ) );
				}
		}
		out.excluded_anchors = excluded_anchors.size();

		// ── synthetic values ──────────────────────────────────────────────────────────────────────────────────────────
		// 1. the adapter MACs: gen_mac_family (one synthetic MAC per physical NIC / radio, dependents re-derived)
		MacFamily fam;
		for ( size_t i : mac_ids )
		{
			ids[ i ].family = fam.members.size();
			fam.members.push_back( { { keep( ids[ i ].original ), ids[ i ].desc, ids[ i ].id } } );
		}
		const MacFamily sfam = gen_mac_family( ctx, fam );
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
			out.macs.push_back( { { keep( d.original ), d.desc, d.id }, { keep( d.synthetic ), d.desc, d.id } } );
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
		// 1b. the install instant (rule 7b) BEFORE the GUIDs: an RFC 4122 version-1 GUID's timestamp moves by the same
		// install-history offset (rule 4c, U1516). ONE instant for InstallDate and InstallTime (gen_install_time records the
		// offset in ctx's identity map; this capture's offset only - a value of an earlier build of ctx is dropped first)
		ctx.identity_map.has_install_shift = false;
		ctx.identity_map.install_shift = 0;
		bool have_instant = false;
		InstallTime inst, inst_real;
		// an install-history value as unix seconds (FILETIMEs floored to the second)
		auto history_unix = [ & ]( const Ident& x, int64_t& t ) {
			if ( !k_history.count( x.kind ) || x.preset ) return false;
			if ( x.kind == "install_date" ) { t = std::stoll( x.original ); return true; }
			if ( x.kind == "event_iso" ) return unix_of_iso( x.original, t );
			t = int64_t( std::stoull( x.original ) / 10000000ull ) - 11644473600LL;
			return true;
		};
		if ( std::any_of( ids.begin(), ids.end(), [ & ]( const Ident& x ) { int64_t t = 0; return history_unix( x, t ); } ) )
		{
			for ( const std::string& l : split_lines( f_info->data ) )
			{
				std::smatch m;
				static const std::regex r_build( R"(CurrentBuild ([0-9]+))" ), r_utc( R"(^run\.start_utc\s*=\s*(\S+))" );
				if ( l.rfind( "os.registry", 0 ) == 0 && std::regex_search( l, m, r_build ) ) inst_real.os_build = unsigned( std::stoul( m[ 1 ].str() ) );
				if ( std::regex_search( l, m, r_utc ) ) inst_real.capture_utc = m[ 1 ].str();
			}
			bool have_install = false;
			for ( const Ident& x : ids )
			{
				if ( x.kind == "install_date" ) { inst_real.unix_seconds = std::stoll( x.original ); have_install = true; }
				if ( x.kind == "install_time" ) inst_real.filetime = std::stoull( x.original );
			}
			if ( !have_install ) fail( "rule 7b: install-history timestamps without the OS InstallDate (os_ids.txt) - nothing to shift them with" );
			if ( !inst_real.filetime ) inst_real.filetime = ( uint64_t( inst_real.unix_seconds ) + 11644473600ull ) * 10000000ull;
			int64_t lo = INT64_MAX, hi = INT64_MIN;
			for ( const Ident& x : ids )
			{
				int64_t t = 0;
				if ( history_unix( x, t ) ) { lo = std::min( lo, t ); hi = std::max( hi, t ); }
			}
			inst_real.earliest_event = lo;
			inst_real.latest_event = hi;
			inst = gen_install_time( ctx, inst_real );
			have_instant = true;
		}
		// 2. everything else (dependent kinds - SIDs, NameSamCompatible, the install-history values - in the pass after it)
		const std::vector<std::string> usb_table = in.usb_table.empty() ? parse_usb_serial_table( { f_enum->data } ) : in.usb_table;
		static const std::set<std::string> k_post = { "machine_sid", "user_sid", "name_sam_compatible", "install_date", "install_time", "device_install_time",
			"device_arrival_time", "sqm_first_session", "diagtrack_time", "bt_event_time", "event_iso" };	// = the SIDs, NameSamCompatible and k_history
		static const std::set<std::string> k_late = { "disk_serial", "sus_client_id_validation" };	// after the USB serials (step 3a / 3b)
		for ( Ident& d : ids )
		{
			if ( d.kind == "mac" || d.preset || k_post.count( d.kind ) || k_late.count( d.kind ) ) continue;
			const std::string label = "identity/" + d.id;
			if ( d.rule == "4a" ) d.synthetic = gen_computer_name( ctx, { d.original.c_str(), label } ).text;
			else if ( d.kind == "mac_eui64" )
			{
				const Ident& nic = ids[ size_t( d.parent ) ];
				d.synthetic = gen_eui64_instance_id( ctx, { d.original.c_str(), d.eui_ffff }, { nic.synthetic.c_str(), nic.desc, nic.id } ).text;
				out.fields.push_back( { "network_adapters.txt", d.id, "3r", fmt( "relation: the EUI-64 form (OUI + %s + NIC part%s) of the MAC of adapter \"%s\" in instance IDs",
					d.eui_ffff ? "FFFF" : "FFFE", d.eui_ffff ? " + 00" : "", nic.desc.c_str() ), 0, 0, uint32_t( d.original.size() ) } );
			}
			else if ( d.rule == "3" )	// another device's MAC (a paired Bluetooth device): OUI kept
			{
				d.synthetic = gen_mac( ctx, { d.original.c_str(), "", d.id } ).text;
				std::string l = lower( d.original ), h = l;
				std::replace( h.begin(), h.end(), ':', '-' );
				d.needles = { l, h, mac_plain( d.original ) };
			}
			else if ( d.rule == "4b" )
			{
				if ( !std::all_of( d.original.begin(), d.original.end(), is_hex ) ) fail( "rule 4b: the serial part is not a hex token" );
				d.synthetic = gen_hex_token( ctx, { d.original.c_str(), label } ).text;
			}
			else if ( d.rule == "4d" )
			{
				// ONE mapping per physical device for both profiles: the shared table's index keys the stream (never the value). A
				// HID serial equal to a USB serial is that identity already; one outside the table gets a stream of this environment
				auto it = std::find( usb_table.begin(), usb_table.end(), lower( d.original ) );
				if ( it == usb_table.end() && d.kind == "usb_serial" ) fail( "rule 4d: a USB serial of the capture is missing from the shared serial table" );
				d.synthetic = gen_usb_serial( ctx, { d.original.c_str(), it == usb_table.end() ? -1 : long( it - usb_table.begin() ), label } ).text;
			}
			else if ( d.rule == "7a" ) d.synthetic = gen_product_id( ctx, { d.original.c_str(), label } ).text;
			else if ( d.rule == "7c" || d.rule == "8b" || d.rule == "8e" ) d.synthetic = gen_class_string( ctx, { d.original.c_str(), label } ).text;
			else if ( d.rule == "7e" ) d.synthetic = gen_volume_serial( ctx, { d.original.c_str(), label } ).serial;
			else if ( d.rule == "8a" )
			{
				for ( WinInstallationId& w : win_ids )
					if ( w.text == d.original )
					{
						w.label = label;
						WinInstallationId s = gen_win_installation_id( ctx, w );
						d.synthetic = s.text;
						out.installation_ids.push_back( w );
						out.installation_ids.push_back( s );
						break;
					}
			}
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
				d.synthetic = gen_guid( ctx, { g.c_str(), label }, nodes ).text;
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
				if ( !sids.count( key ) ) sids[ key ] = gen_sid( ctx, { prefix.c_str(), fmt( "identity/sid%02d", ++nsid ) } ).prefix;
				return sids[ key ];
			};
			auto syn_value = [ & ]( const std::string& v, const char* kind ) -> std::string {
				for ( const Ident& x : ids ) if ( x.kind == kind && ieq( x.original, v ) ) return x.synthetic;
				return {};
			};
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
					if ( sdom.empty() ) sdom = gen_class_string( ctx, { dom.c_str(), "identity/" + d.id + "/domain" } ).text;	// a domain account
					if ( suser.empty() ) suser = gen_class_string( ctx, { user.c_str(), "identity/" + d.id + "/user" } ).text;
					d.synthetic = sdom + "\\" + suser;
				}
				else	// rule 7b: the install history, shifted together by ONE offset (gen_install_time, step 1b)
				{
					if ( !have_instant ) fail( "rule 7b: an install-history value without the install instant (step 1b)" );
					if ( d.kind == "install_date" ) d.synthetic = std::to_string( inst.unix_seconds );
					else if ( d.kind == "event_iso" )
					{
						int64_t t = 0;
						if ( !unix_of_iso( d.original, t ) ) fail( "rule 7b: an unreadable ISO timestamp" );
						d.synthetic = iso_of_unix( t + inst.shift_seconds );
					}
					else d.synthetic = std::to_string( uint64_t( int64_t( std::stoull( d.original ) ) + inst.shift_seconds * 10000000LL ) );
				}
				if ( d.needles.empty() ) d.needles = { lower( d.original ) };
			}
			// the self-check of the shift: synthetic - real = the one offset for every install-history value (so every pairwise
			// difference is the original one), every event strictly between the build release and the capture, and the same
			// events precede the OS install as in the capture (Windows setup installs devices before OOBE writes InstallDate)
			if ( have_instant && rules.r7 )
			{
				TimeShiftCheck& c = out.time_shift;
				const InstallWindow w = parse_install_window( inst_real.os_build, inst_real.capture_utc );
				for ( const Ident& x : ids )
				{
					int64_t tr = 0;
					if ( !history_unix( x, tr ) ) continue;
					Ident y = x;
					y.original = x.synthetic;
					int64_t ts = 0;
					history_unix( y, ts );
					c.shifted++;
					c.per_kind[ x.kind ]++;
					const bool exact = x.kind == "install_date" || x.kind == "event_iso" ? ts - tr == inst.shift_seconds
						: int64_t( std::stoull( x.synthetic ) ) - int64_t( std::stoull( x.original ) ) == inst.shift_seconds * 10000000LL;
					c.exact += exact;
					c.outside_window += !( ts > w.lo && ts < w.hi );
					c.before_install_real += tr < inst_real.unix_seconds;
					c.before_install_synthetic += ts < inst.unix_seconds;
				}
				if ( c.exact != c.shifted || c.outside_window || c.before_install_real != c.before_install_synthetic )
					fail( fmt( "self-check rule 7b: %zu of %zu install-history values shifted by the one offset, %zu outside the window, %zu / %zu before the install", c.exact, c.shifted,
						c.outside_window, c.before_install_real, c.before_install_synthetic ) );
			}
			// the self-check of the version-1 GUID timestamps (rule 4c, U1516): synthetic = real + the one offset (100-ns units) for
			// every RFC 4122 version-1 GUID identity; counted (no value): real timestamps before the OS install / after the capture
			if ( rules.r4c )
			{
				V1GuidCheck& c = out.v1_guids;
				const int64_t capture_unix = have_instant ? parse_install_window( inst_real.os_build, inst_real.capture_utc ).hi : 0;
				for ( const Ident& x : ids )
				{
					if ( x.rule != "4c" ) continue;
					const GuidV1 a = parse_guid_v1( x.original.c_str() ), b = parse_guid_v1( x.synthetic.c_str() );
					if ( !a.ok ) continue;
					c.guids++;
					c.exact += have_instant && b.ok && int64_t( b.timestamp ) - int64_t( a.timestamp ) == inst.shift_seconds * 10000000LL;
					const int64_t unix_real = int64_t( a.timestamp / 10000000ull ) - 12219292800LL;	// 1582-10-15 -> 1970-01-01: 141427 days
					c.before_install += have_instant && unix_real < inst_real.unix_seconds;
					c.after_capture += have_instant && unix_real > capture_unix;
				}
				if ( c.exact != c.guids ) fail( fmt( "self-check rule 4c: %zu of %zu version-1 GUID timestamps = real + the install-history offset", c.exact, c.guids ) );
			}
		}
		// 3a. disk serials (8d): the start of a USB serial of the capture (a USB flash drive reports the start of its USB serial) =
		// the start of that serial's synthetic value; any other disk serial rule-2 classes
		for ( Ident& d : ids )
		{
			if ( d.kind != "disk_serial" ) continue;
			const Ident* usb = nullptr;
			for ( const Ident& x : ids )
				if ( x.kind == "usb_serial" && x.original.size() > d.original.size() && ieq( x.original.substr( 0, d.original.size() ), d.original ) ) { usb = &x; break; }
			if ( usb )
			{
				d.synthetic = follow_case( usb->synthetic.substr( 0, d.original.size() ), d.original );
				out.fields.push_back( { "disks.txt", d.id, "8dr", fmt( "relation: the disk serial = the first %zu characters of USB serial %s (the same device)", d.original.size(), usb->id.c_str() ),
					0, 0, uint32_t( d.original.size() ) } );
			}
			else d.synthetic = gen_class_string( ctx, { d.original.c_str(), "identity/" + d.id } ).text;
			d.needles = { lower( d.original ) };
		}
		// 3b. SusClientIdValidation (8c): the layout kept, every component its synthetic identity (gen_sus_client_id_validation)
		for ( Ident& d : ids )
		{
			if ( d.kind != "sus_client_id_validation" ) continue;
			SusValidationInput si { d.blob, {}, out.macs };
			for ( const Ident& x : ids ) if ( x.kind == "disk_serial" || x.kind == "usb_serial" ) si.strings.emplace_back( x.original, x.synthetic );
			si.strings.insert( si.strings.end(), in.smbios_strings.begin(), in.smbios_strings.end() );
			const SusValidation sv = gen_sus_client_id_validation( ctx, si );
			d.synthetic = sv.hex;
			for ( const std::string& c : sv.components ) out.fields.push_back( { "update_ids.txt", d.id, "8cr", "relation: SusClientIdValidation " + c, 0, 0, 0 } );
			d.needles = { lower( d.original ) };
		}
		for ( Ident& d : ids )
			for ( const std::string& n : d.needles )
				if ( !short_number( n ) ) out.needles.push_back( n );	// short pure numbers: too generic for the leak guard

		// occurrences in every searched text file of the capture (fieldmap rows; 1-based line and byte column); rows inside the
		// ranges of an excluded tool artifact are dropped
		std::map<std::string, size_t> occ, occ_excluded;
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
						else if ( !is_token( lc, pos, len, d.rule == "4a" ) ) continue;	// part of a longer token
						size_t li = line_of( t, pos ) - 1;
						if ( !d.line_markers.empty() )	// rule 7b ISO forms: only on the event lines (not a registry last-write time)
						{
							const size_t le = li + 1 < t.starts.size() ? t.starts[ li + 1 ] : lc.size();
							const std::string line = lc.substr( t.starts[ li ], le - t.starts[ li ] );
							if ( std::none_of( d.line_markers.begin(), d.line_markers.end(), [ & ]( const std::string& mk ) { return line.find( mk ) != std::string::npos; } ) ) continue;
						}
						if ( excl.excluded( t.f->name, li + 1 ) ) { occ_excluded[ d.id ]++; out.excluded_rows++; continue; }
						out.fields.push_back( { t.f->name, d.id, d.rule, d.kind, pos - t.starts[ li ] + 1, li + 1, uint32_t( len ) } );
						if ( !occ[ d.id ]++ ) first_at[ d.id ] = fmt( "%s:%zu", t.f->name.c_str(), li + 1 );
					}
				}
			}
		}
		for ( Ident& d : ids )
			for ( FieldMapEntry f : d.manual )
			{
				if ( excl.excluded( f.area, size_t( f.line ) ) ) { occ_excluded[ d.id ]++; out.excluded_rows++; continue; }
				f.id = d.id;
				out.fields.push_back( f );
				occ[ d.id ]++;
			}
		for ( Ident& d : ids )
		{
			if ( !rule_on( rules, d.rule ) ) d.synthetic = d.original;	// rule disabled: the real value stays (the leak guard refuses a synthetic profile)
			if ( !occ[ d.id ] && occ_excluded[ d.id ] ) { out.excluded_anchors++; continue; }	// only inside excluded ranges: not part of the profile
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

	// ── atomic profile writes (user decision 2026-10-17 "Atomic writes", U1515) ─────────────────────────────────────────
	namespace
	{
		// the sibling "<dir><suffix>" of a profile folder: "<dir>.partial" (the new profile while it is written) and
		// "<dir>.old" (the previous profile while the new one is switched in)
		fs::path sibling( const fs::path& dir, const wchar_t* suffix ) { return dir.parent_path() / ( dir.filename().native() + suffix ); }
		// a NEW file (CREATE_NEW: an existing file is never overwritten), written completely and flushed to disk
		void write_new_file( const fs::path& f, const Bytes& data )
		{
			HANDLE h = CreateFileW( f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr );
			if ( h == INVALID_HANDLE_VALUE ) fail( fmt( "cannot create %s (Win32 error %lu)", show( f ).c_str(), GetLastError() ) );
			size_t done = 0;
			bool ok = true;
			while ( ok && done < data.size() )
			{
				const DWORD part = DWORD( std::min<size_t>( data.size() - done, size_t( 1 ) << 30 ) );
				DWORD wrote = 0;
				ok = WriteFile( h, data.data() + done, part, &wrote, nullptr ) && wrote == part;
				done += wrote;
			}
			ok = ok && FlushFileBuffers( h );
			const DWORD err = ok ? 0 : GetLastError();
			CloseHandle( h );
			if ( !ok ) fail( fmt( "cannot write %s (Win32 error %lu)", show( f ).c_str(), err ) );
		}
		// a folder rename on the same volume (MoveFileExW without MOVEFILE_REPLACE_EXISTING: an existing target fails), tried
		// again while another process briefly holds a handle inside the folder (a reader, the search indexer, a virus
		// scanner): up to 150 attempts 20 ms apart. Returns 0 or the last Win32 error
		DWORD move_folder( const fs::path& from, const fs::path& to )
		{
			DWORD err = 0;
			for ( int i = 0; i < 150; i++ )
			{
				if ( MoveFileExW( from.c_str(), to.c_str(), 0 ) ) return 0;
				err = GetLastError();
				if ( err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION && err != ERROR_LOCK_VIOLATION ) break;
				Sleep( 20 );
			}
			return err;
		}
		// removes EXACTLY the listed files (relative paths) of a folder this tool created or validated, then the sub-folders
		// they emptied (deepest first) and the folder itself - each a single non-recursive remove; anything not listed stays
		// and keeps its folders. Never a recursive delete of a path
		struct KnownRemoval { size_t files = 0; bool folder_removed = false; size_t left = 0; };
		KnownRemoval remove_known( const fs::path& dir, const std::vector<std::string>& rel_files )
		{
			KnownRemoval r;
			std::error_code ec;
			std::set<fs::path> subdirs;
			for ( const std::string& rel : rel_files )
			{
				const fs::path f = ( dir / rel ).lexically_normal();
				if ( f.native().rfind( dir.native(), 0 ) != 0 || f == dir ) continue;	// never outside dir
				for ( int i = 0; i < 25 && fs::is_regular_file( f, ec ); i++ )
				{
					if ( fs::remove( f, ec ) ) { r.files++; break; }
					Sleep( 20 );	// a handle held briefly by another process
				}
				for ( fs::path p = f.parent_path(); p != dir && p.native().size() > dir.native().size(); p = p.parent_path() ) subdirs.insert( p );
			}
			std::vector<fs::path> order( subdirs.begin(), subdirs.end() );
			std::sort( order.begin(), order.end(), []( const fs::path& a, const fs::path& b ) { return a.native().size() > b.native().size(); } );
			for ( const fs::path& p : order ) fs::remove( p, ec );	// non-recursive: fails (and stays) when not empty
			r.folder_removed = fs::remove( dir, ec );
			if ( !r.folder_removed && fs::exists( dir, ec ) )
				for ( auto it = fs::recursive_directory_iterator( dir, ec ); !ec && it != fs::recursive_directory_iterator(); it.increment( ec ) ) r.left++;
			return r;
		}
		// the manifest.txt this tool writes into a profile folder of environment env (first line)
		bool has_profile_manifest( const fs::path& dir, const std::string& env )
		{
			std::ifstream f( dir / "manifest.txt", std::ios::binary );
			std::string first;
			if ( !f || !std::getline( f, first ) ) return false;
			return first.rfind( "# emu-profilegen ", 0 ) == 0 && first.find( " - platform profile manifest - " + env + " (" ) != std::string::npos;
		}
		// one file read with full sharing (a concurrent rename of its folder is not blocked); missing = it does not exist
		struct SharedRead { bool ok = false, missing = false; Bytes data; };
		SharedRead read_shared( const fs::path& p )
		{
			SharedRead r;
			HANDLE h = CreateFileW( p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
			if ( h == INVALID_HANDLE_VALUE )
			{
				const DWORD e = GetLastError();
				r.missing = e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
				return r;
			}
			LARGE_INTEGER size {};
			bool ok = GetFileSizeEx( h, &size ) && size.QuadPart < ( int64_t( 1 ) << 31 );
			if ( ok ) r.data.resize( size_t( size.QuadPart ) );
			DWORD got = 0;
			ok = ok && ( r.data.empty() || ( ReadFile( h, r.data.data(), DWORD( r.data.size() ), &got, nullptr ) && got == r.data.size() ) );
			CloseHandle( h );
			r.ok = ok;
			return r;
		}
	}

	ProfileDirCheck ProfileGenerator::check_profile_dir( const fs::path& dir0 )
	{
		ProfileDirCheck c;
		std::error_code ec;
		fs::path dir = fs::absolute( dir0, ec ).lexically_normal();
		if ( dir.filename().empty() ) dir = dir.parent_path();	// a trailing separator
		const std::string env = lower( dir.filename().string() );
		if ( env != "vmware_guest" && env != "bare_host" ) { c.reason = "the folder name is not a profile environment (vmware_guest / bare_host)"; return c; }
		const fs::path p1 = dir.parent_path(), p2 = p1.parent_path(), p3 = p2.parent_path();
		const bool repo_profile = ieq( p1.filename().string(), "platform" ) && ieq( p2.filename().string(), "data" ) && ieq( p3.filename().string(), "Emulator" );
		const bool original_profile = ieq( p1.filename().string(), "profiles_original" );
		if ( !repo_profile && !original_profile ) { c.reason = "the folder is neither <...>\\Emulator\\data\\platform\\<env> nor <...>\\profiles_original\\<env>"; return c; }
		if ( fs::exists( dir, ec ) && !has_profile_manifest( dir, env ) ) { c.reason = "the folder holds no manifest.txt of an emu-profilegen " + env + " profile"; return c; }
		c.ok = true;
		return c;
	}

	IdentityMapFile ProfileGenerator::load_identity_map( const fs::path& file )
	{
		// the folder states a concurrent save_profile goes through: [dir] -> [dir, dir.partial] -> [dir.old, dir.partial] ->
		// [dir.old, dir] -> [dir]. A reader looks at dir, then dir.old, then dir again; when none of them holds the map while
		// a "<dir>.partial" or "<dir>.old" exists (a switch - or several quick ones - in progress, or a first write not yet
		// switched in), it tries again. No map = three attempts in a row (4 ms apart) found neither the map, nor a previous
		// copy, nor a write in progress: a reader preempted across switches cannot see that, since between two switches the
		// map is always in place
		IdentityMapFile r;
		const fs::path dir = file.parent_path(), old_dir = sibling( dir, L".old" ), old = old_dir / file.filename(), partial = sibling( dir, L".partial" );
		std::error_code ec;
		int quiet = 0;	// attempts in a row without a map and without a write in progress
		for ( size_t attempt = 1; attempt <= 2500; attempt++ )
		{
			r.attempts = attempt;
			bool busy = false;
			for ( int pass = 0; pass < 3; pass++ )
			{
				const fs::path& p = pass == 1 ? old : file;
				SharedRead s = read_shared( p );
				if ( s.ok )
				{
					r.found = true;
					r.source = pass == 1 ? "previous profile (a replacement was switching it)" : "profile";
					r.map = parse_identity_map( std::string( s.data.begin(), s.data.end() ) );
					return r;
				}
				busy |= !s.missing;	// present but not readable right now (a sharing conflict): try again
			}
			busy |= fs::exists( partial, ec ) || fs::exists( old_dir, ec );
			quiet = busy ? 0 : quiet + 1;
			if ( quiet == 3 ) return r;	// no map and no write in progress, three times in a row
			Sleep( 4 );
		}
		fail( "the identity map " + show( file ) + " could not be read for 10 s: a sharing conflict, or a profile write in progress that does not finish (an interrupted run's " +
			partial.filename().string() + " / " + old_dir.filename().string() + " folder?)" );
	}

	ProfileSaveResult ProfileGenerator::save_profile( const fs::path& dir0, const std::map<std::string, Bytes>& files, bool original )
	{
		ProfileSaveResult res;
		std::error_code ec;
		fs::path dir = fs::absolute( dir0, ec ).lexically_normal();
		if ( dir.filename().empty() ) dir = dir.parent_path();	// a trailing separator
		res.dir = dir;
		if ( original )
		{
			fs::path wt = check_git_work_tree( dir );
			if ( !wt.empty() ) fail( "refusing to write the ORIGINAL profile (real identifiers) inside a git work tree: " + show( dir ) + " is under " + show( wt ) );
		}
		if ( dir.filename().empty() || dir == dir.root_path() || dir.parent_path().empty() ) fail( "save_profile: not a profile folder path: " + show( dir ) );
		const fs::path partial = sibling( dir, L".partial" ), old = sibling( dir, L".old" );
		for ( const fs::path& s : { partial, old } )
			if ( fs::exists( s, ec ) )
				fail( "a folder of an interrupted profile write exists: " + show( s ) + " - this build did not create it and leaves it untouched; inspect it, then remove it by hand" );
		res.replaced = fs::exists( dir, ec );
		std::vector<std::string> names;	// the profile's files (relative paths)
		for ( auto& [ rel, data ] : files ) names.push_back( rel );
		if ( res.replaced )
		{
			if ( !fs::is_directory( dir, ec ) ) fail( show( dir ) + " exists and is not a folder" );
			const ProfileDirCheck pc = check_profile_dir( dir );
			if ( !pc.ok ) fail( "refusing to replace " + show( dir ) + ": " + pc.reason );
			std::vector<std::string> stale;
			for ( auto& e : fs::recursive_directory_iterator( dir ) )
				if ( e.is_regular_file() && !files.count( fs::relative( e.path(), dir ).string() ) ) stale.push_back( fs::relative( e.path(), dir ).string() );
			if ( !stale.empty() ) fail( "the output folder " + show( dir ) + " holds " + std::to_string( stale.size() ) + " file(s) that are not part of the profile (first: " + stale[ 0 ] + "); remove them first" );
		}
		fs::create_directories( dir.parent_path(), ec );
		// CREATE_NEW semantics for the folder: CreateDirectoryW fails when it exists (a concurrent writer of the same profile)
		if ( !CreateDirectoryW( partial.c_str(), nullptr ) ) fail( fmt( "cannot create %s (Win32 error %lu)", show( partial ).c_str(), GetLastError() ) );
		std::vector<std::string> written;
		try
		{
			for ( auto& [ rel, data ] : files )
			{
				const fs::path f = partial / rel;
				fs::create_directories( f.parent_path() );
				write_new_file( f, data );
				written.push_back( rel );
			}
		}
		catch ( const std::exception& )
		{
			remove_known( partial, written );	// the folder this call created, exactly the files it wrote
			throw;
		}
		if ( res.replaced )
			if ( const DWORD e = move_folder( dir, old ) )
			{
				remove_known( partial, written );
				fail( fmt( "cannot move the previous profile %s aside to %s (Win32 error %lu); nothing changed", show( dir ).c_str(), show( old ).c_str(), e ) );
			}
		if ( const DWORD e = move_folder( partial, dir ) )
		{
			std::string back = "no previous profile";
			if ( res.replaced ) back = move_folder( old, dir ) ? "the previous profile could NOT be moved back - it is " + show( old ) : "the previous profile was moved back";
			remove_known( partial, written );
			fail( fmt( "cannot switch %s into place as %s (Win32 error %lu); %s", show( partial ).c_str(), show( dir ).c_str(), e, back.c_str() ) );
		}
		res.files = written.size();
		if ( res.replaced )
		{
			// validated before anything is removed: the literal sibling this call created by renaming the validated profile
			// folder, still a profile folder of the same environment holding only files of this profile's list
			bool foreign = !has_profile_manifest( old, lower( dir.filename().string() ) );
			for ( auto it = fs::recursive_directory_iterator( old, ec ); !foreign && !ec && it != fs::recursive_directory_iterator(); it.increment( ec ) )
				if ( it->is_regular_file() && !files.count( fs::relative( it->path(), old ).string() ) ) foreign = true;
			if ( foreign || ec )
				res.note = "the previous profile " + show( old ) + " was kept: it no longer looks like this profile (a file outside the profile appeared) - inspect it, then remove it by hand";
			else
			{
				const KnownRemoval kr = remove_known( old, names );
				res.old_removed = kr.folder_removed;
				res.old_files_removed = kr.files;
				if ( !kr.folder_removed ) res.note = fmt( "the previous profile %s could not be removed completely (%zu entr%s left) - remove it by hand", show( old ).c_str(), kr.left, kr.left == 1 ? "y" : "ies" );
			}
		}
		return res;
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
	static std::map<std::string, Bytes> make_files( const Identity& p, bool original, const std::vector<std::pair<std::string, std::string>>& prov, const std::string& config_text,
		const std::string& identity_map_text )
	{
		std::map<std::string, Bytes> files;
		const char* env = env_dir_name( p.env );
		const char* kind = original ? "ORIGINAL: real values - LOCAL ONLY, never commit or push" : "SYNTHETIC: every replaced value is synthetic, not host data";
		files[ "smbios.bin" ] = original ? p.smbios_original : p.smbios_synthetic;
		for ( auto& [ name, data ] : p.acpi ) files[ "acpi\\" + name ] = data;
		for ( const EdidBlock& e : original ? p.edid_original : p.edid_synthetic ) files[ "edid\\" + e.name ] = e.bytes;
		for ( const DiskDescriptor& d : original ? p.disk_original : p.disk_synthetic ) files[ "disk\\" + d.name ] = d.bytes;

		// the tool artifacts that are not part of the presented machine, and the volatile values (both: locations only)
		std::ostringstream ex;
		ex << "# " << k_tool << " - excluded tool artifacts - " << env << " - capture ranges that are NOT part of the presented machine (BuildConfig\n"
		   << "# \"exclude\", user decision J 2026-10-16); identical in the synthetic and the original profile. A presenter omits these lines\n"
		   << "# (and adjusts the counts of the enclosing lists).\n"
		   << "# columns: item, capture text file, first line, last line (1-based, inclusive), unit\n";
		for ( const ExclusionRange& r : p.exclusions.ranges ) ex << r.item << '\t' << r.file << '\t' << r.first_line << '\t' << r.last_line << '\t' << r.unit << "\n";
		files[ "exclusions.tsv" ] = to_bytes( ex.str() );
		std::ostringstream vo;
		vo << "# " << k_tool << " - volatile values - " << env << " - captured values the emulator may advance (the captured value is a plausible\n"
		   << "# snapshot; user decision J 2026-10-16); identical in the synthetic and the original profile; names, never values.\n"
		   << "# columns: capture text file, line, name, mark\n";
		for ( const VolatileItem& v : p.volatiles ) vo << v.file << '\t' << v.line << '\t' << v.name << '\t' << v.mark << "\n";
		files[ "volatile.tsv" ] = to_bytes( vo.str() );

		std::ostringstream id;
		id << "# " << k_tool << " - identity of the fwid text outputs - " << env << " - " << kind << "\n"
		   << "# Rules (README.md / profile_generator.hpp): 3 MAC, 4a computer name, 4c GUID, 4d USB / HID serial (4b: PnP hashes exact),\n"
		   << "# 6 EDID serial, 7a ProductId, 7b install instant, 7c user name, 7d SID, 7e volume serial, 8a installation ID, 8c\n"
		   << "# SusClientIdValidation (hex, layout kept), 8d disk serial; identifiers only inside excluded tool artifacts are not listed.\n"
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
		   << "# columns: id, rule, file (smbios.bin / edid\\edid_<n>.bin / disk\\disk_<n>_descriptor.bin of this profile, or the capture's fwid text file), offset (blobs: absolute byte offset, smbios.bin incl. the 8-byte RawSMBIOSData header) or line:column (text) or - (relation rows), length in bytes, location\n";
		for ( const FieldMapEntry& f : p.fields )
			fm << f.id << '\t' << f.rule << '\t' << f.area << '\t'
			   << ( f.rule.back() == 'r' ? std::string( "-" ) : f.line ? fmt( "%llu:%llu", (unsigned long long)f.line, (unsigned long long)f.offset ) : fmt( "0x%04llX", (unsigned long long)f.offset ) )
			   << '\t' << f.length << '\t' << f.location << "\n";
		files[ "fieldmap.tsv" ] = to_bytes( fm.str() );
		files[ BuildConfig::k_file ] = to_bytes( config_text );
		files[ "identity_map.tsv" ] = to_bytes( identity_map_text );	// the original copy alone carries the install-history offset

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

	// ── identity_map.tsv ───────────────────────────────────────────────────────────────────────────────────────────────────
	// "key<TAB>kind<TAB>value" rows sorted by key; '#' comments. The ORIGINAL (local-only) copy adds one line
	// "install_shift<TAB>seconds" (rule 7b); a repository copy holding it is refused by every check
	IdentityMap ProfileGenerator::parse_identity_map( const std::string& text )
	{
		IdentityMap m;
		std::istringstream is( text );
		size_t n = 0;
		for ( std::string l; std::getline( is, l ); )
		{
			n++;
			if ( !l.empty() && l.back() == '\r' ) l.pop_back();
			if ( l.empty() || l[ 0 ] == '#' ) continue;
			const size_t t1 = l.find( '\t' ), t2 = t1 == std::string::npos ? t1 : l.find( '\t', t1 + 1 );
			if ( l.rfind( "install_shift\t", 0 ) == 0 )
			{
				m.has_install_shift = true;
				m.install_shift = std::stoll( l.substr( 14 ) );
				continue;
			}
			if ( t2 == std::string::npos ) fail( fmt( "identity map line %zu: key<TAB>kind<TAB>value expected", n ) );
			const std::string key = l.substr( 0, t1 );
			if ( !m.entries.emplace( key, IdentityMapEntry { l.substr( t1 + 1, t2 - t1 - 1 ), l.substr( t2 + 1 ) } ).second ) fail( fmt( "identity map line %zu: key given twice", n ) );
		}
		return m;
	}
	std::string ProfileGenerator::save_identity_map( const IdentityMap& m, Environment e, bool original )
	{
		std::ostringstream o;
		o << "# " << k_tool << " - identity map - " << env_dir_name( e ) << " - "
		  << ( original ? "ORIGINAL copy: LOCAL ONLY, never commit (it holds the install-history offset)" : "every value synthetic (RDRAND), no real value, no install-history offset" ) << "\n"
		  << "# The identity this profile presents: GENERATION (the first build, or --new-identity) drew every value from RDRAND\n"
		  << "# (Randomize->gen_...); every later build APPLIES this map to the clean capture, byte for byte. Keys name a field location\n"
		  << "# (environment + field id), never a value; \"shared/\" keys are the same in both profiles (one USB serial per physical device).\n"
		  << "# columns: key, kind, synthetic value (final text form)\n";
		for ( auto& [ k, v ] : m.entries ) o << k << '\t' << v.kind << '\t' << v.value << "\n";
		if ( original && m.has_install_shift ) o << "install_shift\t" << m.install_shift << "\n";
		return o.str();
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
		// the pinned clean captures: --guest is built ONLY from the clean VMware guest without VmLoader (fwid 1.4 Run3b, its
		// firmware checked against Run 3), --host ONLY from the clean host run (fwid 1.4, its firmware checked against fwid 1.3)
		static const BuildConfig guest = [] {
			BuildConfig c;
			c.env = Environment::vmware_guest;
			c.capture_parent = "baseline\\Run3b_clean_guest_fwid14_20261016";
			c.capture_folder = "*";
			c.capture_manifest_sha256 = "2828D5201670AFFF41FAD5027E90C113A310AE2F8CAA11F59282FA7FB79F526D";
			c.companions = { { "baseline\\Run3b_clean_guest_fwid14_20261016.sha256", "82C116C913650B687FA789AD52AAEDD3D724A1EA7C9B71344402095A2AB8EA6A" },
				{ "baseline\\Run3_clean_guest_probes_20261015.sha256", "748A3515D7FC401B14C97E650996D15426E891C477B5CF75F28C6EAF59F89E8D" } };
			c.firmware_ref_parent = "fwid\\baseline";	// Run 3 (fwid 1.2): the same clean VM state
			c.firmware_ref_folder = "Run3_clean_guest_*";
			c.firmware_ref_manifest_sha256 = "57268E68D23B488C74AC0ABA57EDD275DCEC9680153367A8C4A8786A30AE8E1C";
			return c;
		}();
		static const BuildConfig host = [] {
			BuildConfig c;
			c.env = Environment::bare_host;
			c.capture_parent = "baseline\\Host_clean_20261016_fwid14";
			c.capture_folder = "*";
			c.capture_manifest_sha256 = "FF2062E2E72AB2138CF4BD08942028E0E8D43E5F0FACBC105DB449E92CEC1C64";
			c.companions = { { "baseline\\Host_clean_20261016_fwid14.sha256", "74B0BBFFC670B4ED53C873872DF8970CEB4B634CB75D328C012CA8EAA3421FEE" },
				{ "baseline\\Host_clean_20261016.sha256", "1C8C498DB6CD26A57F8673375BF563868F12D3AEB79901363DF2EEEE8A8EF78C" },
				{ "baseline\\Host_clean_20261016_xinuse_all_lps.sha256", "CA726BF73851C9251D9DF30737B4CE171053B4814610798DA6551ECC03348F5C" } };
			c.firmware_ref_parent = "baseline\\Host_clean_20261016_fwid13";	// fwid 1.3 (itself byte-identical to the fwid 1.2 firmware)
			c.firmware_ref_folder = "*";
			c.firmware_ref_manifest_sha256 = "C9B008D860BEB95F3E7BFD5724FE6670A01EC4024E9C5471D7EF34851D706B89";
			return c;
		}();
		return e == Environment::vmware_guest ? guest : host;
	}
	// tool artifacts of the capture setup, excluded from both profiles (user decision J 2026-10-16); VMware Workstation's
	// host-side services and VMnet adapters are real host state and stay in --host. Patterns match case-insensitively.
	std::vector<ToolExclusion> BuildConfig::default_exclusions()
	{
		return {
			{ "RustDesk Printer", { "RustDesk Printer", "RustDesk v4 Printer Driver" }, "the remote-desktop tool's virtual printer (its queue, port, driver and PRINTENUM device)" },
			{ "RustDesk", { "RustDesk" }, "the remote-desktop tool itself: its service and any driver / registry / device trace (user decision 2026-10-16)" },
			{ "System Informer", { "SystemInformer" }, "the analysis tool's kernel driver and service (KSystemInformer, SystemInformer.sys, ksi.dll)" },
			{ "DebugView", { "Dbgv" }, "Sysinternals DebugView's kernel driver (Dbgv.sys)" },
			{ "VmLoader", { "vmloader" }, "any VmLoader service / driver / file trace" } };
	}
	BuildConfig BuildConfig::defaults( Environment e )
	{
		BuildConfig c = pinned( e );
		c.tool = k_tool;
		c.out = std::string( "Emulator\\data\\platform\\" ) + env_dir_name( e );
		c.original_out = std::string( "profiles_original\\" ) + env_dir_name( e );
		c.identity_map = "identity_map.tsv";
		c.peer_identity_map = std::string( "Emulator\\data\\platform\\" ) + env_dir_name( e == Environment::vmware_guest ? Environment::bare_host : Environment::vmware_guest ) + "\\identity_map.tsv";
		c.rules.r2h = e == Environment::bare_host;	// rule 2h is a bare-host rule; the guest keeps every type 17 string
		const BuildConfig& peer = pinned( e == Environment::vmware_guest ? Environment::bare_host : Environment::vmware_guest );
		c.peer_capture_parent = peer.capture_parent;
		c.peer_capture_folder = peer.capture_folder;
		c.peer_capture_manifest_sha256 = peer.capture_manifest_sha256;
		c.exclusions = default_exclusions();
		return c;
	}
	std::string BuildConfig::save() const
	{
		std::ostringstream o;
		o << "# emu-profilegen BuildConfig (format 3): every input of one platform-profile build. Rebuild with\n"
			 "#   emu-profilegen --config build_config.txt [--check]\n"
			 "# Paths: out / peer_identity_map relative to repo_root; identity_map relative to out; original_out, capture_parent and companion relative to capture_root;\n"
			 "# \"auto\" = NOVMP_CAPTURE_ROOT, else the first ancestor folder \"capture\" (capture_root) / the folder holding NoVmp.sln (repo_root).\n"
			 "# No identifier values: the capture folder is a name pattern plus its pinned manifest SHA-256. peer_capture_*: the other\n"
			 "# environment's clean capture, read only for the USB serial table both profiles share (rule 4d). firmware_ref_*: an older\n"
			 "# capture of the same machine; every firmware entry (rsmb / acpi / hwacpi / edid) must have the same SHA-256. exclude =\n"
			 "# name | pattern; pattern | reason: a tool artifact excluded from the profile (exclusions.tsv). identity_map: the identity\n"
			 "# this profile presents (every value drawn from RDRAND once, then applied by every build); peer_identity_map: the other\n"
			 "# environment's map (the shared USB serials). No seed (format 3).\n";
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
		if ( !firmware_ref_parent.empty() )
		{
			kv( "firmware_ref_parent", firmware_ref_parent );
			kv( "firmware_ref_folder", firmware_ref_folder );
			kv( "firmware_ref_manifest_sha256", firmware_ref_manifest_sha256 );
		}
		for ( const ToolExclusion& x : exclusions )
		{
			std::string pats;
			for ( const std::string& p : x.patterns ) pats += ( pats.empty() ? "" : "; " ) + p;
			kv( "exclude", x.name + " | " + pats + " | " + x.reason );
		}
		kv( "repo_root", repo_root );
		kv( "out", out );
		kv( "original_out", original_out );
		kv( "identity_map", identity_map );
		kv( "peer_identity_map", peer_identity_map );
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
			if ( k != "companion" && k != "exclude" && !seen.insert( k ).second ) fail( fmt( "build config line %zu: key \"%s\" given twice", n, k.c_str() ) );
			if ( k == "format" ) { if ( v != "3" ) fail( fmt( "build config line %zu: format %s not supported (3; the seed formats 1 / 2 were replaced by the identity map)", n, v.c_str() ) ); }
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
			else if ( k == "firmware_ref_parent" ) c.firmware_ref_parent = v;
			else if ( k == "firmware_ref_folder" ) c.firmware_ref_folder = v;
			else if ( k == "firmware_ref_manifest_sha256" ) c.firmware_ref_manifest_sha256 = v;
			else if ( k == "exclude" )
			{
				const size_t p1 = v.find( '|' ), p2 = p1 == std::string::npos ? std::string::npos : v.find( '|', p1 + 1 );
				if ( p2 == std::string::npos ) fail( fmt( "build config line %zu: exclude = <name> | <pattern>; <pattern> | <reason>", n ) );
				auto trim = []( std::string s ) {
					s.erase( 0, std::min( s.size(), s.find_first_not_of( " \t" ) ) );
					while ( !s.empty() && ( s.back() == ' ' || s.back() == '\t' ) ) s.pop_back();
					return s;
				};
				ToolExclusion x;
				x.name = trim( v.substr( 0, p1 ) );
				x.reason = trim( v.substr( p2 + 1 ) );
				std::istringstream ps( v.substr( p1 + 1, p2 - p1 - 1 ) );
				for ( std::string p; std::getline( ps, p, ';' ); )
					if ( !trim( p ).empty() ) x.patterns.push_back( trim( p ) );
				if ( x.name.empty() || x.patterns.empty() ) fail( fmt( "build config line %zu: an exclusion needs a name and at least one pattern", n ) );
				c.exclusions.push_back( x );
			}
			else if ( k == "repo_root" ) c.repo_root = v;
			else if ( k == "out" ) c.out = v;
			else if ( k == "original_out" ) c.original_out = v;
			else if ( k == "identity_map" ) { if ( v != "identity_map.tsv" ) fail( fmt( "build config line %zu: identity_map is the profile file identity_map.tsv", n ) ); c.identity_map = v; }
			else if ( k == "peer_identity_map" ) c.peer_identity_map = v;
			else if ( k == "seed" || k == "seed_file" ) fail( fmt( "build config line %zu: \"%s\" - the seed was replaced by the identity map (format 3)", n, k.c_str() ) );
			else if ( k == "rules" ) c.rules = RuleSet::parse( v );
			else if ( k.rfind( "resolved.", 0 ) == 0 ) {}	// the local original copy's record of the resolved paths
			else fail( fmt( "build config line %zu: unknown key \"%s\"", n, k.c_str() ) );
		}
		for ( const char* k : { "format", "environment", "capture_parent", "capture_folder", "capture_manifest_sha256", "out", "identity_map", "rules" } )
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
			else if ( bin && n.rfind( "disk_", 0 ) == 0 && n.find( "_descriptor" ) != std::string::npos ) in.disks.push_back( { n, read_file( cap / n ) } );
			else if ( txt ) in.texts.push_back( { n, read_file( cap / n ) } );
		}
		log.info( "capture", fmt( "inputs: rsmb.bin %zu bytes, %zu ACPI table files, %zu EDID values, %zu disk descriptors, %zu text files (manifest entries)", in.rsmb.size(),
			in.acpi.size(), in.edid.size(), in.disks.size(), in.texts.size() ) );

		// the firmware of this capture = the firmware of the reference capture of the same machine (manifest SHA-256 per entry;
		// the reference manifest itself is pinned): rsmb.bin, acpi_*.bin, hwacpi_*.bin, edid_*.bin
		if ( !c.firmware_ref_parent.empty() )
		{
			const fs::path ref = find_one( root / c.firmware_ref_parent, c.firmware_ref_folder, "firmware reference capture" );
			const Bytes rm = read_file( ref / "manifest.txt" );
			if ( sha256_hex( rm ) != c.firmware_ref_manifest_sha256 ) fail( "the firmware reference capture's manifest.txt is not the pinned one" );
			auto entries = []( const Bytes& m ) {
				std::map<std::string, std::string> e;
				for ( const std::string& l : split_lines( m ) )
				{
					std::istringstream is( l );
					unsigned long long size = 0;
					std::string sha, name;
					if ( !l.empty() && l[ 0 ] != '#' && ( is >> size >> sha >> name ) ) e[ name ] = sha;
				}
				return e;
			};
			auto firmware = []( const std::string& n ) {
				const bool bin = n.size() > 4 && n.substr( n.size() - 4 ) == ".bin";
				return n == "rsmb.bin" || ( bin && ( n.rfind( "acpi_", 0 ) == 0 || n.rfind( "hwacpi_", 0 ) == 0 || n.rfind( "edid_", 0 ) == 0 ) );
			};
			const auto a = entries( manifest ), b = entries( rm );
			for ( auto& [ n, sha ] : a )
			{
				if ( !firmware( n ) ) continue;
				in.firmware_ref.compared++;
				auto it = b.find( n );
				if ( it == b.end() || it->second != sha ) fail( "firmware entry " + n + " differs from the firmware reference capture (" + c.firmware_ref_parent + ")" );
				in.firmware_ref.identical++;
			}
			for ( auto& [ n, sha ] : b )
				if ( firmware( n ) && !a.count( n ) ) fail( "the firmware reference capture has the firmware entry " + n + ", this capture lacks it" );
			in.firmware_ref.manifest_sha256 = c.firmware_ref_manifest_sha256;
			log.info( "capture", fmt( "firmware = the reference capture %s\\%s (pinned manifest %s): %zu of %zu firmware entries identical (SHA-256)", c.firmware_ref_parent.c_str(),
				c.firmware_ref_folder.c_str(), c.firmware_ref_manifest_sha256.c_str(), in.firmware_ref.identical, in.firmware_ref.compared ) );
		}

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

	Identity ProfileGenerator::gen_identity( BuildContext& ctx, const CaptureInputs& in )
	{
		Identity p;
		p.env = ctx.config.env;
		p.smbios_original = in.rsmb;
		p.acpi = in.acpi;
		p.edid_original = in.edid;
		// rule 6 first (its serials are presets for the edid.txt occurrences), then the text identity (the synthetic MACs are
		// needed for the UUID-MAC relation), then SMBIOS
		const EdidGen eg = gen_edid( ctx, { in.edid } );
		p.edid_synthetic = eg.synthetic;
		p.edid_ext_blocks = eg.ext_blocks;
		p.edid_ext_hits = eg.ext_hits;
		const std::vector<std::string> usb_table = in.usb_sources.empty() ? std::vector<std::string> {} : parse_usb_serial_table( in.usb_sources );
		// the tool artifacts of the capture setup (BuildConfig "exclude"), then the text identity with the SMBIOS rule-2 value
		// mapping (rule 8c needs the synthetic baseboard serial; the UUID-MAC tail needs the synthetic MACs: SMBIOS again below)
		const ExclusionScan ex = parse_tool_exclusions( in.texts, ctx.config.exclusions );
		const SmbiosGen sg0 = gen_smbios( ctx, { in.rsmb } );
		TextIdentityGen tg = gen_text_identity( ctx, { in.texts, usb_table, eg.presets, ex, sg0.strings } );
		SmbiosGen sg = gen_smbios( ctx, { in.rsmb, ctx.config.rules.r1 && ctx.config.rules.r3 ? tg.macs : std::vector<MacPair> {} } );
		const DiskGen dg = gen_disk_descriptors( ctx, { in.disks, tg.identity } );
		p.smbios_synthetic = sg.synthetic;
		p.disk_original = in.disks;
		p.disk_synthetic = dg.synthetic;
		p.disk_serials = dg.serials;
		p.disk_filler = dg.filler;
		p.disk_none = dg.none;
		p.fields = sg.fields;
		p.smbios_fields = sg.fields.size();
		p.fields.insert( p.fields.end(), eg.fields.begin(), eg.fields.end() );
		p.edid_fields = eg.fields.size();
		p.fields.insert( p.fields.end(), dg.fields.begin(), dg.fields.end() );
		p.disk_fields = dg.fields.size();
		p.fields.insert( p.fields.end(), tg.fields.begin(), tg.fields.end() );
		p.identity = tg.identity;
		p.storage_paths = tg.storage_paths;
		p.macs = tg.macs;
		p.exclusions = ex;
		p.exclusions.ranges.insert( p.exclusions.ranges.end(), tg.extra_exclusions.begin(), tg.extra_exclusions.end() );
		p.excluded_anchors = tg.excluded_anchors;
		p.excluded_rows = tg.excluded_rows;
		p.filler_serials = tg.filler_serials;
		p.volatiles = tg.volatiles;
		p.time_shift = tg.time_shift;
		p.v1_guids = tg.v1_guids;
		p.needles = sg.needles;
		p.needles.insert( p.needles.end(), eg.needles.begin(), eg.needles.end() );
		p.needles.insert( p.needles.end(), dg.needles.begin(), dg.needles.end() );
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
		for ( size_t i = 0; i < p.disk_original.size(); i++ ) check_blob( "disk\\" + p.disk_original[ i ].name, p.disk_original[ i ].bytes, p.disk_synthetic[ i ].bytes );
		// (c) every identity value of an enabled rule changed and kept its length
		for ( const IdentityEntry& r : p.identity )
			if ( r.synthetic.size() != r.original.size() || ( rule_on( ctx.config.rules, r.rule ) && ieq( r.synthetic, r.original ) ) ) fail( "self-check: identity " + r.id + " not replaced at the same length" );
		for ( const StoragePath& sp : p.storage_paths )
			if ( sp.original.size() != sp.synthetic.size() ) fail( "self-check: a storage path changed its length" );
		// (d) the CHID-relevant SMBIOS fields are unchanged
		ChidCheck cc = check_chid_fields( p.smbios_original, p.smbios_synthetic );
		if ( cc.changed ) fail( fmt( "self-check: %zu CHID-relevant SMBIOS field(s) changed - a CHID input became synthetic", cc.changed ) );
		return p;
	}

	GenResult ProfileGenerator::gen_build( BuildContext& ctx, const CaptureInputs& in, const std::string& resolved_root )
	{
		GenResult res;
		res.config = ctx.config;
		BuildLog& log = res.log;
		try
		{
			const Environment env = ctx.config.env;
			Identity& p = res.identity;
			ctx.used_keys.clear();
			ctx.upgraded_keys.clear();
			const size_t loaded = ctx.identity_map.entries.size();
			p = gen_identity( ctx, in );
			// the identity map: application uses every saved value (one left over means the map does not fit this capture)
			if ( ctx.applying )
				for ( auto& [ k, v ] : ctx.identity_map.entries )
					if ( !ctx.used_keys.count( k ) ) fail( "the identity map has a value (" + k + ") this capture does not use - it does not fit; generate a new identity (--new-identity)" );
			res.generated = !ctx.applying;
			res.identity_map = ctx.identity_map;
			const IdentityMap& map = ctx.identity_map;
			const size_t shared_total = size_t( std::count_if( map.entries.begin(), map.entries.end(), [ & ]( auto& e ) { return e.first.rfind( "shared/", 0 ) == 0; } ) );
			const size_t shared_peer = size_t( std::count_if( map.entries.begin(), map.entries.end(), [ & ]( auto& e ) { return e.first.rfind( "shared/", 0 ) == 0 && ctx.peer_map.entries.count( e.first ); } ) );
			log.info( "identity map", ctx.applying ? fmt( "APPLIED: %zu saved values (identity_map.tsv), no randomness drawn", loaded )
				: fmt( "GENERATED: %zu values drawn from RDRAND (Randomize->gen_...; %s), %zu shared value(s) taken from the other environment's map, %zu shared value(s) taken from / recorded in this process's shared/ registry (%zu registered)",
					map.entries.size(), Randomize->cpu_supports_rand ? "CPUID.1:ECX[30] = 1" : "no RDRAND", shared_peer, shared_total - shared_peer, check_shared_values() ) );
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
			if ( in.disks.empty() ) log.info( "disk", "no disk descriptors in this capture (fwid < 1.4): section omitted" );
			else
				log.info( "disk", fmt( "%zu STORAGE_DEVICE_DESCRIPTORs: %zu serials replaced (fieldmap.tsv R rows), %zu placeholder serials exact, %zu without a serial",
					p.disk_original.size(), p.disk_serials, p.disk_filler, p.disk_none ) );
			std::map<std::string, std::pair<size_t, size_t>> per_item;	// ranges, lines
			for ( const ExclusionRange& r : p.exclusions.ranges ) { per_item[ r.item ].first++; per_item[ r.item ].second += r.last_line - r.first_line + 1; }
			std::string excl_summary;
			for ( const ToolExclusion& x : ctx.config.exclusions )
			{
				auto it = per_item.find( x.name );
				excl_summary += ( excl_summary.empty() ? "" : "; " ) + x.name + ": " + ( it == per_item.end() ? std::string( "nothing found" ) : fmt( "%zu range(s), %zu line(s)", it->second.first, it->second.second ) );
			}
			log.info( "exclude", fmt( "%zu tool artifact item(s), %zu text files scanned: %s; %zu pattern hit(s) in absence records (not excluded); %zu identifier anchor(s) and %zu occurrence row(s) inside the excluded ranges dropped",
				ctx.config.exclusions.size(), p.exclusions.scanned_files, excl_summary.empty() ? "-" : excl_summary.c_str(), p.exclusions.absence_records, p.excluded_anchors, p.excluded_rows ) );
			for ( const ExclusionRange& r : p.exclusions.ranges )
				log.info( "exclude", fmt( "%s: %s lines %zu-%zu (%s)", r.item.c_str(), r.file.c_str(), r.first_line, r.last_line, r.unit.c_str() ) );
			log.info( "volatile", fmt( "%zu volatile values (volatile.tsv); %zu disk / HID / USB placeholder serials kept exact", p.volatiles.size(), p.filler_serials ) );
			std::string shift_kinds;
			for ( auto& [ k, n ] : p.time_shift.per_kind ) shift_kinds += ( shift_kinds.empty() ? "" : ", " ) + k + " " + std::to_string( n );
			const std::string shift_text = p.time_shift.shifted
				? fmt( "%zu install-history values (%s) shifted by ONE offset (RDRAND at generation, whole seconds, at least one day earlier; value withheld, only in the original identity map): %zu of %zu exact, so every pairwise difference is the original one; %zu outside the build release .. capture window; %zu precede the OS install in the capture and %zu in the profile (Windows setup)",
					p.time_shift.shifted, shift_kinds.c_str(), p.time_shift.exact, p.time_shift.shifted, p.time_shift.outside_window, p.time_shift.before_install_real, p.time_shift.before_install_synthetic )
				: std::string( "no install history in this capture" );
			log.info( "install shift", shift_text );
			const V1GuidCheck& vc = p.v1_guids;
			const std::string v1_text = vc.guids
				? fmt( "%zu RFC 4122 version-1 GUIDs (rule 4c, U1516): timestamp = real + the install-history offset for %zu of %zu (100-ns units, derived - never drawn); clock sequence synthetic, variant bits and the node rule kept; %zu real timestamps precede the OS install (they stay earlier by the same spacing), %zu follow the capture",
					vc.guids, vc.exact, vc.guids, vc.before_install, vc.after_capture )
				: std::string( "no RFC 4122 version-1 GUID in this capture" );
			log.info( "version-1 guids", v1_text );
			if ( !ctx.upgraded_keys.empty() )
			{
				std::string keys;
				for ( const std::string& k : ctx.upgraded_keys ) keys += ( keys.empty() ? "" : ", " ) + k;
				log.info( "identity map", fmt( "UPGRADE (--upgrade-v1-time, U1516): %zu version-1 GUID value(s) of the saved map re-derived - timestamp = real + the install-history offset, the saved clock sequence and node kept; every other value applied unchanged: %s",
					ctx.upgraded_keys.size(), keys.c_str() ) );
			}
			std::map<std::string, size_t> per_kind;
			for ( const IdentityEntry& r : p.identity ) per_kind[ r.kind ]++;
			std::string kinds;
			for ( auto& [ k, n ] : per_kind ) kinds += ( kinds.empty() ? "" : ", " ) + k + " " + std::to_string( n );
			log.info( "identity", fmt( "%zu identifiers (%s), %zu occurrence + relation rows, %zu storage instance paths", p.identity.size(), kinds.c_str(),
				p.fields.size() - p.smbios_fields - p.edid_fields - p.disk_fields, p.storage_paths.size() ) );
			{
				size_t iid = 0, real_ok = 0, syn_ok = 0;
				for ( const IdentityEntry& r : p.identity )
					if ( r.kind == "win_installation_id" ) { iid++; real_ok += check_installation_id( r.original.c_str() ); syn_ok += check_installation_id( r.synthetic.c_str() ); }
				if ( iid ) log.info( "installation id", fmt( "%zu installation ID(s): check digits valid in %zu real and %zu synthetic (9 groups of 6 digits + (d0 + 2 d1 + d2 + 2 d3 + d4 + 2 d5) mod 7)", iid, real_ok, syn_ok ) );
				if ( syn_ok != iid ) fail( "self-check rule 8a: a synthetic installation ID fails its check digits" );
			}
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
			log.info( "capture", "folder <capture root>\\" + ctx.config.capture_parent + "\\" + cap_name );

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
			prov.emplace_back( "provenance.capture", ctx.config.capture_parent + "\\" + cap_name + " (relative to the capture root; the computer-name part of the folder name is withheld)" );
			prov.emplace_back( "provenance.capture_tool", tool + " (capture manifest header)" );
			prov.emplace_back( "provenance.capture_manifest_sha256", in.manifest_sha256 );
			prov.emplace_back( "provenance.capture_check", fmt( "%zu of %zu manifest entries match (size + SHA-256); manifest SHA-256 = the pinned clean capture", in.manifest_entries, in.manifest_entries ) );
			int ci = 0;
			for ( auto& [ rel, sha ] : in.companions ) prov.emplace_back( fmt( "provenance.companion_%d", ++ci ), rel + " sha256 " + sha + " (frozen hash list of the same state; not consumed)" );
			prov.emplace_back( "provenance.peer_capture", ctx.config.peer_capture_parent + "\\" + ctx.config.peer_capture_folder + " manifest " + ctx.config.peer_capture_manifest_sha256 + " (enum_registry.txt only: the shared USB serial table)" );
			if ( !ctx.config.firmware_ref_parent.empty() )
				prov.emplace_back( "provenance.firmware_reference", ctx.config.firmware_ref_parent + "\\" + ctx.config.firmware_ref_folder + " manifest " + ctx.config.firmware_ref_manifest_sha256 +
					fmt( ": %zu of %zu firmware entries (rsmb / acpi / hwacpi / edid) byte-identical (SHA-256)", in.firmware_ref.identical, in.firmware_ref.compared ) );
			prov.emplace_back( "build_config", std::string( BuildConfig::k_file ) + " (this folder; rebuild: emu-profilegen --config " + BuildConfig::k_file + " - applies identity_map.tsv)" );
			prov.emplace_back( "identity_map", fmt( "identity_map.tsv (this folder): %zu synthetic values, drawn once from RDRAND at generation; every build applies them", ctx.identity_map.entries.size() ) );
			prov.emplace_back( "rules", ctx.config.rules.to_string() );
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
			prov.emplace_back( "disk", in.disks.empty() ? std::string( "not in this capture" ) : fmt( "%zu STORAGE_DEVICE_DESCRIPTORs (disk\\): %zu serials replaced (fieldmap.tsv R rows), %zu placeholder serials exact, %zu without a serial",
				p.disk_original.size(), p.disk_serials, p.disk_filler, p.disk_none ) );
			prov.emplace_back( "identity", fmt( "%zu identifiers, %zu occurrence + relation rows (identity.tsv, fieldmap.tsv)", p.identity.size(), p.fields.size() - p.smbios_fields - p.edid_fields - p.disk_fields ) );
			prov.emplace_back( "exclusions", fmt( "%zu range(s) of tool artifacts not part of the profile (exclusions.tsv): %s; %zu identifier anchor(s), %zu occurrence row(s) inside them dropped",
				p.exclusions.ranges.size(), excl_summary.empty() ? "-" : excl_summary.c_str(), p.excluded_anchors, p.excluded_rows ) );
			prov.emplace_back( "volatile", fmt( "%zu values (volatile.tsv): fwid's [volatile] marks and the DiagTrack counters / timestamps", p.volatiles.size() ) );
			prov.emplace_back( "install_shift", shift_text );
			prov.emplace_back( "v1_guids", v1_text );

			// the BuildConfig copies: synthetic = the configuration as used (portable); original adds the resolved
			// capture root and folder name (local only)
			std::string cfg_syn = ctx.config.save();
			std::string cfg_org = cfg_syn + "# resolved (LOCAL ONLY copy, ignored by load):\nresolved.capture_root   = " + ( resolved_root.empty() ? "-" : resolved_root ) +
								  "\nresolved.capture_folder = " + in.folder_name + "\n";
			res.synthetic = make_files( p, false, prov, cfg_syn, save_identity_map( ctx.identity_map, env, false ) );
			res.original = make_files( p, true, prov, cfg_org, save_identity_map( ctx.identity_map, env, true ) );

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
							( ctx.config.rules == BuildConfig::defaults( ctx.config.env ).rules ? "" : " (a field rule is disabled)" ) );
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

	// the convenience form: one new context of the default configuration per call (pg_main uses its own context)
	GenResult ProfileGenerator::run_build( bool check )
	{
		BuildContext ctx = new_context();
		return run_build( ctx, check );
	}
	GenResult ProfileGenerator::run_build( BuildContext& ctx, bool check )
	{
		GenResult res;
		BuildLog pre;
		try
		{
			BuildConfig c = ctx.config;
			const char* envn = env_dir_name( c.env );
			pre.info( "start", std::string( k_tool ) + "; environment " + envn + " (" + env_switch( c.env ) + ")" + ( check ? "; check mode (nothing is written)" : "" ) );
			const BuildConfig& pin = BuildConfig::pinned( c.env );
			const BuildConfig& peer = BuildConfig::pinned( c.env == Environment::vmware_guest ? Environment::bare_host : Environment::vmware_guest );
			if ( c.capture_manifest_sha256 != pin.capture_manifest_sha256 || c.companions != pin.companions || c.capture_parent != pin.capture_parent ||
				 c.firmware_ref_parent != pin.firmware_ref_parent || c.firmware_ref_folder != pin.firmware_ref_folder || c.firmware_ref_manifest_sha256 != pin.firmware_ref_manifest_sha256 )
				fail( std::string( "the build config does not name the pinned clean capture of " ) + envn + " (--guest is built only from the clean guest without VmLoader, --host only from the clean host run)" );
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
			const fs::path out = in_repo( c.out ), own_map_file = out / c.identity_map;
			const fs::path peer_map_file = c.peer_identity_map.empty() ? fs::path() : in_repo( c.peer_identity_map );

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
			pre.info( "paths", "capture root " + show( root ) + " (" + root_from + "); synthetic out " + show( out ) + "; identity map " + show( own_map_file ) + "; peer identity map " + ( peer_map_file.empty() ? std::string( "-" ) : show( peer_map_file ) ) );
			fs::path orig;
			if ( c.original_out != "none" && !c.original_out.empty() )
			{
				fs::path x = expand( c.original_out );
				orig = x.is_absolute() ? x : root / x;
			}
			const fs::path cap = find_one( root / c.capture_parent, c.capture_folder, "capture" );
			CaptureInputs in = load_capture( cap, root, c, pre );
			// the identity: APPLY the saved map (identity_map.tsv of the profile) unless a new identity was asked for or none exists
			// yet (then GENERATE every value from RDRAND); the other environment's map provides the shared USB serials
			// both maps are read ONCE here (U1515): the context holds the snapshot, nothing reads the files again; a peer profile
			// that another build is switching right now yields its complete previous or new map (load_identity_map)
			auto read_map = [ & ]( const fs::path& f, const char* what ) {
				IdentityMapFile m = load_identity_map( f );
				if ( m.found && m.map.has_install_shift ) fail( std::string( what ) + " identity map holds the install-history offset: an ORIGINAL copy must never be in the repository" );
				return m;
			};
			IdentityMap own, peer_map;
			if ( !peer_map_file.empty() )
			{
				const IdentityMapFile pm = read_map( peer_map_file, "the peer" );
				peer_map = pm.map;
				pre.info( "identity", pm.found ? fmt( "peer identity map read once at the start (a snapshot of %zu values; %s, %zu read attempt(s))", pm.map.entries.size(), pm.source.c_str(), pm.attempts )
											   : std::string( "no peer identity map (the other environment has no profile yet)" ) );
			}
			if ( !ctx.fresh )
			{
				const IdentityMapFile om = read_map( own_map_file, "this profile's" );
				if ( om.found && om.map.entries.empty() ) fail( "the identity map " + show( own_map_file ) + " is empty" );
				if ( !om.found && check ) fail( "check: no identity map " + show( own_map_file ) + " to apply" );
				own = om.map;
			}
			else if ( check ) fail( "check: no identity map " + show( own_map_file ) + " to apply" );
			load_identity_maps( ctx, own, peer_map );
			ctx.fresh = false;
			if ( ctx.upgrade_v1_time && ( check || !ctx.applying ) ) fail( "--upgrade-v1-time upgrades a saved identity map while writing the profile: not in check mode, not without a saved map" );
			pre.info( "identity", own.entries.empty() ? std::string( "GENERATION: a new identity, every value from RDRAND (the shared USB serials from the peer map when it has them)" )
				: fmt( "APPLICATION of the saved identity map (%zu values)", own.entries.size() ) );

			// the configuration as saved next to the outputs: portable (roots "auto", paths relative to the repository / capture
			// root), so the synthetic copy holds no user-specific absolute path
			BuildConfig cp = c;
			cp.tool = k_tool;
			cp.capture_root = "auto";
			cp.repo_root = "auto";
			cp.out = repo.empty() ? show( out ) : portable( out, repo );
			if ( !peer_map_file.empty() ) cp.peer_identity_map = repo.empty() ? show( peer_map_file ) : portable( peer_map_file, repo );
			cp.original_out = orig.empty() ? "none" : portable( orig, root );
			if ( c.capture_root != "auto" ) pre.info( "paths", "the saved build config records capture_root = auto (the root given here is a local path)" );
			ctx.config = cp;

			res = gen_build( ctx, in, root.string() );
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
				log.info( "check", fmt( "%zu files of %s are byte-identical to the regeneration (the same build config and identity map applied to the same capture)", res.synthetic.size(), show( out ).c_str() ) );
				return res;
			}
			if ( !orig.empty() )
			{
				if ( fs::weakly_canonical( fs::absolute( orig ) ) == fs::weakly_canonical( fs::absolute( out ) ) ) fail( "original_out must differ from out" );
				fs::path wt = check_git_work_tree( orig );
				if ( !wt.empty() ) fail( "refusing to write the ORIGINAL profile (real identifiers) inside a git work tree: " + show( orig ) + " is under " + show( wt ) );
			}
			auto saved = [ & ]( const char* what, const ProfileSaveResult& s ) {
				log.info( "write", fmt( "%s %s (%zu files): written into %s.partial, switched into place by rename%s", what, show( s.dir ).c_str(), s.files, s.dir.filename().string().c_str(),
					!s.replaced ? " (new folder)" : s.old_removed ? fmt( "; the previous profile kept as .old until the switch, then its %zu listed files removed", s.old_files_removed ).c_str() : "" ) );
				if ( !s.note.empty() ) log.warning( "write", s.note );
			};
			saved( "SYNTHETIC profile", save_profile( out, res.synthetic, false ) );
			if ( !orig.empty() )
			{
				saved( "ORIGINAL profile (LOCAL ONLY, real values)", save_profile( orig, res.original, true ) );
				res.original_out = orig;
			}
			else log.info( "write", "original profile: not written (original_out = none)" );
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
