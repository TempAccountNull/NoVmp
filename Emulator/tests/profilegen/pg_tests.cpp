// pg_tests.cpp - unit tests of ProfileGenerator (plan 1.J.49, ledger U1511; fwid 1.4 formats, disk descriptors, tool
// exclusions, install-history shift, RDRAND Randomizer, identity map, singleton, per-build context + threading (plan
// 1.J.49.a): U1513-U1514), suite "pg_unit" in test.cmd.
// Every input is a SMALL SYNTHETIC FIXTURE built here (made-up vendor strings, serials, MACs, GUIDs, SIDs, EDIDs); no real
// capture is read. Call sites follow the user's style rule (design: the user's rid demo): the singleton is reached as
// ProfileGen->gen_<what>( ctx, struct ) -> struct with the build's BuildContext; each case starts from ProfileGen->reset() +
// ProfileGen->load_config( cfg ) and a new context (ctx = pg_fresh( env )); every random value comes from Randomize-> (RDRAND).
// (a) only the decided SMBIOS bytes change, (b) lengths / structure layout preserved, (c) a saved identity map applied again
// -> identical output, a new identity -> other values,
// (d) filler fields untouched, (e) MAC OUI preserved, (f) the original profile is refused inside a git work tree; plus the
// guard for undecided fields, the identity rules 4a-4d, rule 2h (host only), the BuildConfig round trip (save -> load ->
// rebuild = byte-identical), the UUID-MAC relation, the shared USB table, the MAC relations (EUI-64), version-1 GUID nodes,
// EDID (checksum, one serial per monitor), ProductId groups, SID / NameSamCompatible / install-instant consistency, volume
// GUID consistency, the fwid 1.4 rules in fwid_probe 1.4's formats (installation IDs per license row, SusClientId and the
// layout-kept SusClientIdValidation, disk serials incl. the USB-prefix relation, HID / GPU / Bluetooth / audio / Wi-Fi / WSD
// port, volatile values), the CHID check, a new identity (fresh RDRAND values: every mapped field differs, every relation still
// holds), the software-device GUIDs, the disk descriptors (rule 8d) and the tool exclusions (BuildConfig "exclude"); threading:
// the Randomizer from 8 threads (formats, check digits, thread-local result buffers), guest + host built in parallel threads
// from saved maps (byte-identical to serial builds), and both generated concurrently (one value per shared/ key, every
// returned const char* still valid after its context is gone). P4: atomic profile writes in parallel (pg_atomic_writes,
// U1515), version-1 GUID timestamps (pg_v1_guid_time, U1516), the synthetic-only field view and single-value edits
// (pg_field_view, U1517), the synthetic text views (pg_views, U1518). Every text fixture carries an install history
// (k_pg_install_os_ids): a version-1 GUID needs the rule 7b offset.
#include "profile_generator.hpp"
#include "pg_randomize.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cctype>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>

#include "acutest.h"

namespace fs = std::filesystem;
using namespace emu::platform;

// ── pg_ block (ledger U1511) ─────────────────────────────────────────────────────────────────────────────────────────────
struct pg_struct
{
	uint8_t type;
	uint16_t handle;
	std::vector<uint8_t> tail;	// formatted area after the 4-byte header
	std::vector<std::string> strings;
};

static Bytes pg_build( const std::vector<pg_struct>& v )
{
	Bytes t;
	for ( const pg_struct& s : v )
	{
		t.push_back( s.type );
		t.push_back( uint8_t( 4 + s.tail.size() ) );
		t.push_back( uint8_t( s.handle ) );
		t.push_back( uint8_t( s.handle >> 8 ) );
		t.insert( t.end(), s.tail.begin(), s.tail.end() );
		if ( s.strings.empty() ) { t.push_back( 0 ); t.push_back( 0 ); continue; }
		for ( const std::string& x : s.strings ) { t.insert( t.end(), x.begin(), x.end() ); t.push_back( 0 ); }
		t.push_back( 0 );
	}
	Bytes raw = { 0, 3, 6, 0, uint8_t( t.size() ), uint8_t( t.size() >> 8 ), 0, 0 };
	raw.insert( raw.end(), t.begin(), t.end() );
	return raw;
}

static std::vector<uint8_t> pg_tail( size_t len, std::initializer_list<std::pair<size_t, uint8_t>> set )
{
	std::vector<uint8_t> t( len - 4, 0 );
	for ( auto [ off, v ] : set ) t[ off - 4 ] = v;
	return t;
}

// the SMBIOS fixture: type 1 (UUID; serial / SKU filler), type 2 (real-looking serial, filler asset tag / location), type 3
// (filler serial / asset tag), type 17 populated (serial + asset tag), type 17 empty slot with an unreferenced copy of the
// asset tag (rule 2o), type 17 with filler serial (shared with the manufacturer string) and asset tag, type 127
static const uint8_t k_pg_uuid_vm[ 16 ] = { 0x56, 0x4D, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA };
static const uint8_t k_pg_uuid_rfc[ 16 ] = { 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0x1C, 0x4E, 0x8F, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
static Bytes pg_fixture( bool vmware, const char* chassis_serial = "None" )
{
	std::vector<uint8_t> t1 = pg_tail( 0x1B, { { 0x04, 1 }, { 0x05, 2 }, { 0x06, 3 }, { 0x07, 4 }, { 0x18, 6 }, { 0x19, 5 }, { 0x1A, 6 } } );
	memcpy( &t1[ 0x08 - 4 ], vmware ? k_pg_uuid_vm : k_pg_uuid_rfc, 16 );
	return pg_build( {
		{ 1, 0x0001, t1, { "Fixture Maker Inc.", "FX-1000", "1.0", "Default string", "To Be Filled By O.E.M.", "Fixture Family" } },
		{ 2, 0x0002, pg_tail( 0x0F, { { 0x04, 1 }, { 0x05, 2 }, { 0x06, 3 }, { 0x07, 4 }, { 0x08, 5 }, { 0x09, 0x09 }, { 0x0A, 6 }, { 0x0B, 3 }, { 0x0D, 0x0A } } ),
			{ "Fixture Maker Inc.", "FXB-2", "2.0", "AB12cd-345_Z", "No Asset Tag", "None" } },
		{ 3, 0x0003, pg_tail( 0x09, { { 0x04, 1 }, { 0x05, 3 }, { 0x06, 2 }, { 0x07, 3 }, { 0x08, 4 } } ), { "Fixture Maker Inc.", "N/A", chassis_serial, "No Asset Tag" } },
		{ 17, 0x0011, pg_tail( 0x28, { { 0x10, 1 }, { 0x11, 2 }, { 0x12, 0x1A }, { 0x17, 3 }, { 0x18, 4 }, { 0x19, 5 }, { 0x1A, 6 }, { 0x1B, 1 } } ),
			{ "DIMM 0", "BANK 5", "FixtureRAM", "1234ABCD", "AT-0042", "FXR-16G  " } },
		{ 17, 0x0012, pg_tail( 0x28, { { 0x10, 1 }, { 0x11, 2 }, { 0x12, 0x02 } } ), { "DIMM 1", "BANK 5", "AT-0042" } },
		{ 17, 0x0013, pg_tail( 0x28, { { 0x10, 1 }, { 0x11, 2 }, { 0x17, 3 }, { 0x18, 3 }, { 0x19, 4 } } ), { "DIMM 2", "BANK 6", "Not Specified", "None" } },
		{ 127, 0xFEFF, {}, {} } } );
}

static BuildConfig pg_cfg( Environment e )
{
	BuildConfig c = BuildConfig::defaults( e );
	c.original_out = "none";
	return c;
}
// a test case starts from the initial state: a new context of e's configuration, no identity loaded (generation: every
// value fresh from RDRAND), the shared/ registry of the process emptied (a new identity session)
static BuildContext pg_fresh( Environment e )
{
	ProfileGen->reset();
	ProfileGen->load_config( pg_cfg( e ) );
	return ProfileGen->new_context();
}

static std::string pg_str( const Bytes& b, const SmbiosString& s ) { return std::string( b.begin() + s.offset, b.begin() + s.offset + s.length ); }

// (a) only the decided bytes change: every differing byte lies inside a field-map range, every range changed, and the
// ranges are exactly the decided locations of the fixture (host: also the rule-2h type 17 name strings)
static void pg_smbios_decided_bytes_only( void )
{
	BuildContext ctx;
	for ( bool vm : { true, false } )
	{
		ctx = pg_fresh( vm ? Environment::vmware_guest : Environment::bare_host );
		Bytes raw = pg_fixture( vm );
		SmbiosGen g = ProfileGen->gen_smbios( ctx, { raw } );
		const Bytes& out = g.synthetic;
		TEST_ASSERT( out.size() == raw.size() );
		TEST_CHECK_( g.fields.size() == ( vm ? 5u : 13u ), "replaced fields: %zu", g.fields.size() );
		std::vector<uint8_t> mapped( raw.size(), 0 );
		for ( const FieldMapEntry& r : g.fields )
		{
			TEST_CHECK( r.area == "smbios.bin" );
			for ( size_t i = 0; i < r.length; i++ ) mapped[ r.offset + i ] = 1;
			TEST_CHECK_( !std::equal( raw.begin() + r.offset, raw.begin() + r.offset + r.length, out.begin() + r.offset ), "field %s changed", r.id.c_str() );
		}
		size_t diffs = 0;
		for ( const ByteRange& r : ProfileGenerator::check_diff_ranges( raw, out ) )
			for ( size_t i = r.first; i < r.last; i++ ) { TEST_CHECK_( mapped[ i ] == 1, "byte 0x%04zX changed outside the field map", i ); diffs++; }
		TEST_CHECK( diffs > 0 );
		SmbiosTable t = parse_smbios( raw );
		std::set<std::pair<uint64_t, uint32_t>> want = { { t.structures[ 0 ].offset + 8, 16 }, { t.structures[ 1 ].strings[ 3 ].offset, 12 },
			{ t.structures[ 3 ].strings[ 3 ].offset, 8 }, { t.structures[ 3 ].strings[ 4 ].offset, 7 }, { t.structures[ 4 ].strings[ 2 ].offset, 7 } };
		if ( !vm )	// rule 2h: DeviceLocator / BankLocator of all three type 17, Manufacturer + PartNumber of h0011 (h0013's are filler)
			for ( auto [ si, k ] : { std::pair { 3, 0 }, { 3, 1 }, { 3, 2 }, { 3, 5 }, { 4, 0 }, { 4, 1 }, { 5, 0 }, { 5, 1 } } )
				want.insert( { t.structures[ si ].strings[ k ].offset, uint32_t( t.structures[ si ].strings[ k ].length ) } );
		std::set<std::pair<uint64_t, uint32_t>> got;
		for ( const FieldMapEntry& r : g.fields ) got.insert( { r.offset, r.length } );
		TEST_CHECK( got == want );
		TEST_CHECK( g.fields[ 0 ].rule == ( vm ? "1-vmware" : "1-rfc4122" ) );
		TEST_CHECK( g.fields.back().rule == "2o" );
		TEST_CHECK( pg_str( out, t.structures[ 3 ].strings[ 4 ] ) == pg_str( out, t.structures[ 4 ].strings[ 2 ] ) );	// the orphan copy follows its value
	}
}

// (b) lengths and structure layout preserved; character classes per position; UUID layout bits kept
static void pg_smbios_layout_preserved( void )
{
	BuildContext ctx;
	for ( bool vm : { true, false } )
	{
		ctx = pg_fresh( vm ? Environment::vmware_guest : Environment::bare_host );
		Bytes raw = pg_fixture( vm );
		SmbiosGen g = ProfileGen->gen_smbios( ctx, { raw } );
		const Bytes& out = g.synthetic;
		SmbiosTable a = parse_smbios( raw ), b = parse_smbios( out );
		TEST_ASSERT( a.structures.size() == b.structures.size() );
		TEST_CHECK( a.length == b.length && a.major == b.major && a.minor == b.minor );
		for ( size_t i = 0; i < a.structures.size(); i++ )
		{
			const auto &x = a.structures[ i ], &y = b.structures[ i ];
			TEST_CHECK( x.offset == y.offset && x.type == y.type && x.length == y.length && x.handle == y.handle && x.end == y.end );
			TEST_ASSERT( x.strings.size() == y.strings.size() );
			for ( size_t k = 0; k < x.strings.size(); k++ )
			{
				TEST_CHECK( x.strings[ k ].offset == y.strings[ k ].offset && x.strings[ k ].length == y.strings[ k ].length );
				std::string s0 = pg_str( raw, x.strings[ k ] ), s1 = pg_str( out, y.strings[ k ] );
				for ( size_t c = 0; c < s0.size(); c++ )
				{
					uint8_t p = uint8_t( s0[ c ] ), q = uint8_t( s1[ c ] );
					bool same_class = ( isdigit( p ) && isdigit( q ) ) || ( isupper( p ) && isupper( q ) ) || ( islower( p ) && islower( q ) ) || p == q;
					TEST_CHECK_( same_class, "structure %zu string %zu position %zu keeps its character class", i, k + 1, c );
					if ( !isalnum( p ) ) TEST_CHECK( p == q );
				}
			}
		}
		const uint8_t* u0 = &raw[ a.structures[ 0 ].offset + 8 ];
		const uint8_t* u1 = &out[ a.structures[ 0 ].offset + 8 ];
		if ( vm ) TEST_CHECK( u1[ 0 ] == 0x56 && u1[ 1 ] == 0x4D );
		else TEST_CHECK( ( u1[ 6 ] >> 4 ) == ( u0[ 6 ] >> 4 ) && ( u1[ 7 ] >> 4 ) == ( u0[ 7 ] >> 4 ) && ( u1[ 8 ] >> 6 ) == ( u0[ 8 ] >> 6 ) );
		TEST_CHECK( g.binary_needles.size() == 1 && g.binary_needles[ 0 ] == Bytes( u0, u0 + 16 ) );
		std::string os( out.begin(), out.end() );
		for ( const char* real : { "AB12cd-345_Z", "1234ABCD", "AT-0042" } )
		{
			TEST_CHECK_( std::find( g.needles.begin(), g.needles.end(), std::string( real ) ) != g.needles.end(), "%s collected", real );
			TEST_CHECK_( os.find( real ) == std::string::npos, "%s no longer in the output", real );
		}
		std::string uh;
		for ( int i = 0; i < 16; i++ ) { char h[ 3 ]; snprintf( h, sizeof h, "%02x", u0[ i ] ); uh += h; }
		SmbiosUuid ru;
		ru.handle = 1;
		ru.hex = uh.c_str();
		SmbiosUuid su = ProfileGen->gen_system_uuid( ctx, ru );	// cached: the value gen_smbios already took for handle 1
		const Bytes sb = bytes_of_hex( su.hex );
		TEST_CHECK( std::equal( sb.begin(), sb.end(), u1 ) && sb != Bytes( u0, u0 + 16 ) && ( !vm || ( sb[ 0 ] == 0x56 && sb[ 1 ] == 0x4D ) ) );
	}
}

static void pg_text_fixture( std::vector<TextFile>& files );
static std::vector<TextFile> pg_text_fixture2( const std::string& usb_lines );

// (c) generation vs application: a saved identity map applied again gives identical output (SMBIOS and identity) without
// drawing anything; a new identity (fresh RDRAND values) gives other values; a map that does not fit stops the build
static void pg_identity_map_replay( void )
{
	BuildContext ctx;
	TEST_CHECK_( Randomize->cpu_supports_rand, "RDRAND (CPUID.1:ECX[30]) on the test machine" );
	const uint64_t r1 = Randomize->gen_u64(), r2 = Randomize->gen_u64();
	TEST_CHECK( r1 != 0 && r1 != ~0ull && r2 != 0 && r2 != ~0ull && r1 != r2 );
	std::vector<TextFile> files;
	pg_text_fixture( files );
	auto ident = [ & ]() {
		Bytes s = ProfileGen->gen_smbios( ctx, { pg_fixture( false ) } ).synthetic;
		TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files } );
		std::string o( s.begin(), s.end() );
		for ( auto& r : t.identity ) o += r.synthetic + "|";
		for ( auto& p : t.storage_paths ) o += p.synthetic + "|";
		return o;
	};
	ctx = pg_fresh( Environment::bare_host );
	const std::string first = ident();
	const IdentityMap saved = ctx.identity_map;
	TEST_CHECK( !saved.entries.empty() && !ctx.applying );
	const IdentityMap parsed = ProfileGenerator::parse_identity_map( ProfileGenerator::save_identity_map( saved, Environment::bare_host, false ) );
	TEST_CHECK( parsed.entries == saved.entries && !parsed.has_install_shift );	// identity_map.tsv round trip
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->load_identity_maps( ctx, parsed, {} );
	TEST_CHECK( ctx.applying );
	TEST_CHECK_( ident() == first, "the applied map reproduces every value" );
	TEST_CHECK( ctx.identity_map.entries == saved.entries );
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->gen_new_identity( ctx );
	TEST_CHECK_( ident() != first, "a new identity: other values" );
	// a map that does not fit (a missing key) stops the build: application never draws randomness
	IdentityMap partial = saved;
	partial.entries.erase( partial.entries.begin() );
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->load_identity_maps( ctx, partial, {} );
	bool threw = false;
	try { ident(); } catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "identity map has no value" ) != std::string::npos; }
	TEST_CHECK( threw );
	// the repository copy never carries the install-history offset; the original copy does
	IdentityMap withshift = saved;
	withshift.has_install_shift = true;
	withshift.install_shift = -123456;
	TEST_CHECK( ProfileGenerator::save_identity_map( withshift, Environment::bare_host, false ).find( "install_shift" ) == std::string::npos );
	const IdentityMap orig = ProfileGenerator::parse_identity_map( ProfileGenerator::save_identity_map( withshift, Environment::bare_host, true ) );
	TEST_CHECK( orig.has_install_shift && orig.install_shift == -123456 );
}

// (d) filler fields untouched
static void pg_filler_untouched( void )
{
	BuildContext ctx;
	// one repeated character (length >= 2) is filler too (user decision J 2026-10-16: the repeated-digit bridge disk serial)
	for ( const char* f : { "Default string", "To Be Filled By O.E.M.", "None", "No Asset Tag", "N/A", "Not Specified", "", "to be filled by o.e.m.", "None  ",
			  "Unknown", "NO DIMM", "00000000", "9876543210", "9876543210  ", "000000000", "22222222222222222222", "FFFF", "aA" } )
		TEST_CHECK_( ProfileGenerator::check_filler( f ), "\"%s\" is filler", f );
	for ( const char* f : { "AT-0042", "0", "7", "Default", "N/A1", "00000017", "13572468", "98765432100", "AAAB" } ) TEST_CHECK_( !ProfileGenerator::check_filler( f ), "\"%s\" is not filler", f );
	TEST_CHECK( ProfileGenerator::check_usb_placeholder( "0" ) && ProfileGenerator::check_usb_placeholder( "1111" ) && !ProfileGenerator::check_usb_placeholder( "1" ) &&
				!ProfileGenerator::check_usb_placeholder( "1112" ) );
	std::vector<uint8_t> t17 = pg_tail( 0x28, { { 0x10, 1 }, { 0x11, 2 }, { 0x17, 3 }, { 0x18, 4 }, { 0x19, 5 }, { 0x1A, 3 } } );
	Bytes raw = pg_build( { { 2, 0x0002, pg_tail( 0x0F, { { 0x04, 1 }, { 0x07, 2 }, { 0x08, 3 } } ), { "Fixture Maker Inc.", "Default string", "To Be Filled By O.E.M." } },
		{ 17, 0x0011, t17, { "DIMM 0", "BANK 5", "Not Specified", "None", "No Asset Tag" } }, { 127, 0xFEFF, {}, {} } } );
	ctx = pg_fresh( Environment::vmware_guest );
	SmbiosGen g = ProfileGen->gen_smbios( ctx, { raw } );
	TEST_CHECK( g.synthetic == raw );
	TEST_CHECK( g.fields.empty() && g.needles.empty() && g.binary_needles.empty() );
	Bytes fx = pg_fixture( true );
	Bytes o2 = ProfileGen->gen_smbios( ctx, { fx } ).synthetic;
	SmbiosTable t = parse_smbios( fx );
	for ( const SmbiosStructure& s : t.structures )
		for ( const SmbiosString& x : s.strings )
			if ( ProfileGenerator::check_filler( pg_str( fx, x ) ) ) TEST_CHECK_( pg_str( o2, x ) == pg_str( fx, x ), "filler string of handle 0x%04X unchanged", s.handle );
}

// the guard: a real value in an undecided identifier field (here the chassis serial) stops the run
static void pg_guard_undecided( void )
{
	BuildContext ctx;
	ctx = pg_fresh( Environment::vmware_guest );
	bool threw = false;
	try { ProfileGen->gen_smbios( ctx, { pg_fixture( true, "CH-998877" ) } ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "undecided identifier field" ) != std::string::npos; }
	TEST_CHECK( threw );
	Bytes bad = pg_fixture( true );
	bad[ 4 ]++;
	threw = false;
	try { parse_smbios( bad ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
}

// the install history every text fixture carries (U1516: an RFC 4122 version-1 GUID's timestamp moves by the rule 7b
// install-history offset, so a capture with a version-1 GUID needs one): os_ids.txt with InstallDate / InstallTime only
// (made-up values, the same as pg_os_ids); the capture_info.txt lines give the build release .. capture window
static const char* const k_pg_install_os_ids =
	"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"InstallDate\" REG_DWORD, 4 bytes [machine-specific]: 0x64A0B1C2 (1688252866)\n"
	"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"InstallTime\" REG_QWORD, 8 bytes [machine-specific]: 0x01D9AC70D901E387 (133327264661234567)\n";
// replaces the text file name of files (or adds it)
static void pg_put( std::vector<TextFile>& files, const char* name, const std::string& body )
{
	for ( TextFile& f : files )
		if ( f.name == name ) { f.data = Bytes( body.begin(), body.end() ); return; }
	files.push_back( { name, Bytes( body.begin(), body.end() ) } );
}

static void pg_text_fixture( std::vector<TextFile>& files )
{
	auto tf = []( const char* name, const std::string& s ) { return TextFile { name, Bytes( s.begin(), s.end() ) }; };
	files = {
		tf( "capture_info.txt", "tool.version            = fixture\n"
								"os.computer_name        = NetBIOS \"DESKTOP-FIXTURE\", DNS host \"DESKTOP-FIXTURE\", DNS domain \"\"\n"
								"run.output_folder       = X:\\out\\DESKTOP-FIXTURE_20260101_000000\n"
								"not.a.match             = DESKTOP-FIXTUREX\n"
								"run.start_utc           = 2026-01-01T00:00:00.000Z\n"
								"os.registry             = ProductName \"Windows 10 Pro\", EditionID \"Professional\", DisplayVersion \"22H2\", CurrentBuild 19045, UBR 1\n" ),
		tf( "network_adapters.txt", "=== adapter 0\nAdapterName: \"{11111111-2222-4333-8444-555555555555}\"\nDescription: \"Fixture Virtual Ethernet Adapter\"\n"
									"MAC (6 bytes): 00:50:56:3F:1D:42  [VMware OUI]\n"
									"=== adapter 1\nAdapterName: \"{AAAAAAAA-BBBB-1CCC-9DDD-EEEEEEEEEEEE}\"\nDescription: \"Fixture Wired Ethernet\"\nMAC (6 bytes): 00:11:22:33:44:55\n"
									"=== adapter 2\nAdapterName: \"{12345678-9ABC-4DEF-A012-3456789ABCDE}\"\nDescription: \"Fixture Wired Ethernet-Filter-0000\"\n"
									"MAC (6 bytes): 00:11:22:33:44:55\nMAC (0 bytes): \n" ),
		tf( "enum_registry.txt", "########## Enum\\SCSI\n"
							   "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SCSI\\Disk&Ven_Fixture&Prod_Disk_1\n"
							   "  subkey \"5&1a2b3c4d&0&000000\"\n"
							   "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SCSI\\Disk&Ven_Fixture&Prod_Disk_1\\5&1a2b3c4d&0&000000\n"
							   "  value \"ContainerID\"  REG_SZ (1)  78 bytes: \"{00000000-0000-0000-ffff-ffffffffffff}\"\n"
							   "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\STORAGE\\Volume\\{abcdef01-2345-11f0-89ab-806e6f6e6963}#0000000000200000\n"
							   "  value \"ContainerID\"  REG_SZ (1)  78 bytes: \"{deadbeef-0000-11f0-a000-806e6f6e6963}\"\n"
							   "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\STORAGE\\VolumeSnapshot\\HarddiskVolumeSnapshot1\n" ),
		tf( "pci_devices.txt", "  SPDRP_BASE_CONTAINERID             REG_SZ \"{deadbeef-0000-11f0-a000-806e6f6e6963}\"\n"
							 "    {8C7ED206-3F8A-4827-B3AB-AE9E1FAEFC6C} pid 2 DEVPKEY_Device_ContainerId [IDENTIFIER - keep local] type 0xD: {DEADBEEF-0000-11F0-A000-806E6F6E6963}\n"
							 "  instance SCSI\\DISK&VEN_FIXTURE&PROD_DISK_1\\5&1A2B3C4D&0&000000\n" ),
		tf( "os_ids.txt", k_pg_install_os_ids ) };
}

static const IdentityEntry* pg_find( const std::vector<IdentityEntry>& v, const std::string& kind, const std::string& original )
{
	for ( const IdentityEntry& r : v )
		if ( r.kind == kind && _stricmp( r.original.c_str(), original.c_str() ) == 0 ) return &r;
	return nullptr;
}

// (e) MAC OUI preserved (also the VMware OUI 00:50:56); one identity per distinct MAC, every occurrence mapped
static void pg_mac_oui( void )
{
	BuildContext ctx;
	ctx = pg_fresh( Environment::bare_host );
	for ( const char* mac : { "00:50:56:2E:9B:17", "00:04:0F:12:34:56", "FC:34:97:AB:CD:EF", "00:0C:29:00:00:00" } )
	{
		const std::string o = ProfileGen->gen_mac( ctx, { mac, "Fixture", std::string( "T" ) + mac } ).text;
		TEST_CHECK_( o.size() == 17 && o.substr( 0, 9 ) == std::string( mac ).substr( 0, 9 ), "OUI of %s kept", mac );
		TEST_CHECK_( o.substr( 9 ) != std::string( mac ).substr( 9 ), "last 3 bytes of %s synthetic", mac );
		TEST_CHECK( std::regex_match( o, std::regex( "([0-9A-F]{2}:){5}[0-9A-F]{2}" ) ) );
	}
	std::vector<TextFile> files;
	pg_text_fixture( files );
	ctx = pg_fresh( Environment::vmware_guest );
	TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files } );
	const IdentityEntry* m1 = pg_find( t.identity, "mac", "00:50:56:3F:1D:42" );
	const IdentityEntry* m2 = pg_find( t.identity, "mac", "00:11:22:33:44:55" );
	TEST_ASSERT( m1 != nullptr && m2 != nullptr );
	TEST_CHECK( m1->synthetic.substr( 0, 9 ) == "00:50:56:" && m1->synthetic != m1->original );
	TEST_CHECK( m2->synthetic.substr( 0, 9 ) == "00:11:22:" && m2->occurrences == 2 );
	TEST_CHECK( std::count_if( t.identity.begin(), t.identity.end(), []( const IdentityEntry& r ) { return r.kind == "mac"; } ) == 2 );
}

// rules 4a / 4b / 4c on the text fixture (4b: the SCSI PnP parent-prefix hash stays exact)
static void pg_identity_rules( void )
{
	BuildContext ctx;
	std::vector<TextFile> files;
	pg_text_fixture( files );
	ctx = pg_fresh( Environment::bare_host );
	TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files } );
	const IdentityEntry* n = pg_find( t.identity, "computer_name", "DESKTOP-FIXTURE" );
	TEST_ASSERT( n != nullptr );
	TEST_CHECK( std::regex_match( n->synthetic, std::regex( "DESKTOP-[A-Z0-9]{7}" ) ) );
	TEST_CHECK_( n->occurrences == 3, "computer name: 3 occurrences (NetBIOS, DNS host, folder name; not the longer token), got %zu", n->occurrences );
	TEST_CHECK_( !pg_find( t.identity, "storage_serial_part", "1a2b3c4d" ), "the SCSI PnP instance hash is exact (decision 2026-10-16)" );
	for ( const FieldMapEntry& f : t.fields ) TEST_CHECK( f.length != 8 || f.rule != "4b" );
	TEST_ASSERT( t.storage_paths.size() == 1 );
	TEST_CHECK( std::regex_match( t.storage_paths[ 0 ].synthetic, std::regex( R"(STORAGE\\Volume\\\{[0-9a-f]{8}-[0-9a-f]{4}-1[0-9a-f]{3}-[89ab][0-9a-f]{3}-806e6f6e6963\}#0000000000200000)" ) ) );
	size_t guids = std::count_if( t.identity.begin(), t.identity.end(), []( const IdentityEntry& r ) { return r.rule == "4c"; } );
	TEST_CHECK_( guids == 5, "5 GUIDs, got %zu", guids );
	TEST_CHECK( !pg_find( t.identity, "container_id", "{00000000-0000-0000-FFFF-FFFFFFFFFFFF}" ) );
	const IdentityEntry* c = pg_find( t.identity, "container_id", "{DEADBEEF-0000-11F0-A000-806E6F6E6963}" );
	TEST_ASSERT( c != nullptr );
	TEST_CHECK_( c->occurrences == 3, "container ID in enum_registry + pci_devices (both cases), got %zu", c->occurrences );
	for ( const IdentityEntry& r : t.identity )
	{
		TEST_CHECK_( r.synthetic.size() == r.original.size(), "%s keeps its length", r.id.c_str() );
		if ( r.rule != "4c" ) continue;
		TEST_CHECK( std::regex_match( r.synthetic, std::regex( R"(\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\})" ) ) );
		TEST_CHECK_( r.synthetic[ 15 ] == r.original[ 15 ], "%s version digit kept", r.id.c_str() );
		unsigned v0 = unsigned( std::stoul( r.original.substr( 20, 1 ), nullptr, 16 ) ), v1 = unsigned( std::stoul( r.synthetic.substr( 20, 1 ), nullptr, 16 ) );
		unsigned keep = v0 < 8 ? 0x8 : v0 < 12 ? 0xC : 0xE;
		TEST_CHECK_( ( v0 & keep ) == ( v1 & keep ), "%s variant bits kept", r.id.c_str() );
	}
	for ( const FieldMapEntry& f : t.fields )
	{
		if ( f.rule.back() == 'r' ) continue;	// relation rows carry no location
		const TextFile* tf = nullptr;
		for ( const TextFile& x : files ) if ( x.name == f.area ) tf = &x;
		TEST_ASSERT( tf != nullptr );
		size_t line = 1, pos = 0;
		while ( line < f.line ) { pos = size_t( std::find( tf->data.begin() + pos, tf->data.end(), '\n' ) - tf->data.begin() ) + 1; line++; }
		std::string got( tf->data.begin() + pos + f.offset - 1, tf->data.begin() + pos + f.offset - 1 + f.length );
		const IdentityEntry* r = nullptr;
		for ( const IdentityEntry& x : t.identity ) if ( x.id == f.id ) r = &x;
		TEST_ASSERT( r != nullptr );
		std::string want = r->original;
		if ( r->rule == "4c" && want.front() == '{' ) want = want.substr( 1, 36 );
		// a MAC occurs in its hyphen / colon forms, an install-history value also as its 0x hex form
		TEST_CHECK_( _stricmp( got.c_str(), want.c_str() ) == 0 || ( r->rule == "3" && got.size() == 17 ) || ( r->rule == "7b" && got.rfind( "0x", 0 ) == 0 ), "occurrence of %s at %s %llu:%llu", f.id.c_str(), f.area.c_str(),
			(unsigned long long)f.line, (unsigned long long)f.offset );
	}
}

// (f) the original profile is refused inside a git work tree (".git" directory or worktree file in an ancestor); nothing
// is created there. A synthetic profile may go anywhere; stale files in an output folder are refused.
// Cleanup (P4, user decision 2026-10-17): no recursive delete - exactly the files and folders this test creates, as literal
// paths relative to its own temp root (validated: the root is temp\pg_unit_<pid>, every path lexically inside it), each
// removed with a single non-recursive remove, deepest first
static void pg_remove_listed( const fs::path& root, std::initializer_list<const char*> files, std::initializer_list<const char*> folders )
{
	fs::path tmp = fs::temp_directory_path().lexically_normal();
	if ( tmp.filename().empty() ) tmp = tmp.parent_path();	// temp_directory_path ends with a separator
	const bool root_ok = root.lexically_normal().parent_path() == tmp;
	const bool name_ok = root.filename().string() == "pg_unit_" + std::to_string( GetCurrentProcessId() );
	TEST_ASSERT_( root_ok && name_ok, "the cleanup root is the test's own temp folder" );
	std::error_code ec;
	auto inside = [ & ]( const fs::path& p ) { const fs::path n = p.lexically_normal(); return n.native().rfind( root.lexically_normal().native() + L"\\", 0 ) == 0; };
	for ( const char* f : files ) { const fs::path p = root / f; if ( inside( p ) && fs::is_regular_file( p, ec ) ) fs::remove( p, ec ); }
	for ( const char* d : folders ) { const fs::path p = root / d; if ( inside( p ) ) fs::remove( p, ec ); }	// non-recursive: fails when not empty
	fs::remove( root, ec );
}
static void pg_git_refusal( void )
{
	fs::path base = fs::temp_directory_path() / ( "pg_unit_" + std::to_string( GetCurrentProcessId() ) );
	std::error_code ec;
	TEST_ASSERT_( !fs::exists( base, ec ), "the test's temp folder does not exist yet (nothing to clean blindly)" );
	fs::create_directories( base / "repo" / ".git" );
	fs::create_directories( base / "wt" );
	std::ofstream( base / "wt" / ".git" ) << "gitdir: elsewhere\n";
	std::map<std::string, Bytes> files = { { "smbios.bin", { 1, 2, 3 } }, { "acpi\\acpi_TEST.bin", { 4 } } };
	TEST_CHECK( ProfileGenerator::check_git_work_tree( base / "repo" / "sub" / "orig" ) == fs::weakly_canonical( base / "repo" ) );
	TEST_CHECK( ProfileGenerator::check_git_work_tree( base / "wt" / "a" / "b" ) == fs::weakly_canonical( base / "wt" ) );
	for ( fs::path dir : { base / "repo" / "sub" / "orig", base / "wt" / "orig", base / "repo" } )
	{
		bool threw = false;
		try { ProfileGenerator::save_profile( dir, files, true ); }
		catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "refusing to write the ORIGINAL profile" ) != std::string::npos; }
		TEST_CHECK_( threw, "original refused in %s", dir.string().c_str() );
	}
	TEST_CHECK( !fs::exists( base / "repo" / "sub" ) && !fs::exists( base / "wt" / "orig" ) && !fs::exists( base / "repo" / "smbios.bin" ) );
	ProfileGenerator::save_profile( base / "repo" / "syn", files, false );
	TEST_CHECK( fs::exists( base / "repo" / "syn" / "acpi" / "acpi_TEST.bin" ) );
	if ( ProfileGenerator::check_git_work_tree( base ).empty() )
	{
		ProfileGenerator::save_profile( base / "plain" / "orig", files, true );
		TEST_CHECK( fs::exists( base / "plain" / "orig" / "smbios.bin" ) );
		std::ofstream( base / "plain" / "orig" / "old.bin" ) << "x";
		bool threw = false;
		try { ProfileGenerator::save_profile( base / "plain" / "orig", files, true ); }
		catch ( const std::exception& ) { threw = true; }
		TEST_CHECK( threw );
	}
	pg_remove_listed( base, { "wt/.git", "repo/syn/smbios.bin", "repo/syn/acpi/acpi_TEST.bin", "plain/orig/smbios.bin", "plain/orig/acpi/acpi_TEST.bin", "plain/orig/old.bin" },
		{ "repo/syn/acpi", "repo/syn", "repo/.git", "repo", "wt", "plain/orig/acpi", "plain/orig", "plain" } );
	TEST_CHECK_( !fs::exists( base, ec ), "everything the test created is removed" );
}

// check_diff_ranges and the single-value generators on their own
static void pg_helpers( void )
{
	BuildContext ctx;
	Bytes a = { 1, 2, 3, 4, 5, 6 }, b = { 1, 9, 9, 4, 5, 0 };
	auto r = ProfileGenerator::check_diff_ranges( a, b );
	TEST_CHECK( r.size() == 2 && r[ 0 ].first == 1 && r[ 0 ].last == 3 && r[ 1 ].first == 5 && r[ 1 ].last == 6 );
	ctx = pg_fresh( Environment::bare_host );
	TextValue o = ProfileGen->gen_class_string( ctx, { "07X1234_A99b-z", "classes" } );
	TEST_CHECK( std::regex_match( o.text, std::regex( "[0-9]{2}[A-Z][0-9]{4}_[A-Z][0-9]{2}[a-z]-[a-z]" ) ) );
	bool threw = false;
	try { ProfileGen->gen_class_string( ctx, { "--__", "x" } ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
	for ( int i = 0; i < 50; i++ ) TEST_CHECK( std::string( ProfileGen->gen_class_string( ctx, { "7", "one/" + std::to_string( i ) } ).text ) != "7" );
	TEST_CHECK( std::regex_match( ProfileGen->gen_hex_token( ctx, { "1a2b3c4d", "hex" } ).text, std::regex( "[0-9a-f]{8}" ) ) );
	ComputerName cn = ProfileGen->gen_computer_name( ctx, { "DESKTOP-ABCDEFG", "cn" } );
	TEST_CHECK( std::regex_match( cn.text, std::regex( "DESKTOP-[A-Z0-9]{7}" ) ) && std::string( cn.text ) != "DESKTOP-ABCDEFG" );
	VolumeId vs = ProfileGen->gen_volume_serial( ctx, { "1A2B-3C4D", "vs" } );
	TEST_CHECK( std::regex_match( vs.serial, std::regex( "[0-9A-F]{4}-[0-9A-F]{4}" ) ) && std::string( vs.serial ) != "1A2B-3C4D" );
	// the same key asks the identity map: the value already taken, not a new draw
	TEST_CHECK( std::string( ProfileGen->gen_class_string( ctx, { "07X1234_A99b-z", "classes" } ).text ) == o.text );
	// the Randomizer's formats on their own (Randomize->gen_...): the captured value is the pattern, the result differs
	TEST_CHECK( std::regex_match( Randomize->gen_like( "AB12cd-345_Z" ), std::regex( "[A-Z]{2}[0-9]{2}[a-z]{2}-[0-9]{3}_[A-Z]" ) ) );
	TEST_CHECK( std::regex_match( Randomize->gen_fill( "X-??", "01" ), std::regex( "X-[01]{2}" ) ) );
	TEST_CHECK( std::regex_match( Randomize->gen_sid_like( "S-1-5-21-1111111111-222222222-3333333333" ), std::regex( "S-1-5-21-[1-9][0-9]{9}-[1-9][0-9]{8}-[1-9][0-9]{9}" ) ) );
	TEST_CHECK( std::regex_match( Randomize->gen_mac_like( "00:11:22:33:44:55", 16 ), std::regex( "00:11:22:[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}" ) ) );
	const int64_t v = Randomize->gen_in_range( -5, 5 );
	TEST_CHECK( v >= -5 && v <= 5 && Randomize->gen_below( 1 ) == 0 );
}

// rule 2h: host only; the guest keeps every type 17 string byte-identical
static void pg_type17_names_host_only( void )
{
	BuildContext ctx;
	Bytes raw = pg_fixture( false );
	SmbiosTable t = parse_smbios( raw );
	ctx = pg_fresh( Environment::bare_host );
	Bytes host = ProfileGen->gen_smbios( ctx, { raw } ).synthetic;
	const Bytes graw = pg_fixture( true );
	ctx = pg_fresh( Environment::vmware_guest );
	Bytes guest = ProfileGen->gen_smbios( ctx, { graw } ).synthetic;
	SmbiosTable tg = parse_smbios( graw );
	auto cls = []( const std::string& a, const std::string& b ) {
		if ( a.size() != b.size() || a == b ) return false;
		for ( size_t i = 0; i < a.size(); i++ )
		{
			uint8_t x = uint8_t( a[ i ] ), y = uint8_t( b[ i ] );
			if ( !( ( isdigit( x ) && isdigit( y ) ) || ( isupper( x ) && isupper( y ) ) || ( islower( x ) && islower( y ) ) || ( !isalnum( x ) && x == y ) ) ) return false;
		}
		return true;
	};
	for ( int k : { 0, 1, 2, 5 } )
		TEST_CHECK_( cls( pg_str( raw, t.structures[ 3 ].strings[ k ] ), pg_str( host, t.structures[ 3 ].strings[ k ] ) ), "host type 17 h0011 string %d synthetic", k + 1 );
	std::string part = pg_str( host, t.structures[ 3 ].strings[ 5 ] );
	TEST_CHECK( part.size() == 9 && part.substr( 7 ) == "  " );
	TEST_CHECK( pg_str( host, t.structures[ 3 ].strings[ 1 ] ) == pg_str( host, t.structures[ 4 ].strings[ 1 ] ) );
	TEST_CHECK( pg_str( host, t.structures[ 5 ].strings[ 2 ] ) == "Not Specified" );
	for ( size_t si : { 3u, 4u, 5u } )
		for ( size_t k = 0; k < tg.structures[ si ].strings.size(); k++ )
		{
			bool decided = ( si == 3 && ( k == 3 || k == 4 ) ) || ( si == 4 && k == 2 );
			if ( decided ) continue;
			TEST_CHECK_( pg_str( guest, tg.structures[ si ].strings[ k ] ) == pg_str( graw, tg.structures[ si ].strings[ k ] ), "guest type 17 structure %zu string %zu exact", si, k + 1 );
		}
	TEST_CHECK( BuildConfig::defaults( Environment::bare_host ).rules.r2h && !BuildConfig::defaults( Environment::vmware_guest ).rules.r2h );
	std::string g = BuildConfig::defaults( Environment::vmware_guest ).save();
	g = g.substr( 0, g.find( "rules" ) ) + "rules                        = 1 2 2h 2o 3 4a 4b 4c 4d 6 7 8\n";
	bool threw = false;
	try { BuildConfig::load( g ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
}

// a capture in memory (fixture SMBIOS, text fixture, one small ACPI table with a valid checksum)
static CaptureInputs pg_capture( bool vm, std::vector<TextFile> texts = {} )
{
	CaptureInputs in;
	in.folder_name = "fixture_DESKTOP-FIXTURE_20260101_000000";
	in.manifest_sha256 = "0000FIXTURE";
	in.tool_line = "# fwid manifest - fwid_probe fixture";
	in.manifest_entries = 7;
	in.rsmb = pg_fixture( vm );
	Bytes acpi = { 'T', 'E', 'S', 'T', 40, 0, 0, 0, 1, 0, 'F', 'I', 'X', 'T', 'U', 'R', 'F', 'I', 'X', 'T', 'U', 'R', 'E', '1' };
	acpi.resize( 40, 0x5A );
	uint8_t sum = 0;
	for ( uint8_t b : acpi ) sum = uint8_t( sum + b );
	acpi[ 9 ] = uint8_t( -sum );
	in.acpi = { { "acpi_TEST.bin", acpi } };
	if ( texts.empty() ) pg_text_fixture( in.texts );
	else in.texts = texts;
	return in;
}

// BuildConfig: save -> load gives the same configuration, and a rebuild from the loaded (and from the saved-next-to-the-
// output) configuration with the build's identity map gives byte-identical synthetic files; the saved copy holds no
// fixture identifier; the identity map is a profile file (the original copy alone with the install-history offset)
static void pg_build_config_roundtrip( void )
{
	BuildContext ctx;
	for ( bool vm : { true, false } )
	{
		Environment e = vm ? Environment::vmware_guest : Environment::bare_host;
		BuildConfig c1 = pg_cfg( e );
		BuildConfig c2 = BuildConfig::load( c1.save() );
		TEST_CHECK( c2 == c1 );
		TEST_CHECK( c2.save() == c1.save() );
		CaptureInputs in = pg_capture( vm );
		ctx = pg_fresh( e );
		ctx = ProfileGen->new_context( c1 );
		GenResult r1 = ProfileGen->gen_build( ctx, in );
		TEST_ASSERT_( r1.ok, "build: %s", r1.error.c_str() );
		TEST_CHECK( r1.generated );
		const Bytes& mf = r1.synthetic.at( "identity_map.tsv" );
		const IdentityMap applied = ProfileGenerator::parse_identity_map( std::string( mf.begin(), mf.end() ) );
		ctx = pg_fresh( e );
		ctx = ProfileGen->new_context( c2 );
		ProfileGen->load_identity_maps( ctx, applied, {} );
		GenResult r2 = ProfileGen->gen_build( ctx, in );
		TEST_ASSERT( r2.ok );
		TEST_CHECK( !r2.generated && r1.synthetic == r2.synthetic );
		const Bytes& saved = r1.synthetic.at( BuildConfig::k_file );
		BuildConfig c3 = BuildConfig::load( std::string( saved.begin(), saved.end() ) );
		TEST_CHECK( c3 == c1 );
		ctx = pg_fresh( e );
		ctx = ProfileGen->new_context( c3 );
		ProfileGen->load_identity_maps( ctx, applied, {} );
		GenResult r3 = ProfileGen->gen_build( ctx, in );
		TEST_ASSERT( r3.ok );
		TEST_CHECK( r3.synthetic == r1.synthetic );
		for ( const char* f : { "smbios.bin", "acpi\\acpi_TEST.bin", "identity.tsv", "fieldmap.tsv", "identity_map.tsv", "build_config.txt", "manifest.txt" } )
			TEST_CHECK_( r1.synthetic.count( f ) && r1.original.count( f ), "%s in both profiles", f );
		TEST_CHECK( r1.synthetic.at( "acpi\\acpi_TEST.bin" ) == in.acpi[ 0 ].second && r1.original.at( "smbios.bin" ) == in.rsmb );
		TEST_CHECK( r1.synthetic.at( "fieldmap.tsv" ) == r1.original.at( "fieldmap.tsv" ) );
		std::string so( saved.begin(), saved.end() );
		const Bytes& oc = r1.original.at( BuildConfig::k_file );
		std::string oo( oc.begin(), oc.end() );
		for ( const char* real : { "DESKTOP-FIXTURE", "AB12cd-345_Z", "1234ABCD", "00:11:22:33:44:55", "deadbeef" } )
			for ( auto& [ name, data ] : r1.synthetic )
			{
				if ( name.rfind( "acpi", 0 ) == 0 ) continue;
				std::string t( data.begin(), data.end() );
				for ( char& ch : t ) ch = char( tolower( uint8_t( ch ) ) );
				std::string n = real;
				for ( char& ch : n ) ch = char( tolower( uint8_t( ch ) ) );
				TEST_CHECK_( t.find( n ) == std::string::npos, "fixture value %s not in the synthetic %s", real, name.c_str() );
			}
		TEST_CHECK( oo.find( "resolved.capture_folder = fixture_DESKTOP-FIXTURE_20260101_000000" ) != std::string::npos );
		TEST_CHECK( so.find( "resolved." ) == std::string::npos );
		TEST_CHECK( !r1.log.entries.empty() && r1.log.text().find( "[profilegen] leak guard: " ) != std::string::npos );
	}
	for ( const char* bad : { "format = 2\n", "environment = mars\n", "bogus = 1\n", "out = a\nout = b\n", "seed = 12\n", "identity_map = other.tsv\n" } )
	{
		bool threw = false;
		try { BuildConfig::load( bad ); } catch ( const std::exception& ) { threw = true; }
		TEST_CHECK_( threw, "rejected: %s", bad );
	}
}

// a second text fixture: USB serials, related MACs (a radio + its derived adapters, a wired NIC with an EUI-64 instance
// ID, a VMnet adapter, a Hyper-V adapter) and version-1 GUIDs (made-up values throughout)
static std::vector<TextFile> pg_text_fixture2( const std::string& usb_lines )
{
	auto tf = []( const char* name, const std::string& s ) { return TextFile { name, Bytes( s.begin(), s.end() ) }; };
	auto adapter = []( int n, const char* guid, const char* desc, const char* mac ) {
		return "=== adapter " + std::to_string( n ) + "\nAdapterName: \"" + guid + "\"\nDescription: \"" + desc + "\"\nMAC (6 bytes): " + mac + "\n";
	};
	std::string na = adapter( 0, "{0F0F0F0F-1111-4222-8333-444455556666}", "Fixture Wired Ethernet", "A0:B1:C2:01:02:03" ) +
					 adapter( 1, "{1E1E1E1E-1111-4222-8333-444455556666}", "Fixture Wi-Fi Radio", "3C:A9:F4:10:20:30" ) +
					 adapter( 2, "{2D2D2D2D-1111-4222-8333-444455556666}", "Fixture Wi-Fi Direct", "3C:A9:F4:10:20:31" ) +
					 adapter( 3, "{3C3C3C3C-1111-4222-8333-444455556666}", "Fixture Wi-Fi Direct #2", "3E:A9:F4:10:20:30" ) +
					 adapter( 4, "{4B4B4B4B-1111-4222-8333-444455556666}", "Fixture Bluetooth PAN", "3C:A9:F4:10:20:34" ) +
					 adapter( 5, "{5A5A5A5A-1111-4222-8333-444455556666}", "Fixture VMnet8", "00:50:56:C0:00:08" ) +
					 adapter( 6, "{69696969-1111-4222-8333-444455556666}", "Fixture Hyper-V Adapter", "00:15:5D:AA:BB:CC" );
	return {
		tf( "capture_info.txt", "os.computer_name        = NetBIOS \"DESKTOP-FIXTUR2\", DNS host \"DESKTOP-FIXTUR2\", DNS domain \"\"\n"
								"run.start_utc           = 2026-01-01T00:00:00.000Z\n"
								"os.registry             = ProductName \"Windows 10 Pro\", EditionID \"Professional\", DisplayVersion \"22H2\", CurrentBuild 19045, UBR 1\n"
								"wpd.note                = SWD\\WPDBUSENUM\\{5c5c5c5c-7777-11f0-9123-3ca9f4102030}\n"
								"profile.path            = C:\\Users\\Fixer\\AppData\n" ),
		tf( "network_adapters.txt", na ),
		tf( "enum_registry.txt", "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\PCI\\VEN_8086&DEV_1234&SUBSYS_00000000&REV_04\\A0B1C2FFFF01020300\n"
							   "  value \"ContainerID\"  REG_SZ (1)  78 bytes: \"{12345678-9abc-11f0-8123-3ca9f4102030}\"\n"
							   "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\STORAGE\\Volume\\{87654321-0000-11f0-a000-806e6f6e6963}#0000000000200000\n" + usb_lines ),
		tf( "pci_devices.txt", "  instance PCI\\VEN_8086&DEV_1234&SUBSYS_00000000&REV_04\\A0B1C2FFFF01020300\n"
							 "  SPDRP_BASE_CONTAINERID             REG_SZ \"{12345678-9abc-11f0-8123-3ca9f4102030}\"\n" ),
		tf( "certificates.txt", "subject: \"CN=Fixture Test Certificate\"\nnode 3CA9F4102030 here is not searched\n" ),
		tf( "os_ids.txt", k_pg_install_os_ids ) };	// index 5: the install history (fixture 3 replaces it by the full os_ids.txt)
}
static std::string pg_usb( const char* dev, const char* serial ) { return std::string( "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\USB\\" ) + dev + "\\" + serial + "\n"; }

// the UUID-MAC relation: the real UUID's last 6 bytes are an adapter's MAC -> the synthetic UUID's last 6 bytes are that
// adapter's synthetic MAC (OUI kept), the field map names the adapter; without a relation the UUID is independent
static void pg_uuid_mac_relation( void )
{
	BuildContext ctx;
	auto mac_of = []( const std::string& t ) {
		std::array<uint8_t, 6> m {};
		for ( int i = 0; i < 6; i++ ) m[ i ] = uint8_t( std::stoul( t.substr( i * 3, 2 ), nullptr, 16 ) );
		return m;
	};
	for ( bool related : { true, false } )
		for ( bool vm : { false, true } )
		{
			CaptureInputs in = pg_capture( vm, pg_text_fixture2( "" ) );
			SmbiosTable t = parse_smbios( in.rsmb );
			const size_t uo = t.structures[ 0 ].offset + 8;
			const uint8_t mac[ 6 ] = { 0xA0, 0xB1, 0xC2, 0x01, 0x02, 0x03 };
			if ( related ) memcpy( &in.rsmb[ uo + 10 ], mac, 6 );
			Bytes real( in.rsmb.begin() + uo, in.rsmb.begin() + uo + 16 );
			ctx = pg_fresh( vm ? Environment::vmware_guest : Environment::bare_host );
			GenResult r = ProfileGen->gen_build( ctx, in );
			TEST_ASSERT_( r.ok, "build: %s", r.error.c_str() );
			const uint8_t* u = &r.synthetic.at( "smbios.bin" )[ uo ];
			const IdentityEntry* m = pg_find( r.identity.identity, "mac", "A0:B1:C2:01:02:03" );
			TEST_ASSERT( m != nullptr );
			auto sm = mac_of( m->synthetic );
			const FieldMapEntry& f = r.identity.fields[ 0 ];
			TEST_CHECK( f.id == "F01" && f.length == 16 );
			bool tied = memcmp( u + 10, sm.data(), 6 ) == 0;
			TEST_CHECK_( tied == related, "related %d vm %d: UUID tail = the synthetic MAC: %d", related, vm, tied );
			TEST_CHECK( memcmp( u, real.data(), 16 ) != 0 );
			TEST_CHECK( ( f.location.find( "UUID-MAC relation" ) != std::string::npos ) == related );
			if ( related )
			{
				TEST_CHECK( u[ 10 ] == 0xA0 && u[ 11 ] == 0xB1 && u[ 12 ] == 0xC2 && memcmp( u + 13, mac + 3, 3 ) != 0 );
				TEST_CHECK( f.location.find( "adapter \"Fixture Wired Ethernet\"" ) != std::string::npos && f.location.find( m->id ) != std::string::npos );
			}
			if ( vm ) TEST_CHECK( u[ 0 ] == 0x56 && u[ 1 ] == 0x4D );
			else TEST_CHECK( ( u[ 6 ] >> 4 ) == ( real[ 6 ] >> 4 ) && ( u[ 7 ] >> 4 ) == ( real[ 7 ] >> 4 ) && ( u[ 8 ] >> 6 ) == ( real[ 8 ] >> 6 ) );
			const Bytes& sb = r.synthetic.at( "smbios.bin" );
			TEST_CHECK( std::search( sb.begin(), sb.end(), mac, mac + 6 ) == sb.end() );
		}
}

// rule 4d: one synthetic serial per physical USB device across both profiles (shared table, index of first appearance);
// hex-encoded ASCII stays hex-encoded ASCII of the same shape; placeholders stay
static void pg_usb_shared_table( void )
{
	BuildContext ctx;
	std::string host = pg_usb( "VID_1111&PID_2222", "4142433132333435" ) + pg_usb( "Vid_0E0F&Pid_0001", "4142433132333435" ) + pg_usb( "VID_3333&PID_4444", "Q9ZK21x" ) +
					   pg_usb( "VID_5555&PID_6666", "000000000000" ) + pg_usb( "VID_7777&PID_8888", "MSFT3000000000" ) + pg_usb( "VID_9999&PID_AAAA", "0a1b2c3d4e5f60718293" );
	std::string guest = pg_usb( "VID_BBBB&PID_CCCC", "7Y7Y" ) + pg_usb( "VID_1111&PID_2222", "4142433132333435" );
	auto fh = pg_text_fixture2( host ), fg = pg_text_fixture2( guest );
	std::vector<std::string> table = ProfileGenerator::parse_usb_serial_table( { fh[ 2 ].data, fg[ 2 ].data } );
	TEST_CHECK_( table.size() == 4, "table: 3 host serials + 1 guest-only serial (placeholders excluded), got %zu", table.size() );
	ctx = pg_fresh( Environment::bare_host );
	TextIdentityGen th = ProfileGen->gen_text_identity( ctx, { fh, table } );
	const IdentityMap hmap = ctx.identity_map;	// the host's map: its "shared/usb/<index>" values
	ctx = pg_fresh( Environment::vmware_guest );
	ProfileGen->load_identity_maps( ctx, {}, hmap );				// a new guest identity, the shared serials from the peer map
	TextIdentityGen tg = ProfileGen->gen_text_identity( ctx, { fg, table } );
	TEST_CHECK( hmap.entries.count( "shared/usb/0" ) && ctx.identity_map.entries.at( "shared/usb/0" ) == hmap.entries.at( "shared/usb/0" ) );
	const IdentityEntry* a = pg_find( th.identity, "usb_serial", "4142433132333435" );
	const IdentityEntry* b = pg_find( tg.identity, "usb_serial", "4142433132333435" );
	TEST_ASSERT( a != nullptr && b != nullptr );
	TEST_CHECK_( a->synthetic == b->synthetic, "the same physical device: the same synthetic serial in --host (incl. the Vid_0E0F mirror) and --guest" );
	TEST_CHECK( a->occurrences == 2 );
	std::string txt;
	for ( size_t i = 0; i < a->synthetic.size(); i += 2 ) txt += char( std::stoul( a->synthetic.substr( i, 2 ), nullptr, 16 ) );
	TEST_CHECK_( std::regex_match( txt, std::regex( "[A-Z]{3}[0-9]{5}" ) ) && txt != "ABC12345", "hex-encoded ASCII of the same shape" );
	const IdentityEntry* h = pg_find( th.identity, "usb_serial", "0a1b2c3d4e5f60718293" );
	TEST_ASSERT( h != nullptr );
	bool shape = h->synthetic.size() == 20;
	for ( size_t i = 0; shape && i < 20; i++ ) shape = ( isdigit( uint8_t( h->original[ i ] ) ) != 0 ) == ( isdigit( uint8_t( h->synthetic[ i ] ) ) != 0 ) && isxdigit( uint8_t( h->synthetic[ i ] ) ) && !isupper( uint8_t( h->synthetic[ i ] ) );
	TEST_CHECK_( shape, "a non-ASCII hex serial stays lower-case hex, digits at the digit positions" );
	const IdentityEntry* q = pg_find( th.identity, "usb_serial", "Q9ZK21x" );
	TEST_ASSERT( q != nullptr );
	TEST_CHECK( std::regex_match( q->synthetic, std::regex( "[A-Z][0-9][A-Z]{2}[0-9]{2}[a-z]" ) ) && q->synthetic != q->original );
	TEST_CHECK( !pg_find( th.identity, "usb_serial", "000000000000" ) && !pg_find( th.identity, "usb_serial", "MSFT3000000000" ) );
	TEST_CHECK( ProfileGenerator::check_usb_placeholder( "00000000" ) && ProfileGenerator::check_usb_placeholder( "MSFT3000000000000000000000" ) && !ProfileGenerator::check_usb_placeholder( "MSFT3100000000" ) );
	ctx = pg_fresh( Environment::vmware_guest );
	TextIdentityGen solo = ProfileGen->gen_text_identity( ctx, { fg } );	// own table only, no peer map: another (new) value
	const IdentityEntry* c = pg_find( solo.identity, "usb_serial", "4142433132333435" );
	TEST_ASSERT( c != nullptr );
	TEST_CHECK( c->synthetic != b->synthetic );
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->load_identity_maps( ctx, hmap, {} );
	UsbSerial us = ProfileGen->gen_usb_serial( ctx, { "4142433132333435", 0, "x" } );
	TEST_CHECK( us.text == a->synthetic );	// gen_usb_serial with the shared index = the identity's value (applied map)
}

// rule 3 relations: one synthetic MAC per radio / NIC, dependents re-derived (+1, locally administered variant, +4), the
// EUI-64 instance ID re-derived, VMnet exact, Hyper-V OUI kept; relations recorded by adapter description
static void pg_mac_relations( void )
{
	BuildContext ctx;
	auto files = pg_text_fixture2( "" );
	ctx = pg_fresh( Environment::bare_host );
	TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files } );
	auto val = [ & ]( const char* real ) -> unsigned long long {
		const IdentityEntry* r = pg_find( t.identity, "mac", real );
		if ( !r ) return ~0ull;
		std::string h;
		for ( char ch : r->synthetic ) if ( ch != ':' ) h += ch;
		return std::stoull( h, nullptr, 16 );
	};
	unsigned long long wifi = val( "3C:A9:F4:10:20:30" ), direct = val( "3C:A9:F4:10:20:31" ), direct2 = val( "3E:A9:F4:10:20:30" ), bt = val( "3C:A9:F4:10:20:34" );
	unsigned long long wired = val( "A0:B1:C2:01:02:03" ), hv = val( "00:15:5D:AA:BB:CC" );
	TEST_ASSERT( wifi != ~0ull && direct != ~0ull && direct2 != ~0ull && bt != ~0ull && wired != ~0ull && hv != ~0ull );
	TEST_CHECK( ( wifi >> 24 ) == 0x3CA9F4 && wifi != 0x3CA9F4102030ull );
	TEST_CHECK_( direct == wifi + 1, "Wi-Fi Direct = Wi-Fi + 1" );
	TEST_CHECK_( direct2 == ( wifi ^ ( 2ull << 40 ) ), "Wi-Fi Direct #2 = the locally administered variant" );
	TEST_CHECK_( bt == wifi + 4, "Bluetooth PAN = Wi-Fi + 4" );
	TEST_CHECK( ( wired >> 24 ) == 0xA0B1C2 && ( hv >> 24 ) == 0x00155D && hv != 0x00155DAABBCCull );
	TEST_CHECK_( !pg_find( t.identity, "mac", "00:50:56:C0:00:08" ), "the VMnet pattern is not an identifier (kept exact)" );
	const IdentityEntry* e = pg_find( t.identity, "mac_eui64", "A0B1C2FFFF01020300" );
	TEST_ASSERT( e != nullptr );
	char want[ 32 ];
	snprintf( want, sizeof want, "A0B1C2FFFF%06llX00", wired & 0xFFFFFF );
	TEST_CHECK_( e->synthetic == want && e->occurrences == 2, "EUI-64 instance ID re-derived from the synthetic wired MAC" );
	size_t rel = 0;
	bool named = false;
	for ( const FieldMapEntry& f : t.fields )
		if ( f.rule == "3r" )
		{
			rel++;
			named |= f.location.find( "adapter \"Fixture Wi-Fi Direct #2\" = the locally administered variant (byte 0 bit 1) of adapter \"Fixture Wi-Fi Radio\"" ) != std::string::npos;
		}
	TEST_CHECK_( rel == 4 && named, "3 MAC relations + 1 EUI-64 relation recorded by description, got %zu", rel );
	MacFamily fam;
	fam.members = { { { "3C:A9:F4:10:20:30", "radio", "R1" } }, { { "3C:A9:F4:10:20:31", "direct", "R2" } }, { { "3E:A9:F4:10:20:30", "direct2", "R3" } } };
	ctx = pg_fresh( Environment::bare_host );
	MacFamily sf = ProfileGen->gen_mac_family( ctx, fam );
	TEST_CHECK( sf.members[ 1 ].parent == 0 && sf.members[ 1 ].delta == 1 && sf.members[ 2 ].parent == 0 && sf.members[ 2 ].locally_administered );
	ctx = pg_fresh( Environment::bare_host );
	Eui64Id ei = ProfileGen->gen_eui64_instance_id( ctx, { "a0b1c2ffff01020300", true }, { "A0:B1:C2:0A:0B:0C", "", "x" } );
	TEST_CHECK( std::string( ei.text ) == "a0b1c2ffff0a0b0c00" );
}

// rule 4c version-1 GUIDs: node = the synthetic MAC of its adapter (also when the GUID only occurs elsewhere, e.g. a WPD
// instance), Windows' no-NIC node kept, timestamp / clock synthetic, version 1 and variant kept, one GUID = one value
static void pg_v1_guid_node( void )
{
	BuildContext ctx;
	auto files = pg_text_fixture2( "" );
	ctx = pg_fresh( Environment::bare_host );
	TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files } );
	const IdentityEntry* wifi = pg_find( t.identity, "mac", "3C:A9:F4:10:20:30" );
	TEST_ASSERT( wifi != nullptr );
	std::string node;
	for ( char ch : wifi->synthetic ) if ( ch != ':' ) node += char( toupper( uint8_t( ch ) ) );
	const IdentityEntry* c = pg_find( t.identity, "container_id", "{12345678-9ABC-11F0-8123-3CA9F4102030}" );
	TEST_ASSERT( c != nullptr );
	TEST_CHECK( c->synthetic.substr( 25, 12 ) == node && c->synthetic[ 15 ] == '1' && c->synthetic.substr( 1, 8 ) != "12345678" );
	unsigned v0 = unsigned( std::stoul( c->original.substr( 20, 1 ), nullptr, 16 ) ), v1 = unsigned( std::stoul( c->synthetic.substr( 20, 1 ), nullptr, 16 ) );
	TEST_CHECK( ( v0 & 0xC ) == ( v1 & 0xC ) );
	TEST_CHECK_( c->occurrences == 2, "the same GUID in enum_registry and pci_devices, got %zu", c->occurrences );
	const IdentityEntry* w = pg_find( t.identity, "mac_node_guid", "{5C5C5C5C-7777-11F0-9123-3CA9F4102030}" );
	TEST_ASSERT( w != nullptr );
	TEST_CHECK( w->synthetic.substr( 25, 12 ) == node );
	const IdentityEntry* v = pg_find( t.identity, "volume_guid", "{87654321-0000-11F0-A000-806E6F6E6963}" );
	TEST_ASSERT( v != nullptr );
	TEST_CHECK( v->synthetic.substr( 25, 12 ) == "806E6F6E6963" && v->synthetic.substr( 1, 8 ) != "87654321" && v->synthetic[ 15 ] == '1' );
	for ( const FieldMapEntry& f : t.fields ) TEST_CHECK( f.area != "certificates.txt" );
	const IdentityEntry* g = pg_find( t.identity, "interface_guid", "{0F0F0F0F-1111-4222-8333-444455556666}" );
	TEST_ASSERT( g != nullptr );
	TEST_CHECK( g->synthetic[ 15 ] == '4' && g->synthetic.substr( 25, 12 ) != "444455556666" );
	ctx = pg_fresh( Environment::bare_host );
	GuidValue mbr = ProfileGen->gen_guid( ctx, { "{3fa81c55-0000-0000-0000-100000000000}", "mbr" } );
	const std::string mt = mbr.text;
	TEST_CHECK( mt.substr( 9 ) == "-0000-0000-0000-100000000000}" && mt.substr( 1, 8 ) != "3fa81c55" );
}

// an EDID base block (made-up manufacturer "FIX"): product, serial field, week / year, version, descriptors (detailed
// timing, FFh serial string or FEh text, FCh name, FDh range), extension count, checksum; optional extension block
static Bytes pg_edid( uint16_t product, uint32_t serial, const char* serial_string, bool with_extension )
{
	Bytes b( 128, 0 );
	const uint8_t hdr[ 8 ] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
	memcpy( b.data(), hdr, 8 );
	b[ 8 ] = 0x19;
	b[ 9 ] = 0x38;
	b[ 10 ] = uint8_t( product );
	b[ 11 ] = uint8_t( product >> 8 );
	for ( int i = 0; i < 4; i++ ) b[ 12 + i ] = uint8_t( serial >> ( 8 * i ) );
	b[ 16 ] = 12;
	b[ 17 ] = 33;
	b[ 18 ] = 1;
	b[ 19 ] = 4;
	b[ 54 ] = 0x02;
	b[ 55 ] = 0x3A;
	auto desc = [ & ]( size_t d, uint8_t tag, const char* s ) {
		b[ d + 3 ] = tag;
		size_t n = strlen( s );
		memcpy( &b[ d + 5 ], s, n );
		if ( n < 13 ) { b[ d + 5 + n ] = 0x0A; for ( size_t k = n + 1; k < 13; k++ ) b[ d + 5 + k ] = 0x20; }
	};
	if ( serial_string ) desc( 72, 0xFF, serial_string );
	else desc( 72, 0xFE, "FIXTURE TEXT" );
	desc( 90, 0xFC, "FIXMON 24" );
	b[ 108 + 3 ] = 0xFD;
	b[ 126 ] = with_extension ? 1 : 0;
	uint8_t sum = 0;
	for ( size_t i = 0; i < 127; i++ ) sum = uint8_t( sum + b[ i ] );
	b[ 127 ] = uint8_t( 0x100 - sum );
	if ( with_extension )
	{
		Bytes ext( 128, 0x02 );
		if ( serial_string ) memcpy( &ext[ 20 ], serial_string, strlen( serial_string ) );	// a serial inside an extension: reported, never changed
		b.insert( b.end(), ext.begin(), ext.end() );
	}
	return b;
}

// rule 6: checksum validity, one synthetic serial per physical monitor (same model + serial on two ports), exact fields
// untouched, extension blocks untouched and scanned
static void pg_edid_rules( void )
{
	BuildContext ctx;
	EdidInput in { { { "edid_0.bin", pg_edid( 0x2701, 0x12345678, "FIXSER12345", true ) }, { "edid_1.bin", pg_edid( 0x2701, 0x12345678, "FIXSER12345", false ) },
		{ "edid_2.bin", pg_edid( 0x4012, 0x32314241, nullptr, false ) } } };
	ctx = pg_fresh( Environment::bare_host );
	EdidGen g = ProfileGen->gen_edid( ctx, in );
	TEST_ASSERT( g.synthetic.size() == 3 );
	for ( const EdidBlock& b : g.synthetic )
	{
		uint8_t sum = 0;
		for ( size_t i = 0; i < 128; i++ ) sum = uint8_t( sum + b.bytes[ i ] );
		TEST_CHECK_( sum == 0, "%s checksum valid", b.name.c_str() );
	}
	const Bytes &a0 = in.blocks[ 0 ].bytes, &s0 = g.synthetic[ 0 ].bytes, &s1 = g.synthetic[ 1 ].bytes, &a2 = in.blocks[ 2 ].bytes, &s2 = g.synthetic[ 2 ].bytes;
	TEST_CHECK( !std::equal( a0.begin() + 12, a0.begin() + 16, s0.begin() + 12 ) );
	TEST_CHECK_( std::equal( s0.begin() + 12, s0.begin() + 16, s1.begin() + 12 ) && std::equal( s0.begin() + 77, s0.begin() + 88, s1.begin() + 77 ), "one synthetic serial per monitor" );
	std::string ss( s0.begin() + 77, s0.begin() + 88 );
	TEST_CHECK( std::regex_match( ss, std::regex( "[A-Z]{6}[0-9]{5}" ) ) && ss != "FIXSER12345" && s0[ 88 ] == 0x0A );
	TEST_CHECK( std::equal( a0.begin() + 8, a0.begin() + 12, s0.begin() + 8 ) && a0[ 16 ] == s0[ 16 ] && a0[ 17 ] == s0[ 17 ] );	// manufacturer, product, week, year
	TEST_CHECK( std::equal( a0.begin() + 90, a0.begin() + 108, s0.begin() + 90 ) );	// FCh name
	TEST_CHECK( std::equal( a0.begin() + 128, a0.end(), s0.begin() + 128 ) && g.ext_blocks == 1 && g.ext_hits == 1 );	// extension untouched, its serial reported
	TEST_CHECK( isupper( s2[ 12 ] ) && isupper( s2[ 13 ] ) && isdigit( s2[ 14 ] ) && isdigit( s2[ 15 ] ) && !std::equal( a2.begin() + 12, a2.begin() + 16, s2.begin() + 12 ) );
	TEST_CHECK( std::equal( a2.begin() + 72, a2.begin() + 90, s2.begin() + 72 ) );
	size_t rows6 = 0, rows6c = 0;
	for ( const FieldMapEntry& f : g.fields ) { rows6 += f.rule == "6"; rows6c += f.rule == "6c"; }
	TEST_CHECK_( rows6 == 5 && rows6c == 3 && g.presets.size() == 3, "rows6 %zu rows6c %zu presets %zu", rows6, rows6c, g.presets.size() );
	EdidSerial es = ProfileGen->gen_edid_serial( ctx, { "1804", "n" } );
	const uint32_t ev = le32_of_decimal( es.text );
	TEST_CHECK( ev >= 1000 && ev <= 9999 && ev != 1804 );
	EdidBlock bad = in.blocks[ 1 ];
	bad.bytes[ 127 ] ^= 0x55;
	EdidBlock fixed = ProfileGen->gen_edid_checksum( ctx, bad );
	TEST_CHECK( fixed.bytes == in.blocks[ 1 ].bytes );
}

// rule 7a: ProductId groups
static void pg_product_id_groups( void )
{
	BuildContext ctx;
	ctx = pg_fresh( Environment::bare_host );
	ProductId p = ProfileGen->gen_product_id( ctx, { "00330-12345-67890-AA123", "pid" } );
	const std::string pt = p.text;
	TEST_CHECK( pt.size() == 23 && pt.substr( 0, 6 ) == "00330-" && pt.substr( 17 ) == "-AA123" && pt != "00330-12345-67890-AA123" );
	TEST_CHECK( std::regex_match( p.text, std::regex( "00330-[0-9]{5}-[0-9]{5}-AA123" ) ) );
	bool threw = false;
	try { ProfileGen->gen_product_id( ctx, { "12345", "x" } ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
}

// the fwid 1.3 os_ids.txt fixture (made-up values) and the fwid 1.4 files
static std::string pg_os_ids( const std::string& extra = "" )
{
	return "## registry identifiers\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Cryptography \"MachineGuid\" REG_SZ, 74 bytes [IDENTIFIER - keep local]: \"1b2c3d4e-5f60-4718-9a0b-c1d2e3f4a5b6\"\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"ProductId\" REG_SZ, 48 bytes [IDENTIFIER - keep local]: \"00330-12345-67890-AA123\"\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"InstallDate\" REG_DWORD, 4 bytes [machine-specific]: 0x64A0B1C2 (1688252866)\n"
		   "    = 2023-07-01T23:07:46Z (seconds since 1970-01-01 UTC)\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"InstallTime\" REG_QWORD, 8 bytes [machine-specific]: 0x01D9AC70D901E387 (133327264661234567)\n"
		   "    = 2023-07-01T23:07:46Z (FILETIME)\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"RegisteredOwner\" REG_SZ, 12 bytes [IDENTIFIER - keep local]: \"Fixer\"\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"RegisteredOrganization\" REG_SZ, 2 bytes [IDENTIFIER - keep local]: \"\"\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"BuildGUID\" REG_SZ, 74 bytes [build identifier]: \"ffffffff-ffff-ffff-ffff-ffffffffffff\"\n"
		   "HKLM\\SYSTEM\\CurrentControlSet\\Control\\IDConfigDB\\Hardware Profiles\\0001 \"HwProfileGuid\" REG_SZ, 78 bytes [IDENTIFIER - keep local]: \"{0a1b2c3d-1111-11ef-8222-806e6f6e6963}\"\n"
		   "HKLM\\SOFTWARE\\Microsoft\\SQMClient \"MachineId\" REG_SZ, 78 bytes [IDENTIFIER - keep local]: \"{ABCDEF01-2345-4678-9ABC-DEF012345678}\"\n"
		   "## current user\n"
		   "GetUserNameW: \"Fixer\" [IDENTIFIER - keep local]\n"
		   "GetUserNameExW(NameSamCompatible): \"DESKTOP-FIXTUR2\\x5CFixer\" [IDENTIFIER - keep local] (only this format is queried)\n"
		   "user SID: S-1-5-21-1111111111-222222222-3333333333-1001 [IDENTIFIER - keep local]\n"
		   "account domain prefix: S-1-5-21-1111111111-222222222-3333333333 [IDENTIFIER - keep local]\n"
		   "  account domain from NameSamCompatible: \"DESKTOP-FIXTUR2\"; computer name: \"DESKTOP-FIXTUR2\"\n"
		   "## volumes\n"
		   "C:\\  type 3 (DRIVE_FIXED)\n"
		   "  volume serial number: 1A2B-3C4D (0x1A2B3C4D) [IDENTIFIER - keep local]\n"
		   "  volume GUID path: \\\\?\\Volume{87654321-0000-11f0-a000-806e6f6e6963}\\ [IDENTIFIER - keep local]\n"
		   "D:\\  type 3 (DRIVE_FIXED)\n"
		   "  volume serial number: 5E6F-7A8B (0x5E6F7A8B) [IDENTIFIER - keep local]\n"
		   "  volume GUID path: \\\\?\\Volume{3fa81c55-0000-0000-0000-100000000000}\\ [IDENTIFIER - keep local]\n" + extra;
}
// the fwid 1.4 files in fwid_probe 1.4's formats (made-up values): two license rows, the SusClientIdValidation blob (inline
// hex, or the hex-dump form), DiagTrack values, three disks (a padded serial, a repeated-digit placeholder, a USB flash
// drive reporting the start of its USB serial), HID devices (= a USB serial, a placeholder, a real serial whose USB serial is
// a placeholder, none), the GPU review marks, Bluetooth device / radio subkeys, an audio endpoint, the Wi-Fi interface,
// the workgroup, a WSD printer port
static const char* const k_pg_iid0 = "123456712345671234567123456712345671234567123456712345671234567";
static const char* const k_pg_iid1 = "765432176543217654321765432176543217654321765432176543217654321";
static const char* const k_pg_flash_usb = "0401a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f6";	// a USB flash drive's (made-up) USB serial
static Bytes pg_sus_blob()
{
	// 06 02 len1 01 len2 len3 | UTF-16LE disk serial | 06 + the wired fixture NIC's MAC | UTF-16LE board serial (the SMBIOS
	// fixture's type 2 serial) | UTF-16LE "Default string"
	auto u16 = []( const std::string& s ) { Bytes b; for ( char c : s ) { b.push_back( uint8_t( c ) ); b.push_back( 0 ); } return b; };
	Bytes s1 = u16( "S5GXNF0R123456K" ), s2 = u16( "AB12cd-345_Z" ), s3 = u16( "Default string" );
	Bytes b = { 0x06, 0x02, uint8_t( s1.size() ), 0x01, uint8_t( s2.size() ), uint8_t( s3.size() ) };
	b.insert( b.end(), s1.begin(), s1.end() );
	for ( uint8_t x : { 0x06, 0xA0, 0xB1, 0xC2, 0x01, 0x02, 0x03 } ) b.push_back( x );
	b.insert( b.end(), s2.begin(), s2.end() );
	b.insert( b.end(), s3.begin(), s3.end() );
	return b;
}
static std::string pg_update_ids( bool dump )
{
	const Bytes b = pg_sus_blob();
	std::string head = "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate \"SusClientIdValidation\" REG_BINARY, " + std::to_string( b.size() ) + " bytes [IDENTIFIER - keep local]: ";
	if ( !dump )
	{
		for ( uint8_t x : b ) { char h[ 3 ]; snprintf( h, sizeof h, "%02X", x ); head += h; }
		head += " (" + std::to_string( b.size() ) + " bytes hex)\n";
	}
	else
	{
		head += "(" + std::to_string( b.size() ) + " bytes)\n";
		for ( size_t o = 0; o < b.size(); o += 16 )
		{
			char h[ 16 ];
			snprintf( h, sizeof h, "    %04zX: ", o );
			std::string hex = h, asc;
			for ( size_t k = o; k < o + 16; k++ )
			{
				if ( k < b.size() ) { snprintf( h, sizeof h, "%02X ", b[ k ] ); hex += h; asc += b[ k ] >= 0x20 && b[ k ] < 0x7F ? char( b[ k ] ) : '.'; }
				else hex += "   ";
			}
			head += hex + " |" + asc + "|\n";
		}
	}
	return "# fwid update / telemetry identifier capture - fixture\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate \"SusClientId\" REG_SZ, 74 bytes [IDENTIFIER - keep local]: \"1234abcd-5678-4ef0-9abc-def012345678\"\n" +
		   head +
		   "\n## HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Diagnostics\\DiagTrack\n"
		   "  value \"TimeStampInterval\"  REG_DWORD (4)  4 bytes: 0x00000001 (1)\n"
		   "  value \"LaunchCount\"  REG_QWORD (11)  8 bytes: 0x0000000000000021 (33)\n"
		   "  value \"LastPersistedEventTime\"  REG_QWORD (11)  8 bytes: 0x01DC7AB0DFD64AB8 (134116989002468024)\n"
		   "\n## HKLM\\SOFTWARE\\Microsoft\\SQMClient\n"
		   "  value \"WinSqmFirstSessionStartTime\"  REG_QWORD (11)  8 bytes: 0x01D9AC793AC82D59 (133327300661357913)\n"
		   "  value \"MachineId\"  REG_SZ (1)  78 bytes: \"{ABCDEF01-2345-4678-9ABC-DEF012345678}\"\n";
}
static std::string pg_disks()
{
	return "# fwid physical disk capture - fixture. IDENTIFIERS: the serial number is marked; keep local.\n\n"
		   "=== PhysicalDrive0\n  vendor ID:        \"FIXDISK\" (offset 40, 7 bytes)\n"
		   "  serial number:    \"S5GXNF0R123456K    \" (offset 103, 19 bytes)  [IDENTIFIER - keep local]\n\n"
		   "=== PhysicalDrive1\n  serial number:    \"00000000000000000000\" (offset 71, 20 bytes)  [IDENTIFIER - keep local]\n\n"
		   "=== PhysicalDrive2\n  serial number:    \"0401A1B2C3D4E5F60718\" (offset 103, 20 bytes)  [IDENTIFIER - keep local]\n\n"
		   "=== PhysicalDrive3\n  serial number:    (offset 0: not reported)\n";
}
static std::string pg_hid()
{
	return "# fwid HID capture - fixture\n"
		   "=== HID device 0\npath: \"\\\\?\\hid#vid_1111&pid_2222&mi_00#8&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}\" [IDENTIFIER - keep local]\n"
		   "manufacturer: \"Fixture Corp\"\nproduct: \"Fixture Mouse\"\nserial:       \"4142433132333435\"  [IDENTIFIER - keep local]\n"
		   "=== HID device 1\npath: \"\\\\?\\hid#vid_3333&pid_4444#8&2b3c4d5e&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}\" [IDENTIFIER - keep local]\n"
		   "serial:       \"000000000000\"  [IDENTIFIER - keep local]\n"
		   "=== HID device 2\npath: \"\\\\?\\hid#vid_5555&pid_6666#8&3c4d5e6f&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}\" [IDENTIFIER - keep local]\n"
		   "serial:       \"987654321098\"  [IDENTIFIER - keep local]\n"
		   "=== HID device 3\nserial:       \"\"  [IDENTIFIER - keep local]\n";
}
static std::vector<TextFile> pg_text_fixture3( bool sus_dump = false )
{
	auto tf = []( const char* name, const std::string& s ) { return TextFile { name, Bytes( s.begin(), s.end() ) }; };
	// a registry last-write time in the same second as a device's first install: not an install event, never shifted
	auto files = pg_text_fixture2( pg_usb( "VID_1111&PID_2222", "4142433132333435" ) + "  depth 2; last write 2023-07-01T23:06:46Z; 0 value(s), 0 subkey(s)\n" +
								   pg_usb( "VID_5555&PID_6666", "000000000000" ) + pg_usb( "VID_0781&PID_5581", k_pg_flash_usb ) );
	// the install history of a device (first install 60 s before InstallDate - Windows setup -, install 30 s after, last
	// arrival 10 min before the capture) and its driver date (not shifted)
	const std::string dev = "    {83DA6326-97A6-4088-9453-A1923F573B29} pid 100 DEVPKEY_Device_InstallDate [machine-specific] type 0x10: 2023-07-01T23:08:16Z (0x01D9AC70EB257563)\n"
							"    {83DA6326-97A6-4088-9453-A1923F573B29} pid 101 DEVPKEY_Device_FirstInstallDate [machine-specific] type 0x10: 2023-07-01T23:06:46Z (0x01D9AC70B56F981C)\n"
							"    {83DA6326-97A6-4088-9453-A1923F573B29} pid 102 DEVPKEY_Device_LastArrivalDate [machine-specific] type 0x10: 2025-12-31T23:50:00Z (0x01DC7AB02D45FDAA)\n"
							"    {A8B865DD-2E3D-4094-AD97-E593A70C75D6} pid 2 DEVPKEY_Device_DriverDate [machine-specific] type 0x10: 2006-06-21T00:00:00Z (0x01C694C5A38C8000)\n";
	files[ 3 ].data.insert( files[ 3 ].data.end(), dev.begin(), dev.end() );
	pg_put( files, "os_ids.txt", pg_os_ids() );
	files.push_back( tf( "license.txt", std::string( "# fwid license capture - fixture\n# OfflineInstallationId = the Windows installation ID [IDENTIFIER - keep local].\n\n"
		"=== SoftwareLicensingProduct row 0\n"
		"ID                     \"f0f0f0f0-aaaa-4bbb-8ccc-dddddddddddd\"  [SKU ID]\n"
		"Name                   \"Windows(R), Professional edition\"\n"
		"Description            \"Windows(R) Operating System, RETAIL channel\"\n"
		"LicenseStatus          1 = Licensed (decode: public knowledge, unverified locally)\n"
		"ProductKeyChannel      \"Retail\"\n"
		"OfflineInstallationId  \"" ) + k_pg_iid0 + "\"  [IDENTIFIER - installation ID, keep local]\n\n"
		"=== SoftwareLicensingProduct row 1\n"
		"ID                     \"e1e1e1e1-bbbb-4ccc-8ddd-eeeeeeeeeeee\"  [SKU ID]\n"
		"Name                   \"Windows(R), Client-ESU-Year1 add-on for Fixture\"\n"
		"Description            \"Windows(R) Operating System, VOLUME_MAK channel\"\n"
		"LicenseStatus          1 = Licensed (decode: public knowledge, unverified locally)\n"
		"ProductKeyChannel      \"Volume:MAK\"\n"
		"OfflineInstallationId  \"" + k_pg_iid1 + "\"  [IDENTIFIER - installation ID, keep local]\n\n2 row(s); query time 1 ms\n" ) );
	files.push_back( tf( "update_ids.txt", pg_update_ids( sus_dump ) ) );
	files.push_back( tf( "disks.txt", pg_disks() ) );
	files.push_back( tf( "hid.txt", pg_hid() ) );
	files.push_back( tf( "gpu.txt", "# [IDENTIFIER?] marks values that look per-device unique (for review).\n"
									"## HKLM\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000\n"
									"  value \"HardwareInformation.AdapterString\"  REG_SZ (1)  24 bytes: \"Fixture GPU\"\n"
									"  value \"UserModeDriverGUID\"  REG_SZ (1)  78 bytes: \"{a1a2a3a4-b4a5-4968-8776-655443322110}\"\n"
									"      [IDENTIFIER?] review: per-device-unique-looking value (GUID other than the class GUID, or a serial / unique-ID name)\n"
									"  value \"KmFileIoBaseFilePath\"  REG_SZ (1)  166 bytes: \"C:\\x5CWindows\\x5CSystem32\\x5CDriverState\\x5CDevices\\x5C{b1b2b3b4-c4c5-4d6e-8f70-123456789abc}\\x5CData\"\n"
									"      [IDENTIFIER?] review: per-device-unique-looking value (GUID other than the class GUID, or a serial / unique-ID name)\n"
									"  value \"BoardSerial\"  REG_SZ (1)  22 bytes: \"GPU12345XY\"\n"
									"      [IDENTIFIER?] review: per-device-unique-looking value (GUID other than the class GUID, or a serial / unique-ID name)\n" ) );
	files.push_back( tf( "bluetooth.txt", "# fwid Bluetooth capture - fixture: each subkey name = a paired device address [IDENTIFIER]\n\n"
										  "## HKLM\\SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters\\Devices\n  depth 0; 0 value(s), 1 subkey(s)\n  subkey \"701ab8112233\"\n\n"
										  "## HKLM\\SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters\\Devices\\701ab8112233\n"
										  "  value \"Name\"  REG_BINARY (3)  15 bytes: 46697874757265204865616473657400 (15 bytes hex)\n      Name as UTF-8: \"Fixture Headset\"\n"
										  "  value \"LastSeen\"  REG_QWORD (11)  8 bytes: 0x01D9C403D164AD1E (133353184661122334)\n"
										  "  subkey \"ServicesFor3ca9f4102034\" (not descended: depth limit)\n\n"
										  "## HKLM\\SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters\\LocalInfo\n" ) );
	files.push_back( tf( "audio.txt", "=== endpoint {a1b2c3d4-e5f6-4789-8abc-def012345678} [IDENTIFIER]\n  name: \"Fixture Speakers\"\n" ) );
	files.push_back( tf( "wifi.txt", "=== interface 0: GUID {1E1E1E1E-1111-4222-8333-444455556666} [IDENTIFIER], profiles 3\n" ) );
	files.push_back( tf( "environment.txt", "## join state (NetGetJoinInformation, local)\nresult 0; status 2 (NetSetupWorkgroupName); name \"FIXGROUP\" [IDENTIFIER: workgroup / domain name]\n"
											"## uptime\nGetTickCount64: 1234 ms (0.00 h) [volatile]\n" ) );
	files.push_back( tf( "printers.txt", "=== printer 0\nname \"Fixture Printer\"\nport \"WSD-c1c2c3c4-d4d5-4e6f-8a7b-0123456789ab\"\ndriver \"Microsoft IPP Class Driver\"\n" ) );
	files.push_back( tf( "chid.txt", "HardwareID-0 [IDENTIFIER]: {01234567-89ab-cdef-0123-456789abcdef}\n" ) );
	return files;
}
// the SMBIOS rule-2 mapping gen_text_identity needs for rule 8c (the fixture's type 2 serial)
static std::vector<std::pair<std::string, std::string>> pg_smbios_strings() { return { { "AB12cd-345_Z", "XY34ab-567_Q" } }; }

// rule 7: SID consistency, NameSamCompatible, one user name everywhere, the install instant; volume GUID consistency
static void pg_os_ids_rules( void )
{
	BuildContext ctx;
	auto files = pg_text_fixture3();
	ctx = pg_fresh( Environment::bare_host );
	TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files, {}, {}, {}, pg_smbios_strings() } );
	const IdentityEntry* ms = pg_find( t.identity, "machine_sid", "S-1-5-21-1111111111-222222222-3333333333" );
	const IdentityEntry* us = pg_find( t.identity, "user_sid", "S-1-5-21-1111111111-222222222-3333333333-1001" );
	TEST_ASSERT( ms != nullptr && us != nullptr );
	TEST_CHECK_( us->synthetic == ms->synthetic + "-1001", "the user SID = the synthetic machine prefix + the exact RID" );
	TEST_CHECK( std::regex_match( ms->synthetic, std::regex( "S-1-5-21-[1-9][0-9]{9}-[1-9][0-9]{8}-[1-9][0-9]{9}" ) ) && ms->synthetic != ms->original );
	const IdentityEntry* un = pg_find( t.identity, "user_name", "Fixer" );
	const IdentityEntry* cn = pg_find( t.identity, "computer_name", "DESKTOP-FIXTUR2" );
	const IdentityEntry* ns = pg_find( t.identity, "name_sam_compatible", "DESKTOP-FIXTUR2\\Fixer" );
	TEST_ASSERT( un != nullptr && cn != nullptr && ns != nullptr );
	TEST_CHECK( std::regex_match( un->synthetic, std::regex( "[A-Z][a-z]{4}" ) ) && un->synthetic != "Fixer" );
	TEST_CHECK_( ns->synthetic == cn->synthetic + "\\" + un->synthetic, "NameSamCompatible = synthetic computer name \\ synthetic user" );
	TEST_CHECK_( un->occurrences >= 4, "user name: RegisteredOwner, GetUserNameW, NameSamCompatible (after \\x5C), the profile path; got %zu", un->occurrences );
	TEST_CHECK( std::count_if( t.identity.begin(), t.identity.end(), []( const IdentityEntry& r ) { return r.kind == "user_name"; } ) == 1 );
	const IdentityEntry* d = pg_find( t.identity, "install_date", "1688252866" );
	const IdentityEntry* it = pg_find( t.identity, "install_time", "133327264661234567" );
	TEST_ASSERT( d != nullptr && it != nullptr );
	long long sd = std::stoll( d->synthetic );
	unsigned long long st = std::stoull( it->synthetic );
	InstallWindow w = ProfileGenerator::parse_install_window( 19045, "2026-01-01T00:00:00.000Z" );
	TEST_CHECK_( sd > w.lo && sd < w.hi, "the install instant lies strictly between the build release and the capture" );
	TEST_CHECK_( ( st / 10000000ull ) - 11644473600ull == (unsigned long long)sd, "InstallTime = the same instant as InstallDate" );
	TEST_CHECK( d->occurrences == 2 );	// hex, decimal; the ISO form is its own install-history identity
	const IdentityEntry* iso = pg_find( t.identity, "event_iso", "2023-07-01T23:07:46Z" );
	TEST_ASSERT( iso != nullptr );
	TEST_CHECK( iso->occurrences == 2 && iso->synthetic.size() == 20 && iso->synthetic != iso->original );	// the InstallDate and InstallTime lines (same second)
	const IdentityEntry* v = pg_find( t.identity, "volume_guid", "{87654321-0000-11F0-A000-806E6F6E6963}" );
	TEST_ASSERT( v != nullptr );
	bool in_enum = false, in_os = false;
	for ( const FieldMapEntry& f : t.fields ) if ( f.id == v->id ) { in_enum |= f.area == "enum_registry.txt"; in_os |= f.area == "os_ids.txt"; }
	TEST_CHECK_( in_enum && in_os, "one volume GUID identity for both appearances" );
	std::string vl = v->synthetic.substr( 1, 36 );
	for ( char& ch : vl ) ch = char( tolower( uint8_t( ch ) ) );
	bool path_same = false;
	for ( const StoragePath& p : t.storage_paths ) if ( p.original.find( "87654321" ) != std::string::npos ) path_same = p.synthetic.find( vl ) != std::string::npos;
	TEST_CHECK( path_same );
	const IdentityEntry* mbr = pg_find( t.identity, "volume_guid", "{3FA81C55-0000-0000-0000-100000000000}" );
	TEST_ASSERT( mbr != nullptr );
	TEST_CHECK( mbr->synthetic.substr( 9 ) == "-0000-0000-0000-100000000000}" );
	const IdentityEntry* vs = pg_find( t.identity, "volume_serial", "1A2B-3C4D" );
	TEST_ASSERT( vs != nullptr );
	TEST_CHECK( std::regex_match( vs->synthetic, std::regex( "[0-9A-F]{4}-[0-9A-F]{4}" ) ) && vs->synthetic != "1A2B-3C4D" && vs->occurrences == 2 );
	const IdentityEntry* mg = pg_find( t.identity, "machine_guid", "1b2c3d4e-5f60-4718-9a0b-c1d2e3f4a5b6" );
	TEST_ASSERT( mg != nullptr );
	TEST_CHECK( std::regex_match( mg->synthetic, std::regex( "[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}" ) ) && mg->synthetic != mg->original );
	const IdentityEntry* hp = pg_find( t.identity, "hw_profile_guid", "{0a1b2c3d-1111-11ef-8222-806e6f6e6963}" );
	TEST_ASSERT( hp != nullptr );
	TEST_CHECK( hp->synthetic.substr( 25 ) == "806e6f6e6963}" && hp->synthetic.front() == '{' );
	TEST_CHECK( pg_find( t.identity, "product_id", "00330-12345-67890-AA123" ) != nullptr );
	ctx = pg_fresh( Environment::bare_host );
	SidParts sp = ProfileGen->gen_sid( ctx, { "S-1-5-21-1111111111-222222222-3333333333", "sid" } );
	TEST_CHECK( std::string( sp.prefix ).rfind( "S-1-5-21-", 0 ) == 0 && std::string( sp.prefix ).size() == 40 );
	InstallTime itr;
	itr.os_build = 19045;
	itr.capture_utc = "2026-01-01T00:00:00Z";
	itr.unix_seconds = 1688252866;
	itr.filetime = 133327264661234567ull;
	ctx = pg_fresh( Environment::bare_host );
	InstallTime its = ProfileGen->gen_install_time( ctx, itr );
	TEST_CHECK( its.unix_seconds > w.lo && its.unix_seconds < w.hi && its.filetime / 10000000ull - 11644473600ull == uint64_t( its.unix_seconds ) );
	TEST_CHECK( its.shift_seconds <= -86400 && its.unix_seconds == itr.unix_seconds + its.shift_seconds && its.filetime % 10000000ull == itr.filetime % 10000000ull );
}

// a marked identifier line no rule consumes stops the run; so does a non-empty RegisteredOrganization or a marked line in
// a file whose content is exact (wifi.txt)
static void pg_os_ids_guard( void )
{
	BuildContext ctx;
	auto run = [ & ]( const char* name, const std::string& body ) {
		auto files = pg_text_fixture2( "" );
		pg_put( files, name, body );
		ctx = pg_fresh( Environment::bare_host );
		try { ProfileGen->gen_text_identity( ctx, { files } ); }
		catch ( const std::exception& e ) { return std::string( e.what() ).find( "needs a field decision" ) != std::string::npos; }
		return false;
	};
	TEST_CHECK( run( "os_ids.txt", pg_os_ids( "HKLM\\SOFTWARE\\Fixture \"FooId\" REG_SZ, 10 bytes [IDENTIFIER - keep local]: \"abc\"\n" ) ) );
	TEST_CHECK( run( "os_ids.txt", "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"RegisteredOrganization\" REG_SZ, 8 bytes [IDENTIFIER - keep local]: \"FixOrg\"\n" ) );
	TEST_CHECK( run( "wifi.txt", "profile name [IDENTIFIER - keep local]: \"FixtureNet\"\n" ) );
	TEST_CHECK( !run( "os_ids.txt", pg_os_ids() ) );
}

// rule 8: the Windows installation ID (its own gen_, license data exact), update IDs, disk / HID / GPU / Bluetooth / audio
static void pg_fwid14_rules( void )
{
	BuildContext ctx;
	ctx = pg_fresh( Environment::bare_host );
	WinInstallationId wi;
	wi.text = "1234567-2345678-3456789-4567890-5678901-6789012-7890123-8901234-9012345";	// with separators: kept
	wi.label = "iid";
	wi.license.license_status = "1";
	wi.license.product_key_channel = "Retail";
	WinInstallationId so = ProfileGen->gen_win_installation_id( ctx, wi );
	TEST_CHECK( strlen( so.text ) == strlen( wi.text ) && std::string( so.text ) != wi.text && so.check_digits_modelled && so.rule == "8a" );
	TEST_CHECK( std::regex_match( so.text, std::regex( "([0-9]{7}-){8}[0-9]{7}" ) ) && so.group_lengths == std::vector<size_t>( 9, 7 ) );
	TEST_CHECK( so.license.product_key_channel == "Retail" && so.license.license_status == "1" );
	WinInstallationId w63;	// fwid 1.4 prints the 63 digits without separators: 9 groups of 7
	w63.text = k_pg_iid0;
	w63.label = "iid63";
	WinInstallationId s63 = ProfileGen->gen_win_installation_id( ctx, w63 );
	TEST_CHECK( std::regex_match( s63.text, std::regex( "[0-9]{63}" ) ) && std::string( s63.text ) != w63.text && s63.group_lengths == std::vector<size_t>( 9, 7 ) );
	// every group of every synthetic installation ID passes its check digit (d0 + 2 d1 + d2 + 2 d3 + d4 + 2 d5) mod 7 (user decision
	// 2026-10-17); 200 fresh ones from the Randomizer, group by group
	for ( int n = 0; n < 200; n++ )
	{
		const std::string iid = Randomize->gen_installation_id_like( k_pg_iid1 );
		TEST_ASSERT( iid.size() == 63 );
		for ( size_t g = 0; g < 63; g += 7 )
		{
			unsigned sum = 0;
			for ( size_t i = 0; i < 6; i++ ) sum += unsigned( iid[ g + i ] - '0' ) * ( i & 1 ? 2 : 1 );
			TEST_CHECK_( iid[ g + 6 ] == char( '0' + sum % 7 ), "IID %d group %zu: check digit", n, g / 7 );
		}
		TEST_CHECK( ProfileGenerator::check_installation_id( iid.c_str() ) );
	}
	TEST_CHECK( ProfileGenerator::check_installation_id( so.text ) && ProfileGenerator::check_installation_id( s63.text ) );
	TEST_CHECK( !ProfileGenerator::check_installation_id( k_pg_iid0 ) && !ProfileGenerator::check_installation_id( "12345" ) );	// the fixture's made-up ID fails
	TEST_CHECK( ProfileGen->gen_win_installation_id( ctx, w63 ).text == s63.text );	// the same key: the value already taken (the identity map)
	for ( bool dump : { false, true } )
	{
		auto files = pg_text_fixture3( dump );
		TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files, ProfileGenerator::parse_usb_serial_table( { files[ 2 ].data } ), {}, {}, pg_smbios_strings() } );
		// 8a: one installation ID per SoftwareLicensingProduct row, with that row's exact license data
		TEST_ASSERT( t.installation_ids.size() == 4 );
		TEST_CHECK( t.installation_ids[ 0 ].license.product_key_channel == "Retail" && t.installation_ids[ 0 ].license.name == "Windows(R), Professional edition" &&
					t.installation_ids[ 0 ].license.license_status == "1" && t.installation_ids[ 0 ].license.sku_id == "f0f0f0f0-aaaa-4bbb-8ccc-dddddddddddd" );
		TEST_CHECK( t.installation_ids[ 2 ].license.product_key_channel == "Volume:MAK" && t.installation_ids[ 2 ].license.description == "Windows(R) Operating System, VOLUME_MAK channel" );
		TEST_CHECK( t.installation_ids[ 1 ].group_lengths == std::vector<size_t>( 9, 7 ) && t.installation_ids[ 1 ].check_digits_modelled && ProfileGenerator::check_installation_id( t.installation_ids[ 1 ].text ) );
		for ( const char* iid : { k_pg_iid0, k_pg_iid1 } )
		{
			const IdentityEntry* ii = pg_find( t.identity, "win_installation_id", iid );
			TEST_ASSERT( ii != nullptr );
			TEST_CHECK( ii->rule == "8a" && std::regex_match( ii->synthetic, std::regex( "[0-9]{63}" ) ) && ii->synthetic != ii->original && ii->occurrences == 1 );
		}
		for ( const TextFile& f : files )
			if ( f.name == "license.txt" ) TEST_CHECK( ProfileGen->parse_license_rows( f ).size() == 2 );
		// 4c: SusClientId (own style), the SQM MachineId (one identity with os_ids.txt)
		const IdentityEntry* sus = pg_find( t.identity, "sus_client_id", "1234abcd-5678-4ef0-9abc-def012345678" );
		TEST_ASSERT( sus != nullptr );
		TEST_CHECK( sus->synthetic.size() == 36 && sus->synthetic[ 14 ] == '4' && sus->synthetic != sus->original && std::none_of( sus->synthetic.begin(), sus->synthetic.end(), []( char c ) { return c >= 'A' && c <= 'F'; } ) );
		const IdentityEntry* sqm = pg_find( t.identity, "sqm_machine_id", "{ABCDEF01-2345-4678-9ABC-DEF012345678}" );
		TEST_ASSERT( sqm != nullptr );
		TEST_CHECK( sqm->occurrences == 2 );
		// 8c: the layout kept, every component its synthetic identity; the dump form gives bytes + ASCII rows per dump line
		const Bytes real = pg_sus_blob();
		const IdentityEntry* val = nullptr;
		for ( const IdentityEntry& r : t.identity ) if ( r.kind == "sus_client_id_validation" ) val = &r;
		TEST_ASSERT( val != nullptr );
		TEST_ASSERT( val->synthetic.size() == real.size() * 2 );
		Bytes sb;
		for ( size_t i = 0; i < val->synthetic.size(); i += 2 ) sb.push_back( uint8_t( std::stoul( val->synthetic.substr( i, 2 ), nullptr, 16 ) ) );
		SusValidationLayout ls = ProfileGenerator::parse_sus_client_id_validation( sb ), lr = ProfileGenerator::parse_sus_client_id_validation( real );
		TEST_ASSERT( ls.ok && lr.ok );
		const IdentityEntry* ds = pg_find( t.identity, "disk_serial", "S5GXNF0R123456K" );
		const IdentityEntry* wired = pg_find( t.identity, "mac", "A0:B1:C2:01:02:03" );
		TEST_ASSERT( ds != nullptr && wired != nullptr );
		std::string wm;
		for ( int i = 0; i < 6; i++ ) { char h[ 4 ]; snprintf( h, sizeof h, i ? ":%02X" : "%02X", ls.mac[ i ] ); wm += h; }
		TEST_CHECK_( ls.disk_serial == ds->synthetic && wm == wired->synthetic && ls.board_serial == "XY34ab-567_Q" && ls.other_serial == "Default string",
			"SusClientIdValidation: string 1 = the synthetic disk serial, MAC = the synthetic NIC MAC, string 2 = the synthetic board serial, string 3 filler" );
		TEST_CHECK( std::equal( sb.begin(), sb.begin() + 6, real.begin() ) );
		TEST_CHECK( val->occurrences == ( dump ? 2 * ( ( real.size() + 15 ) / 16 ) : 1u ) );
		// 8d: disk serials without padding; the placeholder exact; the USB flash drive = the start of its USB serial's synthetic value
		TEST_CHECK( std::regex_match( ds->synthetic, std::regex( "[A-Z][0-9][A-Z]{4}[0-9][A-Z][0-9]{6}[A-Z]" ) ) );
		TEST_CHECK( !pg_find( t.identity, "disk_serial", "00000000000000000000" ) && t.filler_serials >= 2 );
		const IdentityEntry* flash = pg_find( t.identity, "disk_serial", "0401A1B2C3D4E5F60718" );
		const IdentityEntry* fusb = pg_find( t.identity, "usb_serial", k_pg_flash_usb );
		TEST_ASSERT( flash != nullptr && fusb != nullptr );
		std::string want = fusb->synthetic.substr( 0, 20 );
		for ( char& c : want ) c = char( toupper( uint8_t( c ) ) );
		TEST_CHECK_( flash->synthetic == want, "the flash drive's disk serial = the start of its USB serial's synthetic value (case of the disk serial)" );
		bool rel = false;
		for ( const FieldMapEntry& f : t.fields ) rel |= f.rule == "8dr" && f.id == flash->id;
		TEST_CHECK( rel );
		// 4d: the HID serial = the USB serial (one identity); a placeholder exact; a real HID serial whose USB serial is a
		// placeholder: synthetic; no serial: nothing
		const IdentityEntry* usb = pg_find( t.identity, "usb_serial", "4142433132333435" );
		TEST_ASSERT( usb != nullptr );
		TEST_CHECK( !pg_find( t.identity, "hid_serial", "4142433132333435" ) && !pg_find( t.identity, "hid_serial", "000000000000" ) && !pg_find( t.identity, "usb_serial", "000000000000" ) );
		bool in_hid = false;
		for ( const FieldMapEntry& f : t.fields ) in_hid |= f.id == usb->id && f.area == "hid.txt";
		TEST_CHECK_( in_hid, "the HID serial = the USB serial: one identity, occurrences in hid.txt too" );
		const IdentityEntry* hs = pg_find( t.identity, "hid_serial", "987654321098" );
		TEST_ASSERT( hs != nullptr );
		TEST_CHECK( std::regex_match( hs->synthetic, std::regex( "[0-9]{12}" ) ) && hs->synthetic != hs->original );
		// 8e: UserModeDriverGUID exact (a driver-package constant), the DriverState GUID rule 4c, another value rule-2 classes
		TEST_CHECK( !pg_find( t.identity, "gpu_guid", "{a1a2a3a4-b4a5-4968-8776-655443322110}" ) );
		TEST_CHECK( pg_find( t.identity, "gpu_guid", "{b1b2b3b4-c4c5-4d6e-8f70-123456789abc}" ) != nullptr && pg_find( t.identity, "gpu_unique", "GPU12345XY" ) != nullptr );
		// 3: the paired device (subkey names) OUI kept; the local radio ("ServicesFor<address>") = its adapter MAC identity
		const IdentityEntry* bt = pg_find( t.identity, "bt_device", "70:1A:B8:11:22:33" );
		TEST_ASSERT( bt != nullptr );
		TEST_CHECK( bt->synthetic.substr( 0, 9 ) == "70:1A:B8:" && bt->synthetic != bt->original && bt->occurrences == 2 );
		TEST_CHECK_( !pg_find( t.identity, "bt_device", "3C:A9:F4:10:20:34" ), "the local radio address is its adapter MAC identity (Bluetooth PAN = Wi-Fi + 4)" );
		const IdentityEntry* pan = pg_find( t.identity, "mac", "3C:A9:F4:10:20:34" );
		TEST_ASSERT( pan != nullptr );
		bool in_bt = false;
		for ( const FieldMapEntry& f : t.fields ) in_bt |= f.id == pan->id && f.area == "bluetooth.txt" && f.length == 12;
		TEST_CHECK_( in_bt, "\"ServicesFor<address>\": the radio address after the prefix is an occurrence" );
		// 4c: the audio endpoint, the Wi-Fi interface (= the network interface GUID), the WSD port; the workgroup exact
		TEST_CHECK( pg_find( t.identity, "audio_endpoint", "{A1B2C3D4-E5F6-4789-8ABC-DEF012345678}" ) != nullptr );
		TEST_CHECK( !pg_find( t.identity, "wlan_interface_guid", "{1E1E1E1E-1111-4222-8333-444455556666}" ) );
		const IdentityEntry* wifi = pg_find( t.identity, "interface_guid", "{1E1E1E1E-1111-4222-8333-444455556666}" );
		TEST_ASSERT( wifi != nullptr );
		bool in_wifi = false;
		for ( const FieldMapEntry& f : t.fields ) in_wifi |= f.id == wifi->id && f.area == "wifi.txt";
		TEST_CHECK( in_wifi );
		const IdentityEntry* wsd = pg_find( t.identity, "printer_wsd_port", "{C1C2C3C4-D4D5-4E6F-8A7B-0123456789AB}" );
		TEST_ASSERT( wsd != nullptr );
		TEST_CHECK( wsd->occurrences == 1 && wsd->synthetic != wsd->original );
		for ( const IdentityEntry& r : t.identity ) TEST_CHECK( r.original != "FIXGROUP" && r.original != "Fixture GPU" && r.original != "Fixture Headset" );	// exact
		// volatile.tsv: fwid's [volatile] mark + the DiagTrack counters / timestamps, not the TimeStampInterval setting
		std::set<std::string> vn;
		for ( const VolatileItem& v : t.volatiles ) vn.insert( v.name );
		TEST_CHECK_( vn == std::set<std::string>( { "GetTickCount64", "LaunchCount", "LastPersistedEventTime" } ), "volatile: %zu items", vn.size() );
	}
	// another SusClientIdValidation layout stops the run
	SusValidationInput bad { { 0x06, 0x01, 0x00, 0x01, 0x00, 0x00, 0x06, 1, 2, 3, 4, 5, 6 } };
	bool threw = false;
	try { ProfileGen->gen_sus_client_id_validation( ctx, bad ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "needs a field decision" ) != std::string::npos; }
	TEST_CHECK( threw );
	TEST_CHECK( ProfileGenerator::parse_sus_client_id_validation( pg_sus_blob() ).disk_serial == "S5GXNF0R123456K" );
}

// rule 8d on the raw STORAGE_DEVICE_DESCRIPTORs: the serial string (without padding) = its identity, every other byte exact;
// a placeholder and a missing serial stay; the serial elsewhere in the descriptor stops the run
static Bytes pg_descriptor( const char* serial, bool serial_twice = false )
{
	Bytes b( 160, 0 );
	auto put32 = [ & ]( size_t o, uint32_t v ) { for ( int i = 0; i < 4; i++ ) b[ o + i ] = uint8_t( v >> ( 8 * i ) ); };
	put32( 0, 40 );
	put32( 4, 160 );
	put32( 12, 40 );	// vendor
	memcpy( &b[ 40 ], "FIXDISK", 7 );
	put32( 16, 49 );	// product
	memcpy( &b[ 49 ], "FX SSD 1TB", 10 );
	if ( serial )
	{
		put32( 24, 103 );
		memcpy( &b[ 103 ], serial, strlen( serial ) );
	}
	put32( 28, 17 );	// BusTypeNvme
	if ( serial_twice ) memcpy( &b[ 140 ], "S5GXNF0R123456K", 15 );
	return b;
}
static void pg_disk_descriptors( void )
{
	BuildContext ctx;
	ctx = pg_fresh( Environment::bare_host );
	std::vector<IdentityEntry> ids = { { "K01", "disk_serial", "8d", "disks.txt:4", 1, "S5GXNF0R123456K", "Q7HZRA3T987654M" } };
	DiskInput in { { { "disk_0_descriptor.bin", pg_descriptor( "  S5GXNF0R123456K    " ) }, { "disk_1_descriptor.bin", pg_descriptor( "00000000000000000000" ) },
		{ "disk_2_descriptor.bin", pg_descriptor( nullptr ) } }, ids };
	DiskGen g = ProfileGen->gen_disk_descriptors( ctx, in );
	TEST_ASSERT( g.synthetic.size() == 3 && g.fields.size() == 1 );
	TEST_CHECK( g.serials == 1 && g.filler == 1 && g.none == 1 );
	const Bytes &a = in.descriptors[ 0 ].bytes, &s = g.synthetic[ 0 ].bytes;
	TEST_CHECK( g.fields[ 0 ].area == "disk\\disk_0_descriptor.bin" && g.fields[ 0 ].offset == 105 && g.fields[ 0 ].length == 15 && g.fields[ 0 ].rule == "8d" );
	TEST_CHECK( std::string( s.begin() + 105, s.begin() + 120 ) == "Q7HZRA3T987654M" );
	auto diffs = ProfileGenerator::check_diff_ranges( a, s );
	TEST_CHECK( diffs.size() >= 1 && diffs.front().first >= 105 && diffs.back().last <= 120 );
	TEST_CHECK( g.synthetic[ 1 ].bytes == in.descriptors[ 1 ].bytes && g.synthetic[ 2 ].bytes == in.descriptors[ 2 ].bytes );
	bool threw = false;
	try { ProfileGen->gen_disk_descriptors( ctx, { { { "disk_3_descriptor.bin", pg_descriptor( "S5GXNF0R123456K", true ) } }, ids } ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "needs a field decision" ) != std::string::npos; }
	TEST_CHECK( threw );
	threw = false;
	try { ProfileGen->gen_disk_descriptors( ctx, { { { "disk_4_descriptor.bin", pg_descriptor( "UNKNOWN99" ) } }, ids } ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "needs a field decision" ) != std::string::npos; }
	TEST_CHECK( threw );
}

// tool artifacts (BuildConfig "exclude"): the excluded units (a registry key + its subkeys + the parent's subkey line, a
// device block, a printer block, a list item, a numbered list line), an absence record not excluded, the anchors inside
// dropped, the run-metadata lines excluded with them, a value outside stopping the run; the BuildConfig round trip
static std::vector<TextFile> pg_tool_fixture( bool leak_outside )
{
	auto tf = []( const char* name, const std::string& s ) { return TextFile { name, Bytes( s.begin(), s.end() ) }; };
	const std::string swd = "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\PRINTENUM\n  depth 1; 0 value(s), 1 subkey(s)\n  subkey \"{d1d2d3d4-e5e6-4f7a-8b9c-0d1e2f3a4b5c}\"\n\n"
							"## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\PRINTENUM\\{d1d2d3d4-e5e6-4f7a-8b9c-0d1e2f3a4b5c}\n"
							"  value \"ContainerID\"  REG_SZ (1)  78 bytes: \"{e1e2e3e4-f5f6-4a7b-8c9d-0e1f2a3b4c5d}\"\n"
							"  value \"FriendlyName\"  REG_SZ (1)  34 bytes: \"FixTool Printer\"\n\n"
							"## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\PRINTENUM\\{d1d2d3d4-e5e6-4f7a-8b9c-0d1e2f3a4b5c}\\Properties\n  depth 3; 1 value(s)\n\n";
	auto files = pg_text_fixture2( swd );
	std::string ci = "[api] SetupDiGetDeviceInstanceIdW -> \"SWD\\PRINTENUM\\{D1D2D3D4-E5E6-4F7A-8B9C-0D1E2F3A4B5C}\"\n";
	files[ 0 ].data.insert( files[ 0 ].data.end(), ci.begin(), ci.end() );
	std::string pci = "=== device 7\n  instance ID: \"SWD\\x5CPRINTENUM\\x5C{D1D2D3D4-E5E6-4F7A-8B9C-0D1E2F3A4B5C}\"\n  SPDRP_BASE_CONTAINERID             REG_SZ \"{E1E2E3E4-F5F6-4A7B-8C9D-0E1F2A3B4C5D}\"\n"
					  "  SPDRP_FRIENDLYNAME                 REG_SZ \"FixTool Printer\"\n\n=== device 8\n  instance ID: \"PCI\\x5CVEN_1234\"\n";
	files[ 3 ].data.insert( files[ 3 ].data.end(), pci.begin(), pci.end() );
	files.push_back( tf( "printers.txt", "=== printer 0\nname \"FixTool Printer\"\nport \"FixTool Printer\"\ndriver \"FixTool v4 Printer Driver\"\n\n=== printer 1\nname \"Fax\"\n" ) );
	files.push_back( tf( "services_vm.txt", "# the services matching vmloader, ...\n## all service key names (3)\n  \"Beep\"\n  \"KFixDbg\"\n  \"Tcpip\"\n" ) );
	files.push_back( tf( "loaded_drivers.txt", "3 module(s)\n   0  0000000000000000  base \"ntoskrnl.exe\"\n   1  0000000000000000  base \"FixDbg.sys\" (len 10)\n" ) );
	files.push_back( tf( "driver_files.txt", "## \"C:\\x5Cvmloader.sys\"  (old install location)\nexists: no (GetLastError 2 = not present)\n" ) );
	if ( leak_outside ) pg_put( files, "os_ids.txt", std::string( k_pg_install_os_ids ) + "## note\nprinter instance {d1d2d3d4-e5e6-4f7a-8b9c-0d1e2f3a4b5c}\n" );
	return files;
}
static void pg_tool_exclusions( void )
{
	BuildContext ctx;
	const std::vector<ToolExclusion> items = { { "FixTool Printer", { "FixTool Printer", "FixTool v4 Printer Driver" }, "fixture printer" }, { "FixDbg", { "FixDbg" }, "fixture driver" },
		{ "VmLoader", { "vmloader" }, "fixture absence" } };
	auto files = pg_tool_fixture( false );
	ExclusionScan ex = ProfileGenerator::parse_tool_exclusions( files, items );
	std::set<std::string> got;
	for ( const ExclusionRange& r : ex.ranges ) got.insert( r.item + "|" + r.file + "|" + r.unit + "|" + std::to_string( r.first_line ) + "-" + std::to_string( r.last_line ) );
	const std::set<std::string> want = { "FixTool Printer|enum_registry.txt|registry key|8-10", "FixTool Printer|enum_registry.txt|registry subkey|12-13",
		"FixTool Printer|enum_registry.txt|parent subkey line|6-6", "FixTool Printer|pci_devices.txt|block|3-6", "FixTool Printer|printers.txt|block|1-4",
		"FixDbg|services_vm.txt|line|4-4", "FixDbg|loaded_drivers.txt|line|3-3" };
	TEST_CHECK_( got == want, "%zu excluded ranges", got.size() );
	for ( const std::string& g : got ) TEST_MSG( "range %s", g.c_str() );
	TEST_CHECK( ex.absence_records == 1 && ex.excluded( "printers.txt", 3 ) && !ex.excluded( "printers.txt", 6 ) );
	ctx = pg_fresh( Environment::bare_host );
	TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files, {}, {}, ex } );
	TEST_CHECK( !pg_find( t.identity, "swd_guid", "{D1D2D3D4-E5E6-4F7A-8B9C-0D1E2F3A4B5C}" ) && !pg_find( t.identity, "container_id", "{E1E2E3E4-F5F6-4A7B-8C9D-0E1F2A3B4C5D}" ) );
	TEST_CHECK( t.excluded_anchors >= 2 && t.extra_exclusions.size() == 1 && t.extra_exclusions[ 0 ].file == "capture_info.txt" );
	TEST_CHECK( std::find( t.needles.begin(), t.needles.end(), std::string( "d1d2d3d4-e5e6-4f7a-8b9c-0d1e2f3a4b5c" ) ) != t.needles.end() );	// leak guard keeps it
	for ( const FieldMapEntry& f : t.fields ) TEST_CHECK( !( f.area == "pci_devices.txt" && f.line >= 3 && f.line <= 6 ) );
	bool threw = false;
	auto leaky = pg_tool_fixture( true );
	ctx = pg_fresh( Environment::bare_host );
	try { ProfileGen->gen_text_identity( ctx, { leaky, {}, {}, ProfileGenerator::parse_tool_exclusions( leaky, items ) } ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "also occurs outside its excluded ranges" ) != std::string::npos; }
	TEST_CHECK( threw );
	// the BuildConfig carries the list (reviewable, reproducible); the defaults name the four decided items
	BuildConfig c = pg_cfg( Environment::vmware_guest );
	TEST_CHECK( c.exclusions.size() == 5 && c.exclusions[ 0 ].name == "RustDesk Printer" && c.exclusions[ 1 ].name == "RustDesk" && c.exclusions[ 4 ].patterns == std::vector<std::string>( { "vmloader" } ) );
	c.exclusions.push_back( items[ 0 ] );
	BuildConfig c2 = BuildConfig::load( c.save() );
	TEST_CHECK( c2 == c && c2.exclusions.size() == 6 && c2.exclusions[ 5 ].patterns.size() == 2 );
	TEST_CHECK( !c.firmware_ref_parent.empty() && c2.firmware_ref_manifest_sha256 == c.firmware_ref_manifest_sha256 );
	threw = false;
	try { BuildConfig::load( c.save() + "exclude = no pattern list\n" ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
	// gen_build: exclusions.tsv / volatile.tsv in both profiles, identical
	BuildConfig cb = pg_cfg( Environment::bare_host );
	cb.exclusions = items;
	CaptureInputs cap = pg_capture( false, files );
	ctx = pg_fresh( Environment::bare_host );
	ctx = ProfileGen->new_context( cb );
	GenResult r = ProfileGen->gen_build( ctx, cap );
	TEST_ASSERT_( r.ok, "build: %s", r.error.c_str() );
	TEST_CHECK( r.synthetic.at( "exclusions.tsv" ) == r.original.at( "exclusions.tsv" ) && r.synthetic.at( "volatile.tsv" ) == r.original.at( "volatile.tsv" ) );
	const Bytes& et = r.synthetic.at( "exclusions.tsv" );
	TEST_CHECK( std::string( et.begin(), et.end() ).find( "FixTool Printer\tprinters.txt\t1\t4\tblock" ) != std::string::npos );
}

// the CHID check: unchanged fields pass; a changed CHID input (type 1 manufacturer) is counted
static void pg_chid_check( void )
{
	BuildContext ctx;
	Bytes raw = pg_fixture( false );
	ctx = pg_fresh( Environment::bare_host );
	Bytes syn = ProfileGen->gen_smbios( ctx, { raw } ).synthetic;
	ProfileGenerator::ChidCheck c = ProfileGenerator::check_chid_fields( raw, syn );
	TEST_CHECK( c.checked >= 6 && c.changed == 0 );
	Bytes bad = syn;
	SmbiosTable t = parse_smbios( bad );
	bad[ t.structures[ 0 ].strings[ 0 ].offset ] ^= 0x01;	// type 1 Manufacturer
	TEST_CHECK( ProfileGenerator::check_chid_fields( raw, bad ).changed == 1 );
}

// a new identity: fresh RDRAND values give a different value for EVERY mapped field, while every consistency relation holds;
// the repository identity map (no install-history offset) applied again reproduces the identity byte for byte
static void pg_new_identity( void )
{
	BuildContext ctx;
	CaptureInputs in = pg_capture( false, pg_text_fixture3() );
	SmbiosTable t = parse_smbios( in.rsmb );
	const uint8_t wired[ 6 ] = { 0xA0, 0xB1, 0xC2, 0x01, 0x02, 0x03 };
	memcpy( &in.rsmb[ t.structures[ 0 ].offset + 8 + 10 ], wired, 6 );
	in.edid = { { "edid_0.bin", pg_edid( 0x2701, 0x12345678, "FIXSER12345", false ) }, { "edid_1.bin", pg_edid( 0x2701, 0x12345678, "FIXSER12345", false ) } };
	ctx = pg_fresh( Environment::bare_host );
	GenResult ra = ProfileGen->gen_build( ctx, in );	// generation: identity A
	ctx = pg_fresh( Environment::bare_host );
	TEST_CHECK( ProfileGen->gen_new_identity( ctx ).entries.empty() && !ctx.applying );
	GenResult rb = ProfileGen->gen_build( ctx, in );	// a new identity: B
	TEST_CHECK( ra.generated && rb.generated && ra.identity_map.has_install_shift && rb.identity_map.has_install_shift );
	TEST_ASSERT_( ra.ok && rb.ok, "builds: %s / %s", ra.error.c_str(), rb.error.c_str() );
	TEST_ASSERT( ra.identity.identity.size() == rb.identity.identity.size() );
	for ( size_t i = 0; i < ra.identity.identity.size(); i++ )
	{
		const IdentityEntry &x = ra.identity.identity[ i ], &y = rb.identity.identity[ i ];
		TEST_CHECK( x.id == y.id && x.original == y.original );
		TEST_CHECK_( x.synthetic != y.synthetic, "%s (%s) differs between the two identities", x.id.c_str(), x.kind.c_str() );
	}
	for ( size_t i = 0; i < ra.identity.smbios_fields + ra.identity.edid_fields; i++ )
	{
		const FieldMapEntry& f = ra.identity.fields[ i ];
		if ( f.rule == "6c" ) continue;
		const Bytes& a = ra.synthetic.at( f.area );
		const Bytes& b = rb.synthetic.at( f.area );
		TEST_CHECK_( !std::equal( a.begin() + f.offset, a.begin() + f.offset + f.length, b.begin() + f.offset ), "%s %s differs", f.area.c_str(), f.id.c_str() );
	}
	for ( const GenResult* r : { &ra, &rb } )
	{
		auto val = [ & ]( const char* real ) {
			const IdentityEntry* e = pg_find( r->identity.identity, "mac", real );
			std::string h;
			for ( char ch : e->synthetic ) if ( ch != ':' ) h += ch;
			return std::stoull( h, nullptr, 16 );
		};
		unsigned long long wifi = val( "3C:A9:F4:10:20:30" );
		TEST_CHECK( val( "3C:A9:F4:10:20:31" ) == wifi + 1 && val( "3C:A9:F4:10:20:34" ) == wifi + 4 && val( "3E:A9:F4:10:20:30" ) == ( wifi ^ ( 2ull << 40 ) ) );
		unsigned long long w = val( "A0:B1:C2:01:02:03" );
		const uint8_t* u = &r->synthetic.at( "smbios.bin" )[ t.structures[ 0 ].offset + 8 ];
		unsigned long long tail = 0;
		for ( int i = 10; i < 16; i++ ) tail = tail << 8 | u[ i ];
		TEST_CHECK( tail == w );
		const IdentityEntry* ms = pg_find( r->identity.identity, "machine_sid", "S-1-5-21-1111111111-222222222-3333333333" );
		const IdentityEntry* us = pg_find( r->identity.identity, "user_sid", "S-1-5-21-1111111111-222222222-3333333333-1001" );
		TEST_CHECK( us->synthetic == ms->synthetic + "-1001" );
		const IdentityEntry* d = pg_find( r->identity.identity, "install_date", "1688252866" );
		const IdentityEntry* it = pg_find( r->identity.identity, "install_time", "133327264661234567" );
		TEST_CHECK( std::stoull( it->synthetic ) / 10000000ull - 11644473600ull == std::stoull( d->synthetic ) );
		TEST_CHECK( r->synthetic.at( "edid\\edid_0.bin" ) == r->synthetic.at( "edid\\edid_1.bin" ) );	// the same monitor on two ports
	}
	// identity A again from its repository map (no offset inside): byte-identical, nothing drawn
	const Bytes& am = ra.synthetic.at( "identity_map.tsv" );
	const IdentityMap amap = ProfileGenerator::parse_identity_map( std::string( am.begin(), am.end() ) );
	TEST_CHECK( !amap.has_install_shift && std::string( am.begin(), am.end() ).find( "install_shift" ) == std::string::npos );
	const Bytes& om = ra.original.at( "identity_map.tsv" );
	TEST_CHECK( ProfileGenerator::parse_identity_map( std::string( om.begin(), om.end() ) ).install_shift == ra.identity_map.install_shift );
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->load_identity_maps( ctx, amap, {} );
	GenResult ra2 = ProfileGen->gen_build( ctx, in );
	TEST_CHECK( ra2.ok && !ra2.generated && ra2.synthetic == ra.synthetic && ra2.original == ra.original );
}

// rule 4c on software-device instance IDs: every GUID inside an Enum\SWD instance (braced or not) is synthetic, one mapping
// everywhere (the MMDEVAPI endpoint GUID = the audio.txt endpoint); the all-zero GUID and the PnP hashes stay exact
static void pg_swd_guids( void )
{
	BuildContext ctx;
	std::string swd = "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\MMDEVAPI\\{0.0.0.00000000}.{a1b2c3d4-e5f6-4789-8abc-def012345678}\n"
					  "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\DRIVERENUM\\{12ab34cd-56ef-4a1b-9c2d-3e4f5a6b7c8d}#Fixture_component_123&1&2c3d4e5f&0\n"
					  "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\PRINTENUM\\{ABCDEF12-3456-4789-ABCD-EF0123456789}\n"
					  "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\FIXENUM\\FIXTURE_9f8e7d6c-5b4a-4321-8fed-cba987654321&0&3e2f1a0b&0\n"
					  "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\NULLENUM\\{00000000-0000-0000-0000-000000000000}\n";
	auto files = pg_text_fixture2( swd );
	files.push_back( { "audio.txt", [] { std::string a = "endpoint [IDENTIFIER - keep local]: \"{0.0.0.00000000}.{a1b2c3d4-e5f6-4789-8abc-def012345678}\"\n"; return Bytes( a.begin(), a.end() ); }() } );
	ctx = pg_fresh( Environment::bare_host );
	TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files } );
	size_t swd_n = std::count_if( t.identity.begin(), t.identity.end(), []( const IdentityEntry& r ) { return r.kind == "swd_guid"; } );
	TEST_CHECK_( swd_n == 4, "4 SWD GUIDs (the all-zero one excluded), got %zu", swd_n );
	const IdentityEntry* ep = pg_find( t.identity, "swd_guid", "{A1B2C3D4-E5F6-4789-8ABC-DEF012345678}" );
	TEST_ASSERT( ep != nullptr );
	TEST_CHECK( !pg_find( t.identity, "audio_endpoint", "{A1B2C3D4-E5F6-4789-8ABC-DEF012345678}" ) );	// one identity
	bool in_audio = false, in_enum = false;
	for ( const FieldMapEntry& f : t.fields ) if ( f.id == ep->id ) { in_audio |= f.area == "audio.txt"; in_enum |= f.area == "enum_registry.txt"; }
	TEST_CHECK_( in_audio && in_enum, "one mapping everywhere the endpoint GUID appears" );
	for ( const IdentityEntry& r : t.identity )
	{
		if ( r.kind != "swd_guid" ) continue;
		TEST_CHECK( std::regex_match( r.synthetic, std::regex( R"(\{[0-9A-F]{8}-[0-9A-F]{4}-4[0-9A-F]{3}-[89AB][0-9A-F]{3}-[0-9A-F]{12}\})" ) ) && r.synthetic != r.original );
	}
	TEST_CHECK( pg_find( t.identity, "swd_guid", "{9F8E7D6C-5B4A-4321-8FED-CBA987654321}" ) != nullptr );	// unbraced inside an instance name
	TEST_CHECK( !pg_find( t.identity, "swd_guid", "{00000000-0000-0000-0000-000000000000}" ) );
	for ( const IdentityEntry& r : t.identity ) TEST_CHECK( _stricmp( r.original.c_str(), "2c3d4e5f" ) != 0 && _stricmp( r.original.c_str(), "3e2f1a0b" ) != 0 );	// PnP hashes exact
}

// rule 7b (user decision 2026-10-16: shift all together): ONE offset for InstallDate / InstallTime and every install-history
// timestamp (device first install / install / last arrival with their ISO forms, SQM first session, DiagTrack, Bluetooth);
// spacing and ordering exactly kept (the device installed 60 s before InstallDate still is), every event inside the build
// release .. capture window, the offset at least one day earlier; driver dates and registry last-write times untouched
static void pg_install_shift( void )
{
	BuildContext ctx;
	auto files = pg_text_fixture3();
	ctx = pg_fresh( Environment::bare_host );
	TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files, {}, {}, {}, pg_smbios_strings() } );
	const IdentityMap first_map = ctx.identity_map;
	TEST_CHECK( first_map.has_install_shift && first_map.entries.count( "identity/install" ) );
	struct ev { const char* kind; const char* real; bool iso; };
	const ev evs[] = { { "install_time", "133327264661234567", false }, { "device_install_time", "133327264064444444", false }, { "device_install_time", "133327264965555555", false },
		{ "device_arrival_time", "134116986006666666", false }, { "sqm_first_session", "133327300661357913", false }, { "diagtrack_time", "134116989002468024", false },
		{ "bt_event_time", "133353184661122334", false }, { "event_iso", "2023-07-01T23:07:46Z", true }, { "event_iso", "2023-07-01T23:06:46Z", true },
		{ "event_iso", "2023-07-01T23:08:16Z", true }, { "event_iso", "2025-12-31T23:50:00Z", true } };
	const IdentityEntry* id = pg_find( t.identity, "install_date", "1688252866" );
	TEST_ASSERT( id != nullptr );
	const long long shift = std::stoll( id->synthetic ) - 1688252866LL;
	TEST_CHECK_( shift <= -86400, "the offset is at least one day earlier" );
	auto iso_unix = []( const std::string& s ) {
		std::tm tm {};
		sscanf( s.c_str(), "%d-%d-%dT%d:%d:%dZ", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec );
		tm.tm_year -= 1900;
		tm.tm_mon -= 1;
		return (long long)_mkgmtime( &tm );
	};
	for ( const ev& e : evs )
	{
		const IdentityEntry* r = pg_find( t.identity, e.kind, e.real );
		TEST_ASSERT_( r != nullptr, "%s %s", e.kind, e.real );
		const long long d = e.iso ? iso_unix( r->synthetic ) - iso_unix( r->original ) : ( std::stoll( r->synthetic ) - std::stoll( r->original ) ) / 10000000LL;
		TEST_CHECK_( d == shift && ( e.iso || ( std::stoll( r->synthetic ) - std::stoll( r->original ) ) % 10000000LL == 0 ), "%s shifted by the one offset", e.kind );
	}
	// every pairwise difference is the original one (the constant offset), the setup-phase order kept
	const IdentityEntry* first = pg_find( t.identity, "device_install_time", "133327264064444444" );
	const IdentityEntry* it = pg_find( t.identity, "install_time", "133327264661234567" );
	TEST_CHECK( std::stoll( it->synthetic ) - std::stoll( first->synthetic ) == 133327264661234567LL - 133327264064444444LL );
	TEST_CHECK( t.time_shift.shifted == 12 && t.time_shift.exact == 12 && t.time_shift.outside_window == 0 );
	TEST_CHECK( t.time_shift.before_install_real == 2 && t.time_shift.before_install_synthetic == 2 );	// the device first install (FILETIME + ISO)
	// not shifted: the driver date, the registry last-write time in the same second as the device's first install
	TEST_CHECK( !pg_find( t.identity, "device_install_time", "126064512000000000" ) );
	for ( const FieldMapEntry& f : t.fields )
		if ( f.area == "enum_registry.txt" ) TEST_CHECK_( f.rule != "7b", "a registry last-write time is no install event (%s %llu)", f.id.c_str(), (unsigned long long)f.line );
	// the offset: drawn from RDRAND at generation (a new identity, another offset); the same identity map applied again,
	// the same offset - derived from the map's synthetic InstallDate, the repository copy holds no offset
	ctx = pg_fresh( Environment::bare_host );
	TextIdentityGen t2 = ProfileGen->gen_text_identity( ctx, { files, {}, {}, {}, pg_smbios_strings() } );
	IdentityMap repo_copy = first_map;
	repo_copy.has_install_shift = false;
	repo_copy.install_shift = 0;
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->load_identity_maps( ctx, repo_copy, {} );
	TextIdentityGen t3 = ProfileGen->gen_text_identity( ctx, { files, {}, {}, {}, pg_smbios_strings() } );
	TEST_CHECK( ctx.identity_map.install_shift == first_map.install_shift );
	TEST_CHECK( pg_find( t2.identity, "install_date", "1688252866" )->synthetic != id->synthetic && pg_find( t3.identity, "install_date", "1688252866" )->synthetic == id->synthetic );
	// gen_install_time on its own: no room (the history spans the whole window) stops the run
	InstallTime tight;
	tight.os_build = 19045;
	tight.capture_utc = "2022-10-25T00:00:00Z";
	tight.unix_seconds = 1666137600;	// 2022-10-19
	tight.filetime = ( 1666137600ull + 11644473600ull ) * 10000000ull;
	tight.latest_event = 1666569600;	// 2022-10-24
	bool threw = false;
	try { ProfileGen->gen_install_time( ctx, tight ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
}

// ── threading (plan 1.J.49.a, user decision 2026-10-17 "Per-build context") ──────────────────────────────────────────────
// acutest's checks are not thread-safe: the worker threads only count / record, the checks run on the main thread.

// the Randomizer under load: 8 threads x k_pg_rounds rounds of EVERY Randomize->gen_ (formats validated, installation-ID
// check digits valid, no exception), and the thread-local result buffers: a value one thread holds is never overwritten by
// the other threads drawing the same gen_ meanwhile (a barrier makes all threads draw while every thread holds its value)
static void pg_randomizer_threads( void )
{
	TEST_ASSERT_( Randomize->cpu_supports_rand, "RDRAND (CPUID.1:ECX[30]) on the test machine" );
	constexpr int k_threads = 8, k_rounds = 1500, k_checks = 24;	// k_checks: the results validated per round
	std::string iid_real;	// 63 digits: 9 groups of 7 (the pattern; its own check digits do not matter)
	for ( int g = 0; g < 9; g++ ) iid_real += "1234560";
	struct ThreadResult
	{
		size_t calls = 0, bad = 0, exceptions = 0, overwritten = 0;
		std::string first_bad;		// which gen_ failed first (no value)
	};
	std::vector<ThreadResult> res( k_threads );
	std::barrier sync( k_threads );
	auto worker = [ & ]( int t ) {
		ThreadResult& r = res[ t ];
		const std::regex r_like( "[A-Z]{2}[0-9]{2}[a-z]{2}-[0-9]{3}_[A-Z]" ), r_fill( "X-[01]{2}" ), r_hex8( "[0-9a-f]{8}" ), r_hexcls( "[0-9][A-F][0-9][A-F]" ),
			r_cn( "DESKTOP-[A-Z0-9]{7}" ), r_mac( "00:11:22:[0-9A-F]{2}:[0-9A-F]{2}:[0-9A-F]{2}" ),
			r_guid4( R"(\{[0-9A-F]{8}-[0-9A-F]{4}-4[0-9A-F]{3}-[89AB][0-9A-F]{3}-[0-9A-F]{12}\})" ),
			r_guid1( R"([0-9a-f]{8}-[0-9a-f]{4}-1[0-9a-f]{3}-[89ab][0-9a-f]{3}-a0b1c2010203)" ),
			r_mbr( R"(\{[0-9a-f]{8}-0000-0000-0000-100000000000\})" ), r_uuid( "[0-9a-f]{32}" ), r_dec6( "[1-9][0-9]{5}" ),
			r_sid( "S-1-5-21-[1-9][0-9]{9}-[1-9][0-9]{8}-[1-9][0-9]{9}" ), r_usbhex( "[0-9a-f]{20}" ), r_dec( "[0-9]{1,10}" ), r_usbtxt( "[A-Z]{3}[0-9]{5}" ),
			r_clock( "[89ab][0-9a-f]{3}" );
		auto ok = [ & ]( bool good, const char* what ) {
			r.calls++;
			if ( !good && !r.bad++ ) r.first_bad = what;
		};
		try
		{
			for ( int i = 0; i < k_rounds; i++ )
			{
				const uint64_t u = Randomize->gen_u64();
				ok( u != 0 && u != ~0ull, "gen_u64" );
				ok( Randomize->gen_below( 7 ) < 7 && Randomize->gen_below( 1 ) == 0, "gen_below" );
				const int64_t v = Randomize->gen_in_range( -5, 5 );
				ok( v >= -5 && v <= 5, "gen_in_range" );
				ok( strchr( "xyz", Randomize->gen_char( "xyz" ) ) != nullptr, "gen_char" );
				ok( std::regex_match( Randomize->gen_fill( "X-??", "01" ), r_fill ), "gen_fill" );
				const int64_t d = std::stoll( Randomize->gen_decimal_in_range( -1000, 1000 ) );
				ok( d >= -1000 && d <= 1000, "gen_decimal_in_range" );
				ok( std::regex_match( Randomize->gen_like( "AB12cd-345_Z" ), r_like ), "gen_like" );
				const std::string h8 = Randomize->gen_hex_like( "1a2b3c4d", false );
				ok( std::regex_match( h8, r_hex8 ) && h8 != "1a2b3c4d", "gen_hex_like" );
				ok( std::regex_match( Randomize->gen_hex_like( "1A2B", true ), r_hexcls ), "gen_hex_like classes" );
				const std::string us = Randomize->gen_usb_serial_like( "4142433132333435" );
				std::string txt;
				for ( size_t k = 0; k + 1 < us.size(); k += 2 ) txt += char( std::stoul( us.substr( k, 2 ), nullptr, 16 ) );
				ok( us.size() == 16 && std::regex_match( txt, r_usbtxt ) && txt != "ABC12345", "gen_usb_serial_like ascii" );
				ok( std::regex_match( Randomize->gen_usb_serial_like( "0a1b2c3d4e5f60718293" ), r_usbhex ), "gen_usb_serial_like hex" );
				const std::string cn = Randomize->gen_computer_name( "DESKTOP-ABCDEFG" );
				ok( std::regex_match( cn, r_cn ) && cn != "DESKTOP-ABCDEFG", "gen_computer_name" );
				const std::string mac = Randomize->gen_mac_like( "00:11:22:33:44:55", 16 );
				ok( std::regex_match( mac, r_mac ) && std::stoul( mac.substr( 9, 2 ) + mac.substr( 12, 2 ) + mac.substr( 15, 2 ), nullptr, 16 ) <= 0xFFFFFFul - 16, "gen_mac_like" );
				ok( std::regex_match( Randomize->gen_guid_like( "{0F0F0F0F-1111-4222-8333-444455556666}", nullptr ), r_guid4 ), "gen_guid_like v4" );
				ok( std::regex_match( Randomize->gen_guid_like( "12345678-9abc-11f0-8123-3ca9f4102030", "a0b1c2010203" ), r_guid1 ), "gen_guid_like v1 node" );
				ok( std::regex_match( Randomize->gen_guid_like( "{3fa81c55-0000-0000-0000-100000000000}", nullptr ), r_mbr ), "gen_guid_like mbr" );
				const std::string cs = Randomize->gen_clock_seq_like( "a3f1" );
				ok( std::regex_match( cs, r_clock ) && cs != "a3f1", "gen_clock_seq_like" );
				const std::string uv = Randomize->gen_system_uuid_like( "564d0123456789abcdef1032547698ba", true );
				ok( std::regex_match( uv, r_uuid ) && uv.substr( 0, 4 ) == "564d", "gen_system_uuid_like vmware" );
				const std::string ur = Randomize->gen_system_uuid_like( "1032547698ba1c4e8f00112233445566", false );
				ok( std::regex_match( ur, r_uuid ) && ur[ 12 ] == '1' && ur[ 14 ] == '4' && ( ur[ 16 ] >= '8' && ur[ 16 ] <= 'b' ), "gen_system_uuid_like rfc4122" );
				const std::string d6 = Randomize->gen_decimal_like( "123456", 999999 );
				ok( std::regex_match( d6, r_dec6 ) && d6 != "123456", "gen_decimal_like" );
				const std::string es = Randomize->gen_edid_serial_like( "305419896" );
				ok( std::regex_match( es, r_dec ) && std::stoull( es ) <= 0xFFFFFFFFull && es != "305419896", "gen_edid_serial_like" );
				ok( std::regex_match( Randomize->gen_sid_like( "S-1-5-21-1111111111-222222222-3333333333" ), r_sid ), "gen_sid_like" );
				const std::string iid = Randomize->gen_installation_id_like( iid_real.c_str() );
				ok( iid.size() == 63 && ProfileGenerator::check_installation_id( iid.c_str() ), "gen_installation_id_like" );
				ok( Randomizer::check_digit( "123456" ) == char( '0' + ( 1 + 4 + 3 + 8 + 5 + 12 ) % 7 ), "check_digit" );
			}
			// the thread-local buffers: hold one result of each of three gen_, let every thread draw the same gen_ many times,
			// then compare the held pointers with their copies
			const char* held_like = Randomize->gen_like( "AB12cd-345_Z" );
			const char* held_guid = Randomize->gen_guid_like( "{0F0F0F0F-1111-4222-8333-444455556666}", nullptr );
			const char* held_iid = Randomize->gen_installation_id_like( iid_real.c_str() );
			const std::string copy_like = held_like, copy_guid = held_guid, copy_iid = held_iid;
			sync.arrive_and_wait();	// every thread holds its values
			for ( int i = 0; i < 200; i++ )	// the OTHER threads draw the same gen_ now; this thread draws other gen_ only
			{
				( void )Randomize->gen_hex_like( "1a2b3c4d", false );
				( void )Randomize->gen_mac_like( "00:11:22:33:44:55", 0 );
			}
			sync.arrive_and_wait();	// every thread has drawn
			r.overwritten += copy_like != held_like;
			r.overwritten += copy_guid != held_guid;
			r.overwritten += copy_iid != held_iid;
		}
		catch ( const std::exception& )
		{
			r.exceptions++;
			sync.arrive_and_drop();	// the other threads must not wait for this one at the barriers still ahead
		}
	};
	std::vector<std::thread> threads;
	for ( int t = 0; t < k_threads; t++ ) threads.emplace_back( worker, t );
	for ( std::thread& th : threads ) th.join();
	size_t calls = 0;
	for ( int t = 0; t < k_threads; t++ )
	{
		calls += res[ t ].calls;
		TEST_CHECK_( res[ t ].bad == 0, "thread %d: %zu of %zu results with a wrong format (first: %s)", t, res[ t ].bad, res[ t ].calls, res[ t ].first_bad.c_str() );
		TEST_CHECK_( res[ t ].exceptions == 0, "thread %d: %zu exception(s)", t, res[ t ].exceptions );
		TEST_CHECK_( res[ t ].overwritten == 0, "thread %d: %zu held result(s) overwritten by another thread", t, res[ t ].overwritten );
	}
	TEST_MSG( "%d threads, %zu checked results", k_threads, calls );
	TEST_CHECK( calls == size_t( k_threads ) * size_t( k_rounds ) * k_checks );
}

// the two fixture environments of the parallel builds: the host with the rich fwid 1.4 fixture, the guest with fixture 2;
// both share one USB device (the shared table of both enum_registry.txt, host first - as load_capture builds it)
struct PgEnvPair
{
	CaptureInputs guest, host;
};
static PgEnvPair pg_env_pair()
{
	PgEnvPair p;
	p.host = pg_capture( false, pg_text_fixture3() );
	p.guest = pg_capture( true, pg_text_fixture2( pg_usb( "VID_BBBB&PID_CCCC", "7Y7Y" ) + pg_usb( "VID_1111&PID_2222", "4142433132333435" ) ) );
	const std::vector<Bytes> usb = { p.host.texts[ 2 ].data, p.guest.texts[ 2 ].data };	// enum_registry.txt of the host, then of the guest
	p.host.usb_sources = usb;
	p.guest.usb_sources = usb;
	return p;
}
// one application build from saved maps in its own context (callable from any thread)
static GenResult pg_apply( Environment e, const CaptureInputs& in, const IdentityMap& own, const IdentityMap& peer )
{
	BuildContext ctx = ProfileGen->new_context( pg_cfg( e ) );
	ProfileGen->load_identity_maps( ctx, own, peer );
	return ProfileGen->gen_build( ctx, in );
}

// guest and host contexts built in parallel threads from the saved maps: every file of both profiles byte-identical to the
// serial builds (several rounds), nothing drawn
static void pg_parallel_builds( void )
{
	const PgEnvPair env = pg_env_pair();
	BuildContext ctx = pg_fresh( Environment::bare_host );
	GenResult gh = ProfileGen->gen_build( ctx, env.host );	// generation: the host first, then the guest with the host as peer
	TEST_ASSERT_( gh.ok, "host generation: %s", gh.error.c_str() );
	BuildContext gctx = ProfileGen->new_context( pg_cfg( Environment::vmware_guest ) );
	ProfileGen->load_identity_maps( gctx, {}, gh.identity_map );
	GenResult gg = ProfileGen->gen_build( gctx, env.guest );
	TEST_ASSERT_( gg.ok, "guest generation: %s", gg.error.c_str() );
	auto repo_map = []( const GenResult& r ) { const Bytes& b = r.synthetic.at( "identity_map.tsv" ); return ProfileGenerator::parse_identity_map( std::string( b.begin(), b.end() ) ); };
	const IdentityMap mh = repo_map( gh ), mg = repo_map( gg );
	const GenResult sh = pg_apply( Environment::bare_host, env.host, mh, mg ), sg = pg_apply( Environment::vmware_guest, env.guest, mg, mh );	// serial
	TEST_ASSERT_( sh.ok && sg.ok, "serial application: %s / %s", sh.error.c_str(), sg.error.c_str() );
	TEST_CHECK( !sh.generated && !sg.generated && sh.synthetic == gh.synthetic && sg.synthetic == gg.synthetic );
	constexpr int k_rounds = 16;
	for ( int round = 0; round < k_rounds; round++ )
	{
		GenResult ph, pg;
		std::thread th( [ & ] { ph = pg_apply( Environment::bare_host, env.host, mh, mg ); } );
		std::thread tg( [ & ] { pg = pg_apply( Environment::vmware_guest, env.guest, mg, mh ); } );
		th.join();
		tg.join();
		TEST_CHECK_( ph.ok && pg.ok, "round %d: parallel application ok (%s / %s)", round, ph.error.c_str(), pg.error.c_str() );
		TEST_CHECK_( !ph.generated && !pg.generated, "round %d: nothing drawn", round );
		TEST_CHECK_( ph.synthetic == sh.synthetic && ph.original == sh.original, "round %d: host profile (%zu files) byte-identical to the serial build", round, sh.synthetic.size() );
		TEST_CHECK_( pg.synthetic == sg.synthetic && pg.original == sg.original, "round %d: guest profile (%zu files) byte-identical to the serial build", round, sg.synthetic.size() );
		TEST_CHECK( ph.identity_map == sh.identity_map && pg.identity_map == sg.identity_map );
	}
}

// both environments generated CONCURRENTLY into memory (no peer map, no file written): every shared/ key has the same value in
// both maps (the shared/ registry: the first build that needs a key records it, the other takes it); each map applied again
// reproduces its profile; every const char* a gen_ step returned stays valid after its context is destroyed (copies compared)
static void pg_concurrent_fresh_shared( void )
{
	const PgEnvPair env = pg_env_pair();
	constexpr int k_rounds = 16;
	struct HeldValue
	{
		const char* text = nullptr;	// as returned by the gen_ step (the interned pool)
		std::string copy;			// taken while the context existed
	};
	std::vector<HeldValue> held;
	std::mutex held_mutex;
	std::set<std::string> first_round_values;
	for ( int round = 0; round < k_rounds; round++ )
	{
		ProfileGen->reset();	// a new identity session: the shared/ registry empty
		GenResult rh, rg;
		auto run = [ & ]( Environment e, const CaptureInputs& in, GenResult& out ) {
			std::vector<HeldValue> mine;
			{
				BuildContext ctx = ProfileGen->new_context( pg_cfg( e ) );
				ProfileGen->gen_new_identity( ctx );
				out = ProfileGen->gen_build( ctx, in );
				// values straight from gen_ steps of this context (identity map hits and fresh draws)
				const char* usb = ProfileGen->gen_usb_serial( ctx, { "4142433132333435", 0, "x" } ).text;
				const char* mac = ProfileGen->gen_mac( ctx, { "00:04:0F:12:34:56", "Fixture", "Mheld" } ).text;
				const char* guid = ProfileGen->gen_guid( ctx, { "{0F0F0F0F-1111-4222-8333-444455556666}", "identity/Gheld" } ).text;
				const char* cls = ProfileGen->gen_class_string( ctx, { "AB12cd-345_Z", "identity/Cheld" } ).text;
				for ( const char* v : { usb, mac, guid, cls } ) mine.push_back( { v, v } );
				ProfileGen->load_identity_maps( ctx, {}, {} );	// the context's map replaced ...
			}	// ... and the context destroyed
			std::lock_guard lock( held_mutex );
			held.insert( held.end(), mine.begin(), mine.end() );
		};
		std::thread th( run, Environment::bare_host, std::cref( env.host ), std::ref( rh ) );
		std::thread tg( run, Environment::vmware_guest, std::cref( env.guest ), std::ref( rg ) );
		th.join();
		tg.join();
		TEST_ASSERT_( rh.ok && rg.ok, "round %d: concurrent generation: %s / %s", round, rh.error.c_str(), rg.error.c_str() );
		TEST_CHECK( rh.generated && rg.generated );
		size_t common = 0, same = 0;
		for ( auto& [ k, v ] : rh.identity_map.entries )
		{
			if ( k.rfind( "shared/", 0 ) != 0 ) continue;
			auto it = rg.identity_map.entries.find( k );
			if ( it == rg.identity_map.entries.end() ) continue;
			common++;
			same += it->second == v;
		}
		TEST_CHECK_( common >= 1 && same == common, "round %d: %zu shared/ key(s) in both maps, %zu with the same value", round, common, same );
		TEST_CHECK_( ProfileGen->check_shared_values() >= 3, "round %d: %zu value(s) in the shared/ registry", round, ProfileGen->check_shared_values() );
		const IdentityEntry* a = pg_find( rh.identity.identity, "usb_serial", "4142433132333435" );
		const IdentityEntry* b = pg_find( rg.identity.identity, "usb_serial", "4142433132333435" );
		TEST_ASSERT( a != nullptr && b != nullptr );
		TEST_CHECK_( a->synthetic == b->synthetic, "round %d: the shared USB device has one synthetic serial in both profiles", round );
		if ( round == 0 ) first_round_values.insert( a->synthetic );
		else TEST_CHECK_( !first_round_values.count( a->synthetic ), "round %d: a new identity session draws a new shared serial", round );
		// each concurrently generated map applied again (serially, the other map as peer) reproduces its profile byte for byte
		auto repo_map = []( const GenResult& r ) { const Bytes& m = r.synthetic.at( "identity_map.tsv" ); return ProfileGenerator::parse_identity_map( std::string( m.begin(), m.end() ) ); };
		const IdentityMap mh = repo_map( rh ), mg = repo_map( rg );
		const GenResult ah = pg_apply( Environment::bare_host, env.host, mh, mg ), ag = pg_apply( Environment::vmware_guest, env.guest, mg, mh );
		TEST_CHECK_( ah.ok && ag.ok && ah.synthetic == rh.synthetic && ag.synthetic == rg.synthetic, "round %d: the generated maps reproduce both profiles", round );
	}
	ProfileGen->reset();
	size_t valid = 0;
	for ( const HeldValue& h : held ) valid += h.text && h.copy == h.text;
	TEST_CHECK_( valid == held.size() && held.size() == size_t( k_rounds ) * 2 * 4, "%zu of %zu const char* results still valid after their contexts were destroyed", valid, held.size() );
}

// ── version-1 GUID timestamps (gap P1 #1, U1516) ──────────────────────────────────────────────────────────────────────
// the GUID text with its first 8 hex digits (time_low) replaced by x (case of the text kept): another timestamp, the same
// clock sequence and node
static std::string pg_v1_retime( const std::string& g, uint32_t x )
{
	const bool up = std::none_of( g.begin(), g.end(), []( char c ) { return c >= 'a' && c <= 'f'; } );
	char h[ 9 ];
	snprintf( h, sizeof h, up ? "%08X" : "%08x", x );
	std::string o = g;
	size_t k = 0;
	for ( char& c : o )
		if ( isxdigit( uint8_t( c ) ) && k < 8 ) c = h[ k++ ];
	return o;
}
// rule 4c version-1 GUIDs (decision E "valid v1" + rule 7b): every RFC 4122 version-1 GUID's 60-bit timestamp = real + the
// install-history offset (the offset of InstallDate, 100-ns units), the clock sequence synthetic with its variant bits, the
// node rule kept (adapter MAC -> synthetic MAC, the no-NIC node exact); application re-checks every saved value; a map
// written before the rule stops the build unless upgrade_v1_time re-derives exactly those timestamps (clock + node kept);
// without an install history a version-1 GUID stops the build; parse_guid_v1 / Randomize->gen_clock_seq_like on their own
static void pg_v1_guid_time( void )
{
	auto files = pg_text_fixture3();
	BuildContext ctx = pg_fresh( Environment::bare_host );
	const TextIdentityGen t = ProfileGen->gen_text_identity( ctx, { files, {}, {}, {}, pg_smbios_strings() } );
	const IdentityMap gen_map = ctx.identity_map;
	const IdentityEntry* d = pg_find( t.identity, "install_date", "1688252866" );
	const IdentityEntry* wifi = pg_find( t.identity, "mac", "3C:A9:F4:10:20:30" );
	TEST_ASSERT( d != nullptr && wifi != nullptr );
	const long long shift = std::stoll( d->synthetic ) - 1688252866LL;
	std::string wifi_node;
	for ( char ch : wifi->synthetic ) if ( ch != ':' ) wifi_node += char( tolower( uint8_t( ch ) ) );
	size_t v1 = 0, mac_nodes = 0, nonic = 0;
	for ( const IdentityEntry& r : t.identity )
	{
		const GuidV1 a = ProfileGenerator::parse_guid_v1( r.original.c_str() ), b = ProfileGenerator::parse_guid_v1( r.synthetic.c_str() );
		if ( !a.ok ) continue;
		v1++;
		TEST_CHECK_( b.ok && (long long)( b.timestamp - a.timestamp ) == shift * 10000000LL, "%s (%s): timestamp = real + the install-history offset", r.id.c_str(), r.kind.c_str() );
		TEST_CHECK_( b.clock_seq.size() == 4 && ( std::stoul( b.clock_seq.substr( 0, 1 ), nullptr, 16 ) & 0xC ) == 0x8, "%s: the RFC 4122 variant kept", r.id.c_str() );
		std::string an = a.node, bn = b.node;
		for ( char& ch : an ) ch = char( tolower( uint8_t( ch ) ) );
		for ( char& ch : bn ) ch = char( tolower( uint8_t( ch ) ) );
		if ( an == "806e6f6e6963" ) { nonic++; TEST_CHECK( bn == an ); }
		if ( an == "3ca9f4102030" ) { mac_nodes++; TEST_CHECK_( bn == wifi_node, "%s: node = the synthetic Wi-Fi MAC", r.id.c_str() ); }
	}
	TEST_CHECK_( v1 == 4 && mac_nodes == 2 && nonic == 2, "4 version-1 GUIDs (container, WPD, volume, HwProfileGuid): got %zu (%zu MAC nodes, %zu no-NIC nodes)", v1, mac_nodes, nonic );
	TEST_CHECK( t.v1_guids.guids == v1 && t.v1_guids.exact == v1 && t.v1_guids.after_capture == 0 );
	const IdentityEntry* hp = pg_find( t.identity, "hw_profile_guid", "{0a1b2c3d-1111-11ef-8222-806e6f6e6963}" );
	TEST_ASSERT( hp != nullptr );
	TEST_CHECK( std::regex_match( hp->synthetic, std::regex( R"(\{[0-9a-f]{8}-[0-9a-f]{4}-1[0-9a-f]{3}-[89ab][0-9a-f]{3}-806e6f6e6963\})" ) ) );	// its own style
	// application of the saved map: the same values, nothing drawn
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->load_identity_maps( ctx, gen_map, {} );
	const TextIdentityGen t2 = ProfileGen->gen_text_identity( ctx, { files, {}, {}, {}, pg_smbios_strings() } );
	bool same = t2.identity.size() == t.identity.size();
	for ( size_t i = 0; same && i < t.identity.size(); i++ ) same = t2.identity[ i ].synthetic == t.identity[ i ].synthetic;
	TEST_CHECK( same && ctx.upgraded_keys.empty() );
	// a map written before the rule: every version-1 value with another timestamp (clock sequence + node kept)
	IdentityMap old_map = gen_map;
	std::vector<std::string> retimed;
	for ( auto& [ k, e ] : old_map.entries )
		if ( e.kind == "guid" && ProfileGenerator::parse_guid_v1( e.value.c_str() ).ok )
		{
			e.value = pg_v1_retime( e.value, uint32_t( 0x13572468u + retimed.size() ) );
			retimed.push_back( k );
		}
	TEST_ASSERT( retimed.size() == v1 );
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->load_identity_maps( ctx, old_map, {} );
	bool threw = false;
	try { ProfileGen->gen_text_identity( ctx, { files, {}, {}, {}, pg_smbios_strings() } ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "--upgrade-v1-time" ) != std::string::npos; }
	TEST_CHECK_( threw, "a pre-U1516 version-1 timestamp stops the application" );
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->load_identity_maps( ctx, old_map, {} );
	ctx.upgrade_v1_time = true;
	const TextIdentityGen t3 = ProfileGen->gen_text_identity( ctx, { files, {}, {}, {}, pg_smbios_strings() } );
	std::vector<std::string> up = ctx.upgraded_keys;
	std::sort( up.begin(), up.end() );
	TEST_CHECK_( up == retimed && ctx.identity_map.entries == gen_map.entries, "the upgrade re-derives exactly the %zu version-1 values (%zu upgraded), every other value unchanged", retimed.size(), up.size() );
	// another node is no timestamp upgrade: it stops the build even with upgrade_v1_time
	IdentityMap bad_node = gen_map;
	for ( auto& [ k, e ] : bad_node.entries )
		if ( const GuidV1 g = ProfileGenerator::parse_guid_v1( e.value.c_str() ); e.kind == "guid" && g.ok && _stricmp( g.node.c_str(), "806e6f6e6963" ) != 0 )	// a MAC node
		{
			std::string& v = e.value;
			const size_t last = v.find_last_of( "0123456789abcdefABCDEF" );
			v[ last ] = v[ last ] == '0' ? '1' : '0';
			break;
		}
	ctx = pg_fresh( Environment::bare_host );
	ProfileGen->load_identity_maps( ctx, bad_node, {} );
	ctx.upgrade_v1_time = true;
	threw = false;
	try { ProfileGen->gen_text_identity( ctx, { files, {}, {}, {}, pg_smbios_strings() } ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "version-1 GUID" ) != std::string::npos; }
	TEST_CHECK_( threw, "a version-1 value with another node stops the build (not upgradeable)" );
	// no install history: a version-1 GUID cannot be built
	auto no_history = pg_text_fixture2( "" );
	pg_put( no_history, "os_ids.txt", "## no install values\n" );
	ctx = pg_fresh( Environment::bare_host );
	threw = false;
	try { ProfileGen->gen_text_identity( ctx, { no_history } ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "needs the install-history offset" ) != std::string::npos; }
	TEST_CHECK( threw );
	// parse_guid_v1: the fields of a version-1 GUID; nothing for version 4, the MBR form, another variant
	const GuidV1 p = ProfileGenerator::parse_guid_v1( "{12345678-9abc-11f0-8123-3ca9f4102030}" );
	TEST_CHECK( p.ok && p.timestamp == 0x1F09ABC12345678ull && p.clock_seq == "8123" && p.node == "3ca9f4102030" );
	for ( const char* g : { "{0F0F0F0F-1111-4222-8333-444455556666}", "{3fa81c55-0000-0000-0000-100000000000}", "12345678-9abc-11f0-c123-3ca9f4102030", "not a guid" } )
		TEST_CHECK_( !ProfileGenerator::parse_guid_v1( g ).ok, "%s is no RFC 4122 version-1 GUID", g );
	// the Randomizer's clock sequence: variant bits kept per class, case kept, never the real one
	for ( int i = 0; i < 200; i++ )
	{
		const std::string a = Randomize->gen_clock_seq_like( "8123" ), b = Randomize->gen_clock_seq_like( "C0DE" ), c = Randomize->gen_clock_seq_like( "7fff" );
		TEST_CHECK( std::regex_match( a, std::regex( "[89AB][0-9A-F]{3}" ) ) && a != "8123" );	// no letter in the real one: upper case
		TEST_CHECK( std::regex_match( b, std::regex( "[CD][0-9A-F]{3}" ) ) && b != "C0DE" );
		TEST_CHECK( std::regex_match( c, std::regex( "[0-7][0-9a-f]{3}" ) ) && c != "7fff" );
	}
}

// ── the synthetic-only view and single-value edits (gap P1 #8 / #9, U1517) ──────────────────────────────────────────────
// every text of a view (key, kind, rule, location, identities, relations, reason, value)
static std::vector<std::string> pg_view_texts( const ProfileView& v )
{
	std::vector<std::string> t;
	for ( const FieldInfo& f : v.fields )
	{
		for ( const std::string* s : { &f.key, &f.kind, &f.rule, &f.location, &f.not_editable } ) t.push_back( *s );
		t.insert( t.end(), f.identities.begin(), f.identities.end() );
		t.insert( t.end(), f.relations.begin(), f.relations.end() );
		if ( f.value ) t.push_back( f.value );
	}
	return t;
}
static const FieldInfo* pg_field( const ProfileView& v, const std::string& key )
{
	for ( const FieldInfo& f : v.fields ) if ( f.key == key ) return &f;
	return nullptr;
}
// the view of a build holds none of the captured values: every real value the in-memory build knows (each identity's
// original, the SMBIOS / EDID / disk real values of the leak guard) and the fixture's raw values are absent from every text
// of the view (case-insensitive substring); the view lists every map key plus the derived identities with metadata; then
// edits round trip: gen_value (fresh RDRAND, the same format) and set_value (format / real value / uniqueness checked),
// dependents follow (a base MAC's version-1 GUID nodes in the map; the MAC family, NameSamCompatible at the next build),
// the edited map applied by gen_build gives the edited profile, and the saved map reproduces it in a new context
static void pg_field_view( void )
{
	CaptureInputs in = pg_capture( false, pg_text_fixture3() );
	SmbiosTable t = parse_smbios( in.rsmb );
	const uint8_t wired[ 6 ] = { 0xA0, 0xB1, 0xC2, 0x01, 0x02, 0x03 };
	memcpy( &in.rsmb[ t.structures[ 0 ].offset + 8 + 10 ], wired, 6 );
	in.edid = { { "edid_0.bin", pg_edid( 0x2701, 0x12345678, "FIXSER12345", false ) } };
	BuildContext ctx = pg_fresh( Environment::bare_host );
	TEST_CHECK( !ProfileGen->list_fields( ctx ).built );
	const GenResult r = ProfileGen->gen_build( ctx, in );
	TEST_ASSERT_( r.ok, "build: %s", r.error.c_str() );
	const ProfileView v = ProfileGen->list_fields( ctx );
	TEST_CHECK( v.built && v.profile == "bare_host" && v.env == Environment::bare_host );
	// no real value anywhere in the view (and the leak guard of gen_build agrees)
	std::vector<std::string> real;
	for ( const IdentityEntry& e : r.identity.identity ) real.push_back( e.original );
	real.insert( real.end(), r.identity.needles.begin(), r.identity.needles.end() );
	for ( const char* x : { "DESKTOP-FIXTUR2", "Fixer", "AB12cd-345_Z", "1234ABCD", "AT-0042", "S5GXNF0R123456K", "FIXSER12345", "4142433132333435", "987654321098", "GPU12345XY",
			  "a0b1c2010203", "3ca9f4102030", "1688252866", "1111111111-222222222-3333333333", k_pg_iid0, k_pg_iid1, k_pg_flash_usb } )
		real.push_back( x );
	size_t checked = 0, hits = 0;
	const std::vector<std::string> texts = pg_view_texts( v );
	for ( const std::string& x0 : real )
	{
		if ( x0.size() < 4 ) continue;
		std::string x = x0;
		for ( char& c : x ) c = char( tolower( uint8_t( c ) ) );
		if ( x.front() == '{' && x.back() == '}' ) x = x.substr( 1, x.size() - 2 );
		checked++;
		for ( const std::string& s0 : texts )
		{
			std::string s = s0;
			for ( char& c : s ) c = char( tolower( uint8_t( c ) ) );
			if ( s.find( x ) != std::string::npos ) { hits++; TEST_MSG( "a real value of length %zu in a view text", x.size() ); }
		}
	}
	TEST_CHECK_( checked > 100 && hits == 0, "%zu real values x %zu view texts: %zu hits", checked, texts.size(), hits );
	TEST_CHECK( ProfileGenerator::check_view( v, r.identity.needles ) == 0 && ProfileGenerator::check_view( v, { "1688252866" } ) == 0 );
	ProfileView poisoned = v;	// the check itself finds a real value
	poisoned.fields[ 0 ].relations.push_back( "x DESKTOP-FIXTUR2 y" );
	TEST_CHECK( ProfileGenerator::check_view( poisoned, { "desktop-fixtur2" } ) == 1 );
	// the fields: every map key once, the derived identities with their reason, metadata filled
	size_t keys = 0, derived = 0;
	for ( const FieldInfo& f : v.fields )
	{
		if ( f.key.rfind( "derived/", 0 ) == 0 ) { derived++; TEST_CHECK( !f.editable && !f.not_editable.empty() && f.identities.size() == 1 ); }
		else { keys++; TEST_CHECK_( ctx.identity_map.entries.count( f.key ) && f.value && ctx.identity_map.entries.at( f.key ).value == f.value, "%s: the map's value", f.key.c_str() ); }
		TEST_CHECK_( !f.rule.empty() && f.value && f.length == strlen( f.value ), "%s: rule and value", f.key.c_str() );
	}
	// derived: the MAC family dependents (Wi-Fi Direct, Wi-Fi Direct #2, Bluetooth PAN), the EUI-64 form, NameSamCompatible,
	// SusClientIdValidation, the flash drive's disk serial (the install history belongs to identity/install, SIDs to the prefix)
	TEST_CHECK_( keys == ctx.identity_map.entries.size() && derived >= 7, "%zu map keys of %zu, %zu derived", keys, ctx.identity_map.entries.size(), derived );
	const IdentityEntry* wifi = pg_find( r.identity.identity, "mac", "3C:A9:F4:10:20:30" );
	const IdentityEntry* direct = pg_find( r.identity.identity, "mac", "3C:A9:F4:10:20:31" );
	const IdentityEntry* cn = pg_find( r.identity.identity, "computer_name", "DESKTOP-FIXTUR2" );
	const IdentityEntry* cont = pg_find( r.identity.identity, "container_id", "{12345678-9ABC-11F0-8123-3CA9F4102030}" );
	TEST_ASSERT( wifi && direct && cn && cont );
	const FieldInfo* fw = pg_field( v, "identity/" + wifi->id );
	const FieldInfo* fd = pg_field( v, "derived/" + direct->id );
	const FieldInfo* fi = pg_field( v, "identity/install" );
	const FieldInfo* fu = pg_field( v, "smbios/t1/h0001/uuid" );
	TEST_ASSERT( fw && fd && fi && fu );
	TEST_CHECK( fw->editable && fw->rule == "3" && fw->location.rfind( "network_adapters.txt:", 0 ) == 0 && fw->occurrences >= 1 );
	TEST_CHECK_( std::count_if( fw->relations.begin(), fw->relations.end(), []( const std::string& s ) { return s.find( "node of version-1 GUID" ) != std::string::npos; } ) == 2, "the Wi-Fi MAC is the node of 2 version-1 GUIDs" );
	TEST_CHECK( !fd->relations.empty() && fd->relations[ 0 ].find( "+ 1" ) != std::string::npos );
	TEST_CHECK( !fi->editable && fi->identities.size() >= 12 && fu->rule == "1-rfc4122" && fu->location.rfind( "smbios.bin 0x", 0 ) == 0 && !fu->relations.empty() );
	// set_value: refused for a wrong format, the real value, a value another key holds, a non-editable / unknown key
	auto refused = [ & ]( const std::string& key, const char* value, const char* why ) {
		try { ProfileGen->set_value( ctx, key, value ); }
		catch ( const std::exception& e ) { return std::string( e.what() ).find( why ) != std::string::npos; }
		return false;
	};
	const std::string kw = "identity/" + wifi->id, kc = "identity/" + cn->id;
	TEST_CHECK( refused( kw, "3C:A9:F4:10:20:30", "captured (real) value" ) );
	TEST_CHECK( refused( kw, "3E:A9:F4:10:20:30", "OUI" ) && refused( kw, "3c:a9:f4:aa:bb:cc", "upper-case" ) && refused( kw, "3C:A9:F4:FF:FF:FE", "room" ) );
	TEST_CHECK( refused( kc, "DESKTOP-FIXTUR2", "captured (real) value" ) && refused( kc, "LAPTOP-1234567", "DESKTOP-" ) );
	TEST_CHECK( refused( "identity/install", "1700000000", "not editable" ) && refused( "nothing/here", "x", "not a field" ) );
	const IdentityEntry* wired_mac = pg_find( r.identity.identity, "mac", "A0:B1:C2:01:02:03" );
	TEST_ASSERT( wired_mac != nullptr );
	TEST_CHECK( refused( "identity/" + wired_mac->id, ctx.identity_map.entries.at( "identity/" + wired_mac->id ).value.c_str(), "" ) == false );	// its own value: accepted (no change)
	const std::string cont_key = "identity/" + cont->id;
	const std::string cont_v = ctx.identity_map.entries.at( cont_key ).value;
	std::string other_guid = cont_v;
	other_guid[ 3 ] = other_guid[ 3 ] == 'A' ? 'B' : 'A';	// another timestamp
	TEST_CHECK( refused( cont_key, other_guid.c_str(), "only the clock sequence" ) );
	// set_value: the computer name; gen_value: the Wi-Fi base MAC (its v1 GUID nodes follow in the map)
	const FieldInfo sc = ProfileGen->set_value( ctx, kc, "DESKTOP-ABCDEF1" );
	TEST_CHECK( std::string( sc.value ) == "DESKTOP-ABCDEF1" && ctx.identity_map.entries.at( kc ).value == "DESKTOP-ABCDEF1" && ctx.applying );
	TEST_CHECK( refused( "identity/" + wired_mac->id, ctx.identity_map.entries.at( kw ).value.c_str(), "OUI" ) );	// another key's value (another OUI here)
	const std::string old_wifi = ctx.identity_map.entries.at( kw ).value;
	const FieldInfo gw = ProfileGen->gen_value( ctx, kw );
	const std::string new_wifi = gw.value;
	TEST_CHECK( new_wifi != old_wifi && new_wifi.substr( 0, 9 ) == "3C:A9:F4:" && ctx.identity_map.entries.at( kw ).value == new_wifi );
	std::string new_node;
	for ( char ch : new_wifi ) if ( ch != ':' ) new_node += char( tolower( uint8_t( ch ) ) );
	const GuidV1 cg = ProfileGenerator::parse_guid_v1( ctx.identity_map.entries.at( cont_key ).value.c_str() );
	TEST_CHECK_( cg.ok && _stricmp( cg.node.c_str(), new_node.c_str() ) == 0 && cg.timestamp == ProfileGenerator::parse_guid_v1( cont_v.c_str() ).timestamp, "the container's version-1 node followed the new MAC in the map" );
	{
		const ProfileView after = ProfileGen->list_fields( ctx );
		const FieldInfo* fa = pg_field( after, kw );
		TEST_CHECK( fa && std::string( fa->value ) == new_wifi );
	}
	// uniqueness: one interface GUID cannot take another one's value
	const IdentityEntry* if0 = pg_find( r.identity.identity, "interface_guid", "{0F0F0F0F-1111-4222-8333-444455556666}" );
	const IdentityEntry* if1 = pg_find( r.identity.identity, "interface_guid", "{2D2D2D2D-1111-4222-8333-444455556666}" );
	TEST_ASSERT( if0 && if1 );
	TEST_CHECK( refused( "identity/" + if0->id, ctx.identity_map.entries.at( "identity/" + if1->id ).value.c_str(), "already the value of" ) );
	// a fresh value for EVERY other editable key: each differs, keeps its format (the checks), the build applies all
	std::map<std::string, std::string> before;
	for ( auto& [ k, e ] : ctx.identity_map.entries ) before[ k ] = e.value;
	size_t regenerated = 0;
	for ( const FieldInfo& f : ProfileGen->list_fields( ctx ).fields )
	{
		if ( !f.editable || f.key == kw || f.key == kc ) continue;
		const FieldInfo g = ProfileGen->gen_value( ctx, f.key );
		TEST_CHECK_( g.value && before[ f.key ] != g.value, "%s (%s): a new value", f.key.c_str(), f.kind.c_str() );
		regenerated++;
	}
	TEST_CHECK( regenerated + 3 >= keys );
	const GenResult r2 = ProfileGen->gen_build( ctx, in );	// application of the edited map (nothing drawn)
	TEST_ASSERT_( r2.ok, "the edited map applied: %s", r2.error.c_str() );
	TEST_CHECK( !r2.generated );
	const IdentityEntry* cn2 = pg_find( r2.identity.identity, "computer_name", "DESKTOP-FIXTUR2" );
	const IdentityEntry* w2 = pg_find( r2.identity.identity, "mac", "3C:A9:F4:10:20:30" );
	const IdentityEntry* d2 = pg_find( r2.identity.identity, "mac", "3C:A9:F4:10:20:31" );
	const IdentityEntry* ns2 = pg_find( r2.identity.identity, "name_sam_compatible", "DESKTOP-FIXTUR2\\Fixer" );
	TEST_ASSERT( cn2 && w2 && d2 && ns2 );
	TEST_CHECK( cn2->synthetic == "DESKTOP-ABCDEF1" && w2->synthetic == new_wifi && ns2->synthetic.rfind( "DESKTOP-ABCDEF1\\", 0 ) == 0 );
	TEST_CHECK_( std::stoull( d2->synthetic.substr( 9, 2 ) + d2->synthetic.substr( 12, 2 ) + d2->synthetic.substr( 15, 2 ), nullptr, 16 ) == std::stoull( new_wifi.substr( 9, 2 ) + new_wifi.substr( 12, 2 ) + new_wifi.substr( 15, 2 ), nullptr, 16 ) + 1,
		"Wi-Fi Direct = the edited Wi-Fi MAC + 1 (re-derived by the build)" );
	TEST_CHECK( r2.synthetic.at( "identity.tsv" ) != r.synthetic.at( "identity.tsv" ) && ProfileGenerator::check_view( ProfileGen->list_fields( ctx ), r2.identity.needles ) == 0 );
	// the saved edited map reproduces the edited profile in a new context
	const Bytes& mb = r2.synthetic.at( "identity_map.tsv" );
	BuildContext ctx2 = ProfileGen->new_context( pg_cfg( Environment::bare_host ) );
	ProfileGen->load_identity_maps( ctx2, ProfileGenerator::parse_identity_map( std::string( mb.begin(), mb.end() ) ), {} );
	const GenResult r3 = ProfileGen->gen_build( ctx2, in );
	TEST_CHECK( r3.ok && !r3.generated && r3.synthetic == r2.synthetic );
}

// ── the synthetic text views (gap P1 #7, user decision 2026-10-17 "Export synthetic views", U1518) ───────────────────────
static std::vector<std::string> pg_lines( const Bytes& b )
{
	std::vector<std::string> v;
	std::string cur;
	for ( uint8_t c : b )
	{
		if ( c == '\n' ) { v.push_back( cur ); cur.clear(); }
		else if ( c != '\r' ) cur += char( c );
	}
	if ( !cur.empty() ) v.push_back( cur );
	return v;
}
// views\<file> in both profiles: every presentable text file (not capture_info.txt / certificates.txt), the same line count
// as the capture; at every field-map text row the view holds the identity's synthetic value in that occurrence's form and
// case (MAC colon / plain, GUID without braces, 0x hex + decimal FILETIME, the hex dump of rule 8c, ...); no real value in a
// synthetic view; the original views are the capture text; excluded tool-artifact lines are marker lines in both; the
// manifest lists the views; application reproduces them byte for byte. Fixture 3 holds a registry last-write time in the
// same second as a device's first install (exact by decision K in the blobs): in the view it moves by the install-history
// offset like the event itself (user decision 2026-10-17 "Shift by the offset"), so neither the real time nor the offset
// can be read off the view
static void pg_views( void )
{
	for ( bool dump : { false, true } )
	{
		CaptureInputs in = pg_capture( false, pg_text_fixture3( dump ) );
		BuildContext ctx = pg_fresh( Environment::bare_host );
		TEST_CHECK( ctx.config.views );	// the default BuildConfig writes the views
		const GenResult r = ProfileGen->gen_build( ctx, in );
		TEST_ASSERT_( r.ok, "build: %s", r.error.c_str() );
		size_t views = 0, rows = 0, rows_ok = 0, hits = 0;
		std::string bad_row;	// the first row not replaced in its form (location only)
		for ( const TextFile& f : in.texts )
		{
			const std::string key = "views\\" + f.name;
			const bool want = f.name != "capture_info.txt" && f.name != "certificates.txt";
			TEST_CHECK_( r.synthetic.count( key ) == ( want ? 1u : 0u ) && r.original.count( key ) == ( want ? 1u : 0u ), "%s: %s", f.name.c_str(), want ? "a view" : "no view" );
			if ( !want ) continue;
			views++;
			const Bytes& sv = r.synthetic.at( key );
			TEST_CHECK_( r.original.at( key ) == f.data, "%s: the original view is the capture text", f.name.c_str() );
			const std::vector<std::string> cl = pg_lines( f.data ), vl = pg_lines( sv );
			TEST_CHECK_( cl.size() == vl.size(), "%s: %zu lines (capture %zu)", f.name.c_str(), vl.size(), cl.size() );
			for ( const FieldMapEntry& fm : r.identity.fields )
			{
				if ( fm.area != f.name || fm.rule.back() == 'r' || !fm.line || fm.line > vl.size() || fm.location.find( "hex dump" ) != std::string::npos ) continue;	// the dump rows: checked as one blob below
				const IdentityEntry* id = nullptr;
				for ( const IdentityEntry& e : r.identity.identity ) if ( e.id == fm.id ) id = &e;
				if ( !id ) continue;
				rows++;
				const std::string got = vl[ fm.line - 1 ].substr( size_t( fm.offset ) - 1, fm.length ), was = cl[ fm.line - 1 ].substr( size_t( fm.offset ) - 1, fm.length );
				bool ok = _stricmp( got.c_str(), was.c_str() ) != 0 && got.size() == was.size();
				for ( size_t c = 0; ok && c < got.size(); c++ )	// the form kept: separators at the same places, letters in the same case class
					ok = ( isalnum( uint8_t( was[ c ] ) ) != 0 ) == ( isalnum( uint8_t( got[ c ] ) ) != 0 ) && ( isalnum( uint8_t( was[ c ] ) ) || was[ c ] == got[ c ] ) &&
						 !( islower( uint8_t( was[ c ] ) ) && isupper( uint8_t( got[ c ] ) ) ) && !( isupper( uint8_t( was[ c ] ) ) && islower( uint8_t( got[ c ] ) ) );
				rows_ok += ok;
				if ( !ok && bad_row.empty() ) bad_row = f.name + ":" + std::to_string( fm.line ) + ":" + std::to_string( fm.offset ) + " " + id->id + " " + id->kind + " (" + fm.location + ")";
			}
			std::string low( sv.begin(), sv.end() );
			for ( char& c : low ) c = char( tolower( uint8_t( c ) ) );
			for ( const IdentityEntry& e : r.identity.identity )
			{
				std::string x = e.original;
				if ( x.size() == 38 && x.front() == '{' ) x = x.substr( 1, 36 );
				for ( char& c : x ) c = char( tolower( uint8_t( c ) ) );
				if ( x.size() < 6 || low.find( x ) == std::string::npos ) continue;
				hits++;
				TEST_MSG( "%s: the real value of %s (%s) in the synthetic view", f.name.c_str(), e.id.c_str(), e.kind.c_str() );
			}
		}
		TEST_CHECK_( views >= 15 && rows > 50 && rows_ok == rows && hits == 0, "dump %d: %zu views, %zu field-map rows replaced in form %zu, real values found %zu; first row not in form: %s", dump, views, rows, rows_ok, hits, bad_row.c_str() );
		// the FILETIME forms agree in the view (hex = decimal), the hex dump is the synthetic blob
		const std::vector<std::string> os = pg_lines( r.synthetic.at( "views\\os_ids.txt" ) );
		size_t ft = 0;
		for ( const std::string& l : os )
		{
			std::smatch m;
			if ( !std::regex_search( l, m, std::regex( R"(\"InstallTime\".*: 0x([0-9A-F]{16}) \(([0-9]+)\))" ) ) ) continue;
			ft++;
			TEST_CHECK( std::stoull( m[ 1 ].str(), nullptr, 16 ) == std::stoull( m[ 2 ].str() ) && m[ 2 ].str() != "133327264661234567" );
		}
		TEST_CHECK( ft == 1 );
		// every last-write time of the view = the capture's + the install-history offset; the original view exact
		const IdentityEntry* idate = pg_find( r.identity.identity, "install_date", "1688252866" );
		TEST_ASSERT( idate != nullptr );
		const long long shift = std::stoll( idate->synthetic ) - 1688252866LL;
		auto lw = []( const Bytes& b ) {
			std::vector<long long> v;
			const std::string t( b.begin(), b.end() );
			static const std::regex r_lw( R"(last write ([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})Z)" );
			for ( std::sregex_iterator it( t.begin(), t.end(), r_lw ), e; it != e; ++it )
			{
				std::tm tm {};
				tm.tm_year = std::stoi( ( *it )[ 1 ].str() ) - 1900;
				tm.tm_mon = std::stoi( ( *it )[ 2 ].str() ) - 1;
				tm.tm_mday = std::stoi( ( *it )[ 3 ].str() );
				tm.tm_hour = std::stoi( ( *it )[ 4 ].str() );
				tm.tm_min = std::stoi( ( *it )[ 5 ].str() );
				tm.tm_sec = std::stoi( ( *it )[ 6 ].str() );
				v.push_back( (long long)_mkgmtime( &tm ) );
			}
			return v;
		};
		const Bytes& ev = r.synthetic.at( "views\\enum_registry.txt" );
		const std::vector<long long> lc = lw( in.texts[ 2 ].data ), lv = lw( ev ), lo = lw( r.original.at( "views\\enum_registry.txt" ) );
		bool shifted = !lc.empty() && lc.size() == lv.size() && lo == lc;
		for ( size_t i = 0; shifted && i < lc.size(); i++ ) shifted = lv[ i ] - lc[ i ] == shift;
		TEST_CHECK_( shifted, "%zu last-write time(s): view = capture + the install-history offset, the original view exact", lc.size() );
		TEST_CHECK( std::string( ev.begin(), ev.end() ).find( "2023-07-01T23:06:46Z" ) == std::string::npos );	// the real install-event second nowhere in the view
		const IdentityEntry* sus = nullptr;
		for ( const IdentityEntry& e : r.identity.identity ) if ( e.kind == "sus_client_id_validation" ) sus = &e;
		TEST_ASSERT( sus != nullptr );
		const std::string ut( r.synthetic.at( "views\\update_ids.txt" ).begin(), r.synthetic.at( "views\\update_ids.txt" ).end() );
		std::string hex;
		if ( !dump ) TEST_CHECK( ut.find( sus->synthetic ) != std::string::npos );
		else
		{
			for ( const std::string& l : pg_lines( r.synthetic.at( "views\\update_ids.txt" ) ) )
			{
				std::smatch m;
				if ( std::regex_match( l, m, std::regex( R"(^\s+[0-9A-Fa-f]{4}: ((?:[0-9A-Fa-f]{2} )+)\s*\|.*\|$)" ) ) )
					for ( char c : m[ 1 ].str() ) if ( c != ' ' ) hex += c;
			}
			TEST_CHECK_( hex == sus->synthetic, "the hex dump of SusClientIdValidation is the synthetic blob" );
		}
		TEST_CHECK( std::string( r.synthetic.at( "manifest.txt" ).begin(), r.synthetic.at( "manifest.txt" ).end() ).find( "views\\enum_registry.txt" ) != std::string::npos );
		// application of the saved map: the views byte-identical
		const Bytes& mb = r.synthetic.at( "identity_map.tsv" );
		BuildConfig cv = pg_cfg( Environment::bare_host );
		cv.views = true;
		BuildContext ctx2 = ProfileGen->new_context( cv );
		ProfileGen->load_identity_maps( ctx2, ProfileGenerator::parse_identity_map( std::string( mb.begin(), mb.end() ) ), {} );
		const GenResult r2 = ProfileGen->gen_build( ctx2, in );
		TEST_CHECK( r2.ok && r2.synthetic == r.synthetic && r2.original == r.original );
		// views off (a BuildConfig written before U1518): no views\ file, the config text without the key; "views = yes" round trips
		BuildConfig off = pg_cfg( Environment::bare_host );
		off.views = false;
		BuildContext ctx3 = ProfileGen->new_context( off );
		ProfileGen->load_identity_maps( ctx3, ProfileGenerator::parse_identity_map( std::string( mb.begin(), mb.end() ) ), {} );
		const GenResult r3 = ProfileGen->gen_build( ctx3, in );
		TEST_CHECK( r3.ok && std::none_of( r3.synthetic.begin(), r3.synthetic.end(), []( const auto& kv ) { return kv.first.rfind( "views\\", 0 ) == 0; } ) );
		TEST_CHECK( off.save().find( "views" ) == std::string::npos && !BuildConfig::load( off.save() ).views && BuildConfig::load( cv.save() ).views && BuildConfig::load( cv.save() ) == cv );
	}
	// the other install-history timestamps a view shows: "key last write time: <ISO>" (registry_bios.txt) and the code-integrity
	// aggregation FILETIMEs (REG_QWORD hex + decimal, code_integrity.txt) move by the offset; a QWORD that is no time stays
	{
		std::vector<TextFile> f = pg_text_fixture3();
		const std::string bios = "## HKLM\\HARDWARE\\DESCRIPTION\\System\\BIOS\n  key last write time: 2023-07-01T23:06:46Z; 3 value(s), 0 subkey(s)\n";
		const std::string ci = "## HKLM\\SYSTEM\\CurrentControlSet\\Control\\CI\\Aggregation\n  depth 1; last write 2024-01-02T03:04:05Z; 2 value(s), 0 subkey(s)\n"
							   "  value \"0123456789abcdef\"  REG_QWORD (11)  8 bytes: 0x01D9AC70B56F981C (133327264064444444)\n"
							   "  value \"Counter\"  REG_QWORD (11)  8 bytes: 0x0000000000000021 (33)\n";
		f.push_back( { "registry_bios.txt", Bytes( bios.begin(), bios.end() ) } );
		f.push_back( { "code_integrity.txt", Bytes( ci.begin(), ci.end() ) } );
		CaptureInputs in = pg_capture( false, f );
		BuildContext c = pg_fresh( Environment::bare_host );
		const GenResult rt = ProfileGen->gen_build( c, in );
		TEST_ASSERT_( rt.ok, "build: %s", rt.error.c_str() );
		const IdentityEntry* idate = pg_find( rt.identity.identity, "install_date", "1688252866" );
		TEST_ASSERT( idate != nullptr );
		const long long shift = std::stoll( idate->synthetic ) - 1688252866LL;
		const std::string vb( rt.synthetic.at( "views\\registry_bios.txt" ).begin(), rt.synthetic.at( "views\\registry_bios.txt" ).end() );
		const std::string vc( rt.synthetic.at( "views\\code_integrity.txt" ).begin(), rt.synthetic.at( "views\\code_integrity.txt" ).end() );
		std::smatch m;
		TEST_CHECK( vb.find( "2023-07-01T23:06:46Z" ) == std::string::npos && std::regex_search( vb, m, std::regex( "key last write time: ([0-9T:Z-]{20});" ) ) );
		std::tm tm {};
		sscanf( m[ 1 ].str().c_str(), "%d-%d-%dT%d:%d:%dZ", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec );
		tm.tm_year -= 1900;
		tm.tm_mon -= 1;
		TEST_CHECK_( (long long)_mkgmtime( &tm ) - 1688252806LL == shift, "the key last-write time moved by the offset" );
		TEST_CHECK( std::regex_search( vc, m, std::regex( R"(8 bytes: 0x([0-9A-F]{16}) \(([0-9]+)\))" ) ) );
		const unsigned long long h = std::stoull( m[ 1 ].str(), nullptr, 16 ), d = std::stoull( m[ 2 ].str() );
		TEST_CHECK_( h == d && (long long)( d - 133327264064444444ull ) == shift * 10000000LL, "the CI FILETIME moved by the offset in both forms" );
		TEST_CHECK( vc.find( "0x0000000000000021 (33)" ) != std::string::npos && vc.find( "2024-01-02T03:04:05Z" ) == std::string::npos );
	}
	// a USB serial truncated in a SetupAPI instance ID (the first 30 of the flash drive's 48 characters): the same prefix of
	// its synthetic serial, one "4dr" relation row; the truncated real form nowhere in a synthetic file
	{
		std::vector<TextFile> f = pg_text_fixture3();
		const std::string flash = k_pg_flash_usb, cut = flash.substr( 0, 30 );
		const std::string add = "=== device 9\n  instance ID: \"USBSTOR\\x5CDISK&VEN_FIX&PROD_FLASH&REV_1.00\\x5C" + cut + "\"\n";
		f[ 3 ].data.insert( f[ 3 ].data.end(), add.begin(), add.end() );
		CaptureInputs in = pg_capture( false, f );
		BuildContext c = pg_fresh( Environment::bare_host );
		const GenResult rp = ProfileGen->gen_build( c, in );
		TEST_ASSERT_( rp.ok, "build: %s", rp.error.c_str() );
		const IdentityEntry* pre = pg_find( rp.identity.identity, "usb_serial_prefix", cut );
		const IdentityEntry* usb = pg_find( rp.identity.identity, "usb_serial", flash );
		TEST_ASSERT( pre != nullptr && usb != nullptr );
		TEST_CHECK_( pre->rule == "4d" && pre->synthetic == usb->synthetic.substr( 0, 30 ) && pre->occurrences == 1, "the truncated serial = the synthetic serial's first 30 characters" );
		size_t rel = 0;
		for ( const FieldMapEntry& fm : rp.identity.fields ) rel += fm.rule == "4dr" && fm.id == pre->id;
		TEST_CHECK( rel == 1 );
		const std::string pv( rp.synthetic.at( "views\\pci_devices.txt" ).begin(), rp.synthetic.at( "views\\pci_devices.txt" ).end() );
		TEST_CHECK( pv.find( cut ) == std::string::npos && pv.find( "REV_1.00\\x5C" + usb->synthetic.substr( 0, 30 ) + "\"" ) != std::string::npos );
	}
	// rule 4c on the 16-byte REG_BINARY form (Windows byte order): the container GUID {12345678-9ABC-11F0-8123-3CA9F4102030} in
	// binary form follows its synthetic value; a version-1 GUID with the Wi-Fi MAC as node that occurs only in binary form is a
	// new mac_node_guid identity (timestamp + the offset, node = the synthetic Wi-Fi MAC); a saved map without it applies only
	// with add_new_keys (exactly that key drawn); the real binary forms nowhere in a synthetic file
	{
		auto bin = []( const std::string& g ) {	// the Windows byte order of a GUID text, upper-case hex
			std::string h;
			for ( char c : g ) if ( isxdigit( uint8_t( c ) ) ) h += char( toupper( uint8_t( c ) ) );
			return h.substr( 6, 2 ) + h.substr( 4, 2 ) + h.substr( 2, 2 ) + h.substr( 0, 2 ) + h.substr( 10, 2 ) + h.substr( 8, 2 ) + h.substr( 14, 2 ) + h.substr( 12, 2 ) + h.substr( 16 );
		};
		const std::string known = bin( "12345678-9abc-11f0-8123-3ca9f4102030" ), only_bin = bin( "a1b2c3d4-0001-11f0-9abc-3ca9f4102030" );
		const std::string svc = "## HKLM\\SYSTEM\\CurrentControlSet\\Services\\FIXSF\\Parameters\\NdisAdapters\\0\n"
								"  value \"InterfaceGuid\"  REG_BINARY (3)  16 bytes: " + known + "\n"
								"## HKLM\\SYSTEM\\CurrentControlSet\\Services\\FIXSF\\Parameters\\NdisAdapters\\1\n"
								"  value \"InterfaceGuid\"  REG_BINARY (3)  16 bytes: " + only_bin + "\n";
		std::vector<TextFile> f = pg_text_fixture3();
		const CaptureInputs plain = pg_capture( false, f );
		f.push_back( { "services_vm.txt", Bytes( svc.begin(), svc.end() ) } );
		const CaptureInputs in = pg_capture( false, f );
		BuildContext c = pg_fresh( Environment::bare_host );
		const GenResult rg = ProfileGen->gen_build( c, in );
		TEST_ASSERT_( rg.ok, "build: %s", rg.error.c_str() );
		const IdentityEntry* cont = pg_find( rg.identity.identity, "container_id", "{12345678-9ABC-11F0-8123-3CA9F4102030}" );
		const IdentityEntry* nb = pg_find( rg.identity.identity, "mac_node_guid", "{A1B2C3D4-0001-11F0-9ABC-3CA9F4102030}" );
		const IdentityEntry* wifi = pg_find( rg.identity.identity, "mac", "3C:A9:F4:10:20:30" );
		const IdentityEntry* idate = pg_find( rg.identity.identity, "install_date", "1688252866" );
		TEST_ASSERT( cont && nb && wifi && idate );
		std::string wn;
		for ( char ch : wifi->synthetic ) if ( ch != ':' ) wn += ch;
		const GuidV1 ra = ProfileGenerator::parse_guid_v1( nb->original.c_str() ), sa = ProfileGenerator::parse_guid_v1( nb->synthetic.c_str() );
		TEST_CHECK_( ra.ok && sa.ok && _stricmp( sa.node.c_str(), wn.c_str() ) == 0 && (long long)( sa.timestamp - ra.timestamp ) == ( std::stoll( idate->synthetic ) - 1688252866LL ) * 10000000LL,
			"the binary-only version-1 GUID: node = the synthetic Wi-Fi MAC, timestamp = real + the offset" );
		const std::string sv( rg.synthetic.at( "views\\services_vm.txt" ).begin(), rg.synthetic.at( "views\\services_vm.txt" ).end() );
		TEST_CHECK( sv.find( known ) == std::string::npos && sv.find( only_bin ) == std::string::npos && sv.find( bin( cont->synthetic ) ) != std::string::npos && sv.find( bin( nb->synthetic ) ) != std::string::npos );
		// a map saved before the binary rule (the capture without the file) applies to the capture with it only with add_new_keys
		BuildContext c0 = pg_fresh( Environment::bare_host );
		const GenResult r0 = ProfileGen->gen_build( c0, plain );
		TEST_ASSERT( r0.ok );
		const Bytes& mb = r0.synthetic.at( "identity_map.tsv" );
		const IdentityMap saved = ProfileGenerator::parse_identity_map( std::string( mb.begin(), mb.end() ) );
		BuildContext c1 = ProfileGen->new_context( pg_cfg( Environment::bare_host ) );
		ProfileGen->load_identity_maps( c1, saved, {} );
		const GenResult r1 = ProfileGen->gen_build( c1, in );
		TEST_CHECK_( !r1.ok && r1.error.find( "has no value for" ) != std::string::npos, "without add_new_keys: %s", r1.error.c_str() );
		BuildContext c2 = ProfileGen->new_context( pg_cfg( Environment::bare_host ) );
		ProfileGen->load_identity_maps( c2, saved, {} );
		c2.add_new_keys = true;
		const GenResult r2 = ProfileGen->gen_build( c2, in );
		TEST_ASSERT_( r2.ok, "with add_new_keys: %s", r2.error.c_str() );
		size_t same = 0;
		for ( auto& [ k, e ] : saved.entries ) same += r2.identity_map.entries.count( k ) && r2.identity_map.entries.at( k ) == e;
		TEST_CHECK_( c2.added_keys.size() == 1 && r2.identity_map.entries.size() == saved.entries.size() + 1 && same == saved.entries.size(), "exactly one key added (%zu), every saved value kept (%zu of %zu)",
			c2.added_keys.size(), same, saved.entries.size() );
	}
	// a brand name: the host's rule-2h memory Manufacturer ("FixtureRAM" in the SMBIOS fixture) as a HID device's manufacturer /
	// product name (exact by decision H) passes on those name lines of views\hid.txt; on any other line it is refused
	for ( bool elsewhere : { false, true } )
	{
		std::vector<TextFile> f = pg_text_fixture3();
		for ( TextFile& t : f )
			if ( t.name == "hid.txt" )
			{
				const std::string add = "=== HID device 4\nmanufacturer: \"FixtureRAM\"\nproduct: \"FixtureRAM Keyboard\"\n";
				t.data.insert( t.data.end(), add.begin(), add.end() );
			}
			else if ( elsewhere && t.name == "disks.txt" )
			{
				const std::string add = "  note: FixtureRAM\n";
				t.data.insert( t.data.end(), add.begin(), add.end() );
			}
		CaptureInputs in = pg_capture( false, f );
		BuildContext c = pg_fresh( Environment::bare_host );
		const GenResult rb = ProfileGen->gen_build( c, in );
		if ( !elsewhere ) TEST_CHECK_( rb.ok && rb.log.text().find( "2 device name(s) in views equal to a rule-2h memory Manufacturer brand" ) != std::string::npos, "brand on name lines: %s", rb.error.c_str() );
		else TEST_CHECK_( !rb.ok && rb.error.find( "views\\disks.txt" ) != std::string::npos, "the brand on another line is refused: %s", rb.error.c_str() );
	}
	// excluded tool artifacts: marker lines in both profiles' views, the line count kept
	const std::vector<ToolExclusion> items = { { "FixTool Printer", { "FixTool Printer", "FixTool v4 Printer Driver" }, "fixture printer" }, { "FixDbg", { "FixDbg" }, "fixture driver" } };
	BuildConfig cb = pg_cfg( Environment::bare_host );
	cb.exclusions = items;
	cb.views = true;
	const std::vector<TextFile> files = pg_tool_fixture( false );
	CaptureInputs cap = pg_capture( false, files );
	BuildContext ctx = pg_fresh( Environment::bare_host );
	ctx = ProfileGen->new_context( cb );
	const GenResult r = ProfileGen->gen_build( ctx, cap );
	TEST_ASSERT_( r.ok, "build: %s", r.error.c_str() );
	const std::vector<std::string> pv = pg_lines( r.synthetic.at( "views\\printers.txt" ) ), po = pg_lines( r.original.at( "views\\printers.txt" ) );
	TEST_CHECK( pv.size() == pg_lines( files[ 6 ].data ).size() && pv[ 0 ] == "# [excluded tool artifact]" && pv[ 3 ] == pv[ 0 ] && po[ 0 ] == pv[ 0 ] && pv[ 5 ] == "=== printer 1" );
	for ( const auto& [ name, data ] : r.synthetic )
		if ( name.rfind( "views\\", 0 ) == 0 )
		{
			const std::string t( data.begin(), data.end() );
			TEST_CHECK_( t.find( "FixTool" ) == std::string::npos && t.find( "FixDbg" ) == std::string::npos, "%s: no tool artifact text", name.c_str() );
		}
}

// ── atomic profile writes (user decision 2026-10-17 "Atomic writes", U1515) ─────────────────────────────────────────────
// the test's own cleanup: exactly the files it wrote (relative paths of files), then the folders they emptied and dir -
// single non-recursive removes, never a recursive delete
static void pg_remove_written( const fs::path& dir, const std::map<std::string, Bytes>& files )
{
	std::error_code ec;
	std::set<fs::path> sub;
	for ( auto& [ rel, data ] : files )
	{
		fs::remove( dir / rel, ec );
		for ( fs::path p = ( dir / rel ).parent_path(); p != dir && p.native().size() > dir.native().size(); p = p.parent_path() ) sub.insert( p );
	}
	std::vector<fs::path> order( sub.begin(), sub.end() );
	std::sort( order.begin(), order.end(), []( const fs::path& a, const fs::path& b ) { return a.native().size() > b.native().size(); } );
	for ( const fs::path& p : order ) fs::remove( p, ec );
	fs::remove( dir, ec );
}
// every regular file below dir (relative path -> bytes), the layout save_profile writes
static std::map<std::string, Bytes> pg_read_folder( const fs::path& dir )
{
	std::map<std::string, Bytes> m;
	std::error_code ec;
	for ( auto it = fs::recursive_directory_iterator( dir, ec ); !ec && it != fs::recursive_directory_iterator(); it.increment( ec ) )
		if ( it->is_regular_file() )
		{
			std::ifstream f( it->path(), std::ios::binary );
			m[ fs::relative( it->path(), dir ).string() ] = Bytes( std::istreambuf_iterator<char>( f ), std::istreambuf_iterator<char>() );
		}
	return m;
}
// guest + host written in PARALLEL threads into temp folders outside any git work tree (synthetic + original profile each):
// both complete (every file, nothing else, no ".partial" / ".old" left), shared/ keys equal in both maps on disk; re-runs
// that read their maps from disk while the other side switches its folder give byte-identical files; a reader polling a
// map during 25 switches always gets the complete map; an interrupted run's ".partial" / ".old" stops the write and stays
// untouched; a folder that is no validated profile folder is never replaced
static void pg_atomic_writes( void )
{
	const fs::path base = fs::temp_directory_path() / ( "pg_atomic_" + std::to_string( GetCurrentProcessId() ) );
	TEST_ASSERT_( ProfileGenerator::check_git_work_tree( base ).empty(), "the temp folder is outside any git work tree" );
	const PgEnvPair env = pg_env_pair();
	BuildContext ctx = pg_fresh( Environment::bare_host );
	const GenResult gh = ProfileGen->gen_build( ctx, env.host );
	TEST_ASSERT_( gh.ok, "host generation: %s", gh.error.c_str() );
	BuildContext gctx = ProfileGen->new_context( pg_cfg( Environment::vmware_guest ) );
	ProfileGen->load_identity_maps( gctx, {}, gh.identity_map );
	const GenResult gg = ProfileGen->gen_build( gctx, env.guest );
	TEST_ASSERT_( gg.ok, "guest generation: %s", gg.error.c_str() );
	struct Side
	{
		Environment e;
		const CaptureInputs* in;
		const GenResult* gen;
		fs::path syn, org;
		std::string error;	// a worker's exception (checked on the main thread)
	};
	Side sides[ 2 ] = { { Environment::vmware_guest, &env.guest, &gg }, { Environment::bare_host, &env.host, &gh } };
	for ( Side& s : sides )
	{
		s.syn = base / "Emulator" / "data" / "platform" / env_dir_name( s.e );
		s.org = base / "capture" / "profiles_original" / env_dir_name( s.e );
		TEST_CHECK_( ProfileGenerator::check_profile_dir( s.syn ).ok && ProfileGenerator::check_profile_dir( s.org ).ok, "%s: both folders are valid profile locations", env_dir_name( s.e ) );
	}
	TEST_CHECK( !ProfileGenerator::check_profile_dir( base / "plain" / "vmware_guest" ).ok && !ProfileGenerator::check_profile_dir( base / "Emulator" / "data" / "platform" / "other" ).ok );
	auto siblings_gone = [ & ]( const fs::path& d ) {
		std::error_code ec;
		return !fs::exists( d.parent_path() / ( d.filename().native() + L".partial" ), ec ) && !fs::exists( d.parent_path() / ( d.filename().native() + L".old" ), ec );
	};
	// round 0: the first write of both environments in parallel
	auto first = [ & ]( Side& s ) {
		try
		{
			const ProfileSaveResult a = ProfileGenerator::save_profile( s.syn, s.gen->synthetic, false );
			const ProfileSaveResult b = ProfileGenerator::save_profile( s.org, s.gen->original, true );
			if ( a.replaced || b.replaced || a.files != s.gen->synthetic.size() || b.files != s.gen->original.size() ) s.error = "unexpected first-write result";
		}
		catch ( const std::exception& e ) { s.error = e.what(); }
	};
	{
		std::thread t0( first, std::ref( sides[ 0 ] ) ), t1( first, std::ref( sides[ 1 ] ) );
		t0.join();
		t1.join();
	}
	for ( Side& s : sides )
	{
		TEST_CHECK_( s.error.empty(), "%s first write: %s", env_dir_name( s.e ), s.error.c_str() );
		TEST_CHECK_( pg_read_folder( s.syn ) == s.gen->synthetic && pg_read_folder( s.org ) == s.gen->original, "%s: both profiles complete on disk (%zu / %zu files)", env_dir_name( s.e ),
			s.gen->synthetic.size(), s.gen->original.size() );
		TEST_CHECK( siblings_gone( s.syn ) && siblings_gone( s.org ) );
	}
	const IdentityMapFile mg = ProfileGenerator::load_identity_map( sides[ 0 ].syn / "identity_map.tsv" ), mh = ProfileGenerator::load_identity_map( sides[ 1 ].syn / "identity_map.tsv" );
	TEST_ASSERT( mg.found && mh.found && mg.source == "profile" && !mg.map.has_install_shift && !mh.map.has_install_shift );
	size_t common = 0, same = 0;
	for ( auto& [ k, v ] : mh.map.entries )
		if ( k.rfind( "shared/", 0 ) == 0 && mg.map.entries.count( k ) ) { common++; same += mg.map.entries.at( k ) == v; }
	TEST_CHECK_( common >= 1 && same == common, "%zu shared/ key(s) in both maps on disk, %zu equal", common, same );
	TEST_CHECK( !ProfileGenerator::load_identity_map( base / "Emulator" / "data" / "platform" / "nothing" / "identity_map.tsv" ).found );
	// rounds 1..6: each side reads its own and the peer map from disk (once, at its start - while the other side may be
	// switching its folder), applies them and writes both profiles again: byte-identical every time
	auto rerun = [ & ]( Side& s, const Side& peer ) {
		try
		{
			const IdentityMapFile own = ProfileGenerator::load_identity_map( s.syn / "identity_map.tsv" ), other = ProfileGenerator::load_identity_map( peer.syn / "identity_map.tsv" );
			if ( !own.found || !other.found ) { s.error = "a map was not found"; return; }
			const GenResult r = pg_apply( s.e, *s.in, own.map, other.map );
			if ( !r.ok ) { s.error = r.error; return; }
			const ProfileSaveResult a = ProfileGenerator::save_profile( s.syn, r.synthetic, false );
			const ProfileSaveResult b = ProfileGenerator::save_profile( s.org, r.original, true );
			if ( !a.replaced || !a.old_removed || !b.replaced || !b.old_removed || !a.note.empty() || !b.note.empty() ) s.error = "the previous folder was not replaced and removed";
		}
		catch ( const std::exception& e ) { s.error = e.what(); }
	};
	for ( int round = 1; round <= 6; round++ )
	{
		std::thread t0( rerun, std::ref( sides[ 0 ] ), std::cref( sides[ 1 ] ) ), t1( rerun, std::ref( sides[ 1 ] ), std::cref( sides[ 0 ] ) );
		t0.join();
		t1.join();
		for ( Side& s : sides )
		{
			TEST_CHECK_( s.error.empty(), "round %d %s: %s", round, env_dir_name( s.e ), s.error.c_str() );
			TEST_CHECK_( pg_read_folder( s.syn ) == s.gen->synthetic && pg_read_folder( s.org ) == s.gen->original && siblings_gone( s.syn ) && siblings_gone( s.org ),
				"round %d %s: both profiles byte-identical to the first write, no sibling left", round, env_dir_name( s.e ) );
		}
	}
	// a reader polling the guest map while its folder is switched 25 times: always the complete map (old or new copy)
	{
		std::atomic<bool> done { false };
		size_t reads = 0, missing = 0, wrong = 0, from_old = 0;
		std::string werr;
		std::thread writer( [ & ] {
			try { for ( int i = 0; i < 25; i++ ) ProfileGenerator::save_profile( sides[ 0 ].syn, gg.synthetic, false ); }
			catch ( const std::exception& e ) { werr = e.what(); }
			done = true;
		} );
		while ( !done || reads < 25 )
		{
			const IdentityMapFile m = ProfileGenerator::load_identity_map( sides[ 0 ].syn / "identity_map.tsv" );
			reads++;
			missing += !m.found;
			wrong += m.found && !( m.map.entries == mg.map.entries );
			from_old += m.source != "profile";
			if ( done && reads >= 25 ) break;
		}
		writer.join();
		TEST_CHECK_( werr.empty(), "25 switches: %s", werr.c_str() );
		TEST_CHECK_( missing == 0 && wrong == 0, "%zu reads during 25 folder switches: %zu without a map, %zu incomplete / other (%zu from the previous copy)", reads, missing, wrong, from_old );
		TEST_MSG( "%zu reads, %zu from the previous copy", reads, from_old );
	}
	// an interrupted run's sibling stops the write and stays untouched (the test's own sibling, removed by the test afterwards)
	for ( const wchar_t* suffix : { L".partial", L".old" } )
	{
		const fs::path sib = sides[ 1 ].syn.parent_path() / ( sides[ 1 ].syn.filename().native() + suffix );
		fs::create_directories( sib );
		std::ofstream( sib / "left_over.bin", std::ios::binary ) << "x";
		bool threw = false;
		try { ProfileGenerator::save_profile( sides[ 1 ].syn, gh.synthetic, false ); }
		catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "interrupted profile write" ) != std::string::npos; }
		TEST_CHECK_( threw && fs::exists( sib / "left_over.bin" ), "an existing %ls sibling: write refused, the sibling untouched", suffix );
		TEST_CHECK( pg_read_folder( sides[ 1 ].syn ) == gh.synthetic );
		pg_remove_written( sib, { { "left_over.bin", {} } } );
		TEST_CHECK( !fs::exists( sib ) );
	}
	// a folder that is no validated profile folder: written when new, never replaced
	const fs::path plain = base / "plain" / "profile_copy";
	const std::map<std::string, Bytes> small = { { "smbios.bin", { 1, 2, 3 } }, { "acpi\\acpi_TEST.bin", { 4 } } };
	ProfileGenerator::save_profile( plain, small, false );
	bool threw = false;
	try { ProfileGenerator::save_profile( plain, small, false ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "refusing to replace" ) != std::string::npos; }
	TEST_CHECK_( threw && pg_read_folder( plain ) == small, "an unvalidated folder is never replaced" );
	// cleanup: exactly what the test wrote, then the emptied folders (non-recursive)
	pg_remove_written( plain, small );
	for ( Side& s : sides )
	{
		pg_remove_written( s.syn, s.gen->synthetic );
		pg_remove_written( s.org, s.gen->original );
	}
	std::error_code ec;
	for ( const fs::path& p : { base / "plain", base / "Emulator" / "data" / "platform", base / "Emulator" / "data", base / "Emulator", base / "capture" / "profiles_original", base / "capture", base } )
		fs::remove( p, ec );
	TEST_CHECK_( !fs::exists( base ), "the temp folder removed completely" );
}

TEST_LIST = {
	{ "pg_smbios_decided_bytes_only", pg_smbios_decided_bytes_only },
	{ "pg_smbios_layout_preserved", pg_smbios_layout_preserved },
	{ "pg_identity_map_replay", pg_identity_map_replay },
	{ "pg_filler_untouched", pg_filler_untouched },
	{ "pg_guard_undecided", pg_guard_undecided },
	{ "pg_mac_oui", pg_mac_oui },
	{ "pg_identity_rules", pg_identity_rules },
	{ "pg_git_refusal", pg_git_refusal },
	{ "pg_helpers", pg_helpers },
	{ "pg_type17_names_host_only", pg_type17_names_host_only },
	{ "pg_build_config_roundtrip", pg_build_config_roundtrip },
	{ "pg_uuid_mac_relation", pg_uuid_mac_relation },
	{ "pg_usb_shared_table", pg_usb_shared_table },
	{ "pg_mac_relations", pg_mac_relations },
	{ "pg_v1_guid_node", pg_v1_guid_node },
	{ "pg_edid_rules", pg_edid_rules },
	{ "pg_product_id_groups", pg_product_id_groups },
	{ "pg_os_ids_rules", pg_os_ids_rules },
	{ "pg_os_ids_guard", pg_os_ids_guard },
	{ "pg_fwid14_rules", pg_fwid14_rules },
	{ "pg_chid_check", pg_chid_check },
	{ "pg_new_identity", pg_new_identity },
	{ "pg_swd_guids", pg_swd_guids },
	{ "pg_disk_descriptors", pg_disk_descriptors },
	{ "pg_tool_exclusions", pg_tool_exclusions },
	{ "pg_install_shift", pg_install_shift },
	{ "pg_randomizer_threads", pg_randomizer_threads },
	{ "pg_parallel_builds", pg_parallel_builds },
	{ "pg_concurrent_fresh_shared", pg_concurrent_fresh_shared },
	{ "pg_atomic_writes", pg_atomic_writes },
	{ "pg_v1_guid_time", pg_v1_guid_time },
	{ "pg_field_view", pg_field_view },
	{ "pg_views", pg_views },
	{ NULL, NULL } };
// ── end of the pg_ block (U1511) ─────────────────────────────────────────────────────────────────────────────────────────
