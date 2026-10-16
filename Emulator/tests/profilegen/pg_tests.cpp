// pg_tests.cpp - unit tests of ProfileGenerator (plan 1.J.49, ledger U1511), suite "pg_unit" in test.cmd.
// Every input is a SMALL SYNTHETIC FIXTURE built here (made-up vendor strings, serials, MACs, GUIDs, SIDs, EDIDs); no real
// capture is read. Call sites follow the user's style rule: auto ProfileGen = std::make_unique<ProfileGenerator>( cfg );
// ProfileGen->gen_<what>( struct ) -> struct.
// (a) only the decided SMBIOS bytes change, (b) lengths / structure layout preserved, (c) same seed -> identical output,
// (d) filler fields untouched, (e) MAC OUI preserved, (f) the original profile is refused inside a git work tree; plus the
// guard for undecided fields, the identity rules 4a-4d, rule 2h (host only), the BuildConfig round trip (save -> load ->
// rebuild = byte-identical), the UUID-MAC relation, the shared USB table, the MAC relations (EUI-64), version-1 GUID nodes,
// EDID (checksum, one serial per monitor), ProductId groups, SID / NameSamCompatible / install-instant consistency, volume
// GUID consistency, the fwid 1.4 rules (installation ID, update IDs, disk / HID / GPU / Bluetooth / audio), the CHID check,
// a new identity (another seed: every mapped field differs, every relation still holds) and the software-device GUIDs.
#include "profile_generator.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <memory>
#include <regex>
#include <set>
#include <string>

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

static std::string pg_seed_hex( uint8_t fill )
{
	std::string s;
	for ( int i = 0; i < 32; i++ ) { char b[ 3 ]; snprintf( b, sizeof b, "%02X", unsigned( uint8_t( fill + i * 7 ) ) ); s += b; }
	return s;
}
static BuildConfig pg_cfg( Environment e, uint8_t seed )
{
	BuildConfig c = BuildConfig::defaults( e );
	c.seed = pg_seed_hex( seed );
	c.original_out = "none";
	return c;
}
static std::unique_ptr<ProfileGenerator> pg_gen( Environment e, uint8_t seed ) { return std::make_unique<ProfileGenerator>( pg_cfg( e, seed ) ); }

static std::string pg_str( const Bytes& b, const SmbiosString& s ) { return std::string( b.begin() + s.offset, b.begin() + s.offset + s.length ); }

// (a) only the decided bytes change: every differing byte lies inside a field-map range, every range changed, and the
// ranges are exactly the decided locations of the fixture (host: also the rule-2h type 17 name strings)
static void pg_smbios_decided_bytes_only( void )
{
	for ( bool vm : { true, false } )
	{
		auto ProfileGen = pg_gen( vm ? Environment::vmware_guest : Environment::bare_host, 0x11 );
		Bytes raw = pg_fixture( vm );
		SmbiosGen g = ProfileGen->gen_smbios( { raw } );
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
	for ( bool vm : { true, false } )
	{
		auto ProfileGen = pg_gen( vm ? Environment::vmware_guest : Environment::bare_host, 0x22 );
		Bytes raw = pg_fixture( vm );
		SmbiosGen g = ProfileGen->gen_smbios( { raw } );
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
		SmbiosUuid ru;
		ru.handle = 1;
		std::copy( u0, u0 + 16, ru.bytes.begin() );
		SmbiosUuid su = ProfileGen->gen_system_uuid( ru );
		TEST_CHECK( su.bytes != ru.bytes && ( !vm || ( su.bytes[ 0 ] == 0x56 && su.bytes[ 1 ] == 0x4D ) ) );
	}
}

static void pg_text_fixture( std::vector<TextFile>& files );
static std::vector<TextFile> pg_text_fixture2( const std::string& usb_lines );

// (c) the same seed gives identical output; another seed gives other values (SMBIOS and identity)
static void pg_same_seed_identical( void )
{
	auto run = []( uint8_t seed ) { return pg_gen( Environment::bare_host, seed )->gen_smbios( { pg_fixture( false ) } ).synthetic; };
	TEST_CHECK( run( 0x33 ) == run( 0x33 ) );
	TEST_CHECK( run( 0x33 ) != run( 0x34 ) );
	std::vector<TextFile> files;
	pg_text_fixture( files );
	auto ident = [ & ]( uint8_t seed ) {
		TextIdentityGen t = pg_gen( Environment::vmware_guest, seed )->gen_text_identity( { files } );
		std::string s;
		for ( auto& r : t.identity ) s += r.synthetic + "|";
		for ( auto& p : t.storage_paths ) s += p.synthetic + "|";
		return s;
	};
	TEST_CHECK( ident( 0x33 ) == ident( 0x33 ) );
	TEST_CHECK( ident( 0x33 ) != ident( 0x34 ) );
	Bytes seed( 32, 0x33 );
	SyntheticStream g( seed, Environment::vmware_guest, "x" ), h( seed, Environment::bare_host, "x" );
	std::string a, b;
	for ( int i = 0; i < 16; i++ ) { a += char( g.next_byte() ); b += char( h.next_byte() ); }
	TEST_CHECK( a != b );
}

// (d) filler fields untouched
static void pg_filler_untouched( void )
{
	for ( const char* f : { "Default string", "To Be Filled By O.E.M.", "None", "No Asset Tag", "N/A", "Not Specified", "", "to be filled by o.e.m.", "None  ",
			  "Unknown", "NO DIMM", "00000000", "9876543210", "9876543210  " } )
		TEST_CHECK_( ProfileGenerator::check_filler( f ), "\"%s\" is filler", f );
	for ( const char* f : { "AT-0042", "0", "Default", "N/A1", "00000017", "000000000", "98765432100" } ) TEST_CHECK_( !ProfileGenerator::check_filler( f ), "\"%s\" is not filler", f );
	std::vector<uint8_t> t17 = pg_tail( 0x28, { { 0x10, 1 }, { 0x11, 2 }, { 0x17, 3 }, { 0x18, 4 }, { 0x19, 5 }, { 0x1A, 3 } } );
	Bytes raw = pg_build( { { 2, 0x0002, pg_tail( 0x0F, { { 0x04, 1 }, { 0x07, 2 }, { 0x08, 3 } } ), { "Fixture Maker Inc.", "Default string", "To Be Filled By O.E.M." } },
		{ 17, 0x0011, t17, { "DIMM 0", "BANK 5", "Not Specified", "None", "No Asset Tag" } }, { 127, 0xFEFF, {}, {} } } );
	auto ProfileGen = pg_gen( Environment::vmware_guest, 0x44 );
	SmbiosGen g = ProfileGen->gen_smbios( { raw } );
	TEST_CHECK( g.synthetic == raw );
	TEST_CHECK( g.fields.empty() && g.needles.empty() && g.binary_needles.empty() );
	Bytes fx = pg_fixture( true );
	Bytes o2 = ProfileGen->gen_smbios( { fx } ).synthetic;
	SmbiosTable t = parse_smbios( fx );
	for ( const SmbiosStructure& s : t.structures )
		for ( const SmbiosString& x : s.strings )
			if ( ProfileGenerator::check_filler( pg_str( fx, x ) ) ) TEST_CHECK_( pg_str( o2, x ) == pg_str( fx, x ), "filler string of handle 0x%04X unchanged", s.handle );
}

// the guard: a real value in an undecided identifier field (here the chassis serial) stops the run
static void pg_guard_undecided( void )
{
	auto ProfileGen = pg_gen( Environment::vmware_guest, 0x55 );
	bool threw = false;
	try { ProfileGen->gen_smbios( { pg_fixture( true, "CH-998877" ) } ); }
	catch ( const std::exception& e ) { threw = std::string( e.what() ).find( "undecided identifier field" ) != std::string::npos; }
	TEST_CHECK( threw );
	Bytes bad = pg_fixture( true );
	bad[ 4 ]++;
	threw = false;
	try { parse_smbios( bad ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
}

static void pg_text_fixture( std::vector<TextFile>& files )
{
	auto tf = []( const char* name, const std::string& s ) { return TextFile { name, Bytes( s.begin(), s.end() ) }; };
	files = {
		tf( "capture_info.txt", "tool.version            = fixture\n"
								"os.computer_name        = NetBIOS \"DESKTOP-FIXTURE\", DNS host \"DESKTOP-FIXTURE\", DNS domain \"\"\n"
								"run.output_folder       = X:\\out\\DESKTOP-FIXTURE_20260101_000000\n"
								"not.a.match             = DESKTOP-FIXTUREX\n" ),
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
							 "  instance SCSI\\DISK&VEN_FIXTURE&PROD_DISK_1\\5&1A2B3C4D&0&000000\n" ) };
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
	auto ProfileGen = pg_gen( Environment::bare_host, 0x66 );
	for ( const char* mac : { "00:50:56:2E:9B:17", "00:04:0F:12:34:56", "FC:34:97:AB:CD:EF", "00:0C:29:00:00:00" } )
	{
		MacAddress o = ProfileGen->gen_mac( { mac, "Fixture", std::string( "T" ) + mac } );
		TEST_CHECK_( o.text.size() == 17 && o.text.substr( 0, 9 ) == std::string( mac ).substr( 0, 9 ), "OUI of %s kept", mac );
		TEST_CHECK_( o.text.substr( 9 ) != std::string( mac ).substr( 9 ), "last 3 bytes of %s synthetic", mac );
		TEST_CHECK( std::regex_match( o.text, std::regex( "([0-9A-F]{2}:){5}[0-9A-F]{2}" ) ) );
	}
	std::vector<TextFile> files;
	pg_text_fixture( files );
	TextIdentityGen t = pg_gen( Environment::vmware_guest, 0x66 )->gen_text_identity( { files } );
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
	std::vector<TextFile> files;
	pg_text_fixture( files );
	TextIdentityGen t = pg_gen( Environment::bare_host, 0x77 )->gen_text_identity( { files } );
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
		TEST_CHECK_( _stricmp( got.c_str(), want.c_str() ) == 0 || ( r->rule == "3" && got.size() == 17 ), "occurrence of %s at %s %llu:%llu", f.id.c_str(), f.area.c_str(),
			(unsigned long long)f.line, (unsigned long long)f.offset );
	}
}

// (f) the original profile is refused inside a git work tree (".git" directory or worktree file in an ancestor); nothing
// is created there. A synthetic profile may go anywhere; stale files in an output folder are refused.
static void pg_git_refusal( void )
{
	fs::path base = fs::temp_directory_path() / ( "pg_unit_" + std::to_string( GetCurrentProcessId() ) );
	std::error_code ec;
	fs::remove_all( base, ec );
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
	fs::remove_all( base, ec );
}

// check_diff_ranges and the single-value generators on their own
static void pg_helpers( void )
{
	Bytes a = { 1, 2, 3, 4, 5, 6 }, b = { 1, 9, 9, 4, 5, 0 };
	auto r = ProfileGenerator::check_diff_ranges( a, b );
	TEST_CHECK( r.size() == 2 && r[ 0 ].first == 1 && r[ 0 ].last == 3 && r[ 1 ].first == 5 && r[ 1 ].last == 6 );
	auto ProfileGen = pg_gen( Environment::bare_host, 0x88 );
	TextValue o = ProfileGen->gen_class_string( { "07X1234_A99b-z", "classes" } );
	TEST_CHECK( std::regex_match( o.text, std::regex( "[0-9]{2}[A-Z][0-9]{4}_[A-Z][0-9]{2}[a-z]-[a-z]" ) ) );
	bool threw = false;
	try { ProfileGen->gen_class_string( { "--__", "x" } ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
	for ( int i = 0; i < 50; i++ ) TEST_CHECK( ProfileGen->gen_class_string( { "7", "one/" + std::to_string( i ) } ).text != "7" );
	TEST_CHECK( std::regex_match( ProfileGen->gen_hex_token( { "1a2b3c4d", "hex" } ).text, std::regex( "[0-9a-f]{8}" ) ) );
	ComputerName cn = ProfileGen->gen_computer_name( { "DESKTOP-ABCDEFG", "cn" } );
	TEST_CHECK( std::regex_match( cn.text, std::regex( "DESKTOP-[A-Z0-9]{7}" ) ) && cn.text != "DESKTOP-ABCDEFG" );
	VolumeId vs = ProfileGen->gen_volume_serial( { "1A2B-3C4D", "vs" } );
	TEST_CHECK( std::regex_match( vs.serial, std::regex( "[0-9A-F]{4}-[0-9A-F]{4}" ) ) && vs.serial != "1A2B-3C4D" );
	BuildConfig c = BuildConfig::defaults( Environment::bare_host );	// a seedless generator refuses to generate
	auto Seedless = std::make_unique<ProfileGenerator>( c );
	threw = false;
	try { Seedless->gen_class_string( { "AB12", "x" } ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
}

// rule 2h: host only; the guest keeps every type 17 string byte-identical
static void pg_type17_names_host_only( void )
{
	Bytes raw = pg_fixture( false );
	SmbiosTable t = parse_smbios( raw );
	Bytes host = pg_gen( Environment::bare_host, 0x99 )->gen_smbios( { raw } ).synthetic;
	const Bytes graw = pg_fixture( true );
	Bytes guest = pg_gen( Environment::vmware_guest, 0x99 )->gen_smbios( { graw } ).synthetic;
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

// BuildConfig: save -> load gives the same configuration (seed included), and a rebuild from the loaded (and from the
// saved-next-to-the-output) configuration gives byte-identical synthetic files; the saved copy holds no fixture identifier
static void pg_build_config_roundtrip( void )
{
	for ( bool vm : { true, false } )
	{
		Environment e = vm ? Environment::vmware_guest : Environment::bare_host;
		BuildConfig c1 = pg_cfg( e, 0xA1 );
		BuildConfig c2 = BuildConfig::load( c1.save() );
		TEST_CHECK( c2 == c1 );
		TEST_CHECK( c2.save() == c1.save() );
		CaptureInputs in = pg_capture( vm );
		auto ProfileGen = std::make_unique<ProfileGenerator>( c1 );
		GenResult r1 = ProfileGen->gen_build( in );
		TEST_ASSERT_( r1.ok, "build: %s", r1.error.c_str() );
		GenResult r2 = std::make_unique<ProfileGenerator>( c2 )->gen_build( in );
		TEST_ASSERT( r2.ok );
		TEST_CHECK( r1.synthetic == r2.synthetic );
		const Bytes& saved = r1.synthetic.at( BuildConfig::k_file );
		BuildConfig c3 = BuildConfig::load( std::string( saved.begin(), saved.end() ) );
		TEST_CHECK( c3 == c1 && c3.seed == c1.seed );
		GenResult r3 = std::make_unique<ProfileGenerator>( c3 )->gen_build( in );
		TEST_ASSERT( r3.ok );
		TEST_CHECK( r3.synthetic == r1.synthetic );
		for ( const char* f : { "smbios.bin", "acpi\\acpi_TEST.bin", "identity.tsv", "fieldmap.tsv", "build_config.txt", "manifest.txt" } )
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
	for ( const char* bad : { "format = 2\n", "environment = mars\n", "bogus = 1\n", "out = a\nout = b\n", "seed = 12\n" } )
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
		tf( "certificates.txt", "subject: \"CN=Fixture Test Certificate\"\nnode 3CA9F4102030 here is not searched\n" ) };
}
static std::string pg_usb( const char* dev, const char* serial ) { return std::string( "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\USB\\" ) + dev + "\\" + serial + "\n"; }

// the UUID-MAC relation: the real UUID's last 6 bytes are an adapter's MAC -> the synthetic UUID's last 6 bytes are that
// adapter's synthetic MAC (OUI kept), the field map names the adapter; without a relation the UUID is independent
static void pg_uuid_mac_relation( void )
{
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
			GenResult r = pg_gen( vm ? Environment::vmware_guest : Environment::bare_host, 0xB1 )->gen_build( in );
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
	std::string host = pg_usb( "VID_1111&PID_2222", "4142433132333435" ) + pg_usb( "Vid_0E0F&Pid_0001", "4142433132333435" ) + pg_usb( "VID_3333&PID_4444", "Q9ZK21x" ) +
					   pg_usb( "VID_5555&PID_6666", "000000000000" ) + pg_usb( "VID_7777&PID_8888", "MSFT3000000000" ) + pg_usb( "VID_9999&PID_AAAA", "0a1b2c3d4e5f60718293" );
	std::string guest = pg_usb( "VID_BBBB&PID_CCCC", "7Y7Y" ) + pg_usb( "VID_1111&PID_2222", "4142433132333435" );
	auto fh = pg_text_fixture2( host ), fg = pg_text_fixture2( guest );
	std::vector<std::string> table = ProfileGenerator::parse_usb_serial_table( { fh[ 2 ].data, fg[ 2 ].data } );
	TEST_CHECK_( table.size() == 4, "table: 3 host serials + 1 guest-only serial (placeholders excluded), got %zu", table.size() );
	TextIdentityGen th = pg_gen( Environment::bare_host, 0xC1 )->gen_text_identity( { fh, table } );
	TextIdentityGen tg = pg_gen( Environment::vmware_guest, 0xC1 )->gen_text_identity( { fg, table } );
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
	TextIdentityGen solo = pg_gen( Environment::vmware_guest, 0xC1 )->gen_text_identity( { fg } );	// own table only: another index
	const IdentityEntry* c = pg_find( solo.identity, "usb_serial", "4142433132333435" );
	TEST_ASSERT( c != nullptr );
	TEST_CHECK( c->synthetic != b->synthetic );
	UsbSerial us = pg_gen( Environment::bare_host, 0xC1 )->gen_usb_serial( { "4142433132333435", 0, "x" } );
	TEST_CHECK( us.text == a->synthetic );	// gen_usb_serial with the shared index = the identity's value
}

// rule 3 relations: one synthetic MAC per radio / NIC, dependents re-derived (+1, locally administered variant, +4), the
// EUI-64 instance ID re-derived, VMnet exact, Hyper-V OUI kept; relations recorded by adapter description
static void pg_mac_relations( void )
{
	auto files = pg_text_fixture2( "" );
	TextIdentityGen t = pg_gen( Environment::bare_host, 0xC2 )->gen_text_identity( { files } );
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
	MacFamily sf = pg_gen( Environment::bare_host, 0xC2 )->gen_mac_family( fam );
	TEST_CHECK( sf.members[ 1 ].parent == 0 && sf.members[ 1 ].delta == 1 && sf.members[ 2 ].parent == 0 && sf.members[ 2 ].locally_administered );
	Eui64Id ei = pg_gen( Environment::bare_host, 0xC2 )->gen_eui64_instance_id( { "a0b1c2ffff01020300", true }, { "A0:B1:C2:0A:0B:0C", "", "x" } );
	TEST_CHECK( ei.text == "a0b1c2ffff0a0b0c00" );
}

// rule 4c version-1 GUIDs: node = the synthetic MAC of its adapter (also when the GUID only occurs elsewhere, e.g. a WPD
// instance), Windows' no-NIC node kept, timestamp / clock synthetic, version 1 and variant kept, one GUID = one value
static void pg_v1_guid_node( void )
{
	auto files = pg_text_fixture2( "" );
	TextIdentityGen t = pg_gen( Environment::bare_host, 0xC3 )->gen_text_identity( { files } );
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
	GuidValue mbr = pg_gen( Environment::bare_host, 0xC3 )->gen_guid( { "{3fa81c55-0000-0000-0000-100000000000}", "mbr" } );
	TEST_CHECK( mbr.text.substr( 9 ) == "-0000-0000-0000-100000000000}" && mbr.text.substr( 1, 8 ) != "3fa81c55" );
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
	EdidInput in { { { "edid_0.bin", pg_edid( 0x2701, 0x12345678, "FIXSER12345", true ) }, { "edid_1.bin", pg_edid( 0x2701, 0x12345678, "FIXSER12345", false ) },
		{ "edid_2.bin", pg_edid( 0x4012, 0x32314241, nullptr, false ) } } };
	auto ProfileGen = pg_gen( Environment::bare_host, 0xD1 );
	EdidGen g = ProfileGen->gen_edid( in );
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
	EdidSerial es = ProfileGen->gen_edid_serial( { 1804, "n" } );
	TEST_CHECK( es.value >= 1000 && es.value <= 9999 && es.value != 1804 );
	EdidBlock bad = in.blocks[ 1 ];
	bad.bytes[ 127 ] ^= 0x55;
	EdidBlock fixed = ProfileGen->gen_edid_checksum( bad );
	TEST_CHECK( fixed.bytes == in.blocks[ 1 ].bytes );
}

// rule 7a: ProductId groups
static void pg_product_id_groups( void )
{
	auto ProfileGen = pg_gen( Environment::bare_host, 0xD2 );
	ProductId p = ProfileGen->gen_product_id( { "00330-12345-67890-AA123", "pid" } );
	TEST_CHECK( p.text.size() == 23 && p.text.substr( 0, 6 ) == "00330-" && p.text.substr( 17 ) == "-AA123" && p.text != "00330-12345-67890-AA123" );
	TEST_CHECK( std::regex_match( p.text, std::regex( "00330-[0-9]{5}-[0-9]{5}-AA123" ) ) );
	bool threw = false;
	try { ProfileGen->gen_product_id( { "12345", "x" } ); } catch ( const std::exception& ) { threw = true; }
	TEST_CHECK( threw );
}

// the fwid 1.3 os_ids.txt fixture (made-up values) and the fwid 1.4 files
static std::string pg_os_ids( const std::string& extra = "" )
{
	return "## registry identifiers\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Cryptography \"MachineGuid\" REG_SZ, 74 bytes [IDENTIFIER - keep local]: \"1b2c3d4e-5f60-4718-9a0b-c1d2e3f4a5b6\"\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"ProductId\" REG_SZ, 48 bytes [IDENTIFIER - keep local]: \"00330-12345-67890-AA123\"\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"InstallDate\" REG_DWORD, 4 bytes [machine-specific]: 0x62A0B1C2 (1654698434)\n"
		   "    = 2022-06-08T14:27:14Z (seconds since 1970-01-01 UTC)\n"
		   "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion \"InstallTime\" REG_QWORD, 8 bytes [machine-specific]: 0x01D87B43D901E387 (132991720341234567)\n"
		   "    = 2022-06-08T14:27:14Z (FILETIME)\n"
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
static std::vector<TextFile> pg_text_fixture3()
{
	auto tf = []( const char* name, const std::string& s ) { return TextFile { name, Bytes( s.begin(), s.end() ) }; };
	auto files = pg_text_fixture2( pg_usb( "VID_1111&PID_2222", "4142433132333435" ) );
	files.push_back( tf( "os_ids.txt", pg_os_ids() ) );
	files.push_back( tf( "license.txt", "Name: \"Windows(R), Professional edition\"\nDescription: \"Windows(R) Operating System, RETAIL channel\"\nLicenseStatus: 1 (licensed)\n"
										"ProductKeyChannel: \"Retail\"\nOfflineInstallationId [IDENTIFIER - keep local]: \"123456-234567-345678-456789-567890-678901-789012-890123-901234\"\n" ) );
	files.push_back( tf( "update_ids.txt", "SusClientId [IDENTIFIER - keep local]: \"{1234abcd-5678-4ef0-9abc-def012345678}\"\n"
										   "SusClientIdValidation [IDENTIFIER - keep local]: 06 02 00 00 1A 2B 3C 4D\n"
										   "DiagTrack DeviceId [IDENTIFIER - keep local]: \"f0e1d2c3-b4a5-4968-8776-655443322110\"\n" ) );
	files.push_back( tf( "disks.txt", "PhysicalDrive0 vendor \"FIXDISK\" product \"FX SSD 1TB\"\nPhysicalDrive0 serial [IDENTIFIER - keep local]: \"S5GXNF0R123456K\"\n" ) );
	files.push_back( tf( "hid.txt", "HID\\VID_1111&PID_2222 SerialNumber [IDENTIFIER - keep local]: \"4142433132333435\"\nHID\\VID_1111&PID_2222 Manufacturer [IDENTIFIER - keep local]: \"Fixture Corp\"\n" ) );
	files.push_back( tf( "gpu.txt", "AdapterString [IDENTIFIER - keep local]: \"Fixture GPU\"\nDeviceUUID [IDENTIFIER - keep local]: \"{a1a2a3a4-b4a5-4968-8776-655443322110}\"\n"
									"BoardSerial [IDENTIFIER - keep local]: \"GPU12345XY\"\n" ) );
	files.push_back( tf( "bluetooth.txt", "paired device address [IDENTIFIER - keep local]: 70:1A:B8:11:22:33\npaired device name [IDENTIFIER - keep local]: \"Fixture Headset\"\n"
										  "local radio address [IDENTIFIER - keep local]: 3C:A9:F4:10:20:34\n" ) );
	files.push_back( tf( "audio.txt", "endpoint [IDENTIFIER - keep local]: \"{0.0.0.00000000}.{a1b2c3d4-e5f6-4789-8abc-def012345678}\"\nendpoint name: \"Fixture Speakers\"\n" ) );
	files.push_back( tf( "wifi.txt", "profiles: 3\n" ) );
	files.push_back( tf( "chid.txt", "HardwareID-0 [IDENTIFIER]: {01234567-89ab-cdef-0123-456789abcdef}\n" ) );
	return files;
}

// rule 7: SID consistency, NameSamCompatible, one user name everywhere, the install instant; volume GUID consistency
static void pg_os_ids_rules( void )
{
	auto files = pg_text_fixture3();
	TextIdentityGen t = pg_gen( Environment::bare_host, 0xD3 )->gen_text_identity( { files } );
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
	const IdentityEntry* d = pg_find( t.identity, "install_date", "1654698434" );
	const IdentityEntry* it = pg_find( t.identity, "install_time", "132991720341234567" );
	TEST_ASSERT( d != nullptr && it != nullptr );
	long long sd = std::stoll( d->synthetic );
	unsigned long long st = std::stoull( it->synthetic );
	InstallWindow w = ProfileGenerator::parse_install_window( 19045, "2026-01-01T00:00:00.000Z" );
	TEST_CHECK_( sd > w.lo && sd < w.hi, "the install instant lies strictly between the build release and the capture" );
	TEST_CHECK_( ( st / 10000000ull ) - 11644473600ull == (unsigned long long)sd, "InstallTime = the same instant as InstallDate" );
	TEST_CHECK( d->occurrences >= 3 );	// hex, decimal, ISO
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
	SidParts sp = pg_gen( Environment::bare_host, 0xD3 )->gen_sid( { "S-1-5-21-1111111111-222222222-3333333333", "sid" } );
	TEST_CHECK( sp.prefix.rfind( "S-1-5-21-", 0 ) == 0 && sp.prefix.size() == 40 );
	InstallTime itr;
	itr.os_build = 19045;
	itr.capture_utc = "2026-01-01T00:00:00Z";
	InstallTime its = pg_gen( Environment::bare_host, 0xD3 )->gen_install_time( itr );
	TEST_CHECK( its.unix_seconds > w.lo && its.unix_seconds < w.hi && its.filetime / 10000000ull - 11644473600ull == uint64_t( its.unix_seconds ) );
}

// a marked identifier line no rule consumes stops the run; so does a non-empty RegisteredOrganization or a marked line in
// a file whose content is exact (wifi.txt)
static void pg_os_ids_guard( void )
{
	auto run = [ & ]( const char* name, const std::string& body ) {
		auto files = pg_text_fixture2( "" );
		files.push_back( { name, Bytes( body.begin(), body.end() ) } );
		try { pg_gen( Environment::bare_host, 0xD4 )->gen_text_identity( { files } ); }
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
	auto ProfileGen = pg_gen( Environment::bare_host, 0xD5 );
	WinInstallationId wi;
	wi.text = "123456-234567-345678-456789-567890-678901-789012-890123-901234";
	wi.label = "iid";
	wi.license.license_status = "1";
	wi.license.product_key_channel = "Retail";
	WinInstallationId so = ProfileGen->gen_win_installation_id( wi );
	TEST_CHECK( so.text.size() == wi.text.size() && so.text != wi.text && !so.check_digits_modelled && so.rule == "8a" );
	TEST_CHECK( std::regex_match( so.text, std::regex( "([0-9]{6}-){8}[0-9]{6}" ) ) && so.group_lengths == std::vector<size_t>( 9, 6 ) );
	TEST_CHECK( so.license.product_key_channel == "Retail" && so.license.license_status == "1" );
	auto files = pg_text_fixture3();
	TextIdentityGen t = ProfileGen->gen_text_identity( { files, ProfileGenerator::parse_usb_serial_table( { files[ 2 ].data } ) } );
	const IdentityEntry* ii = pg_find( t.identity, "win_installation_id", wi.text );
	TEST_ASSERT( ii != nullptr );
	TEST_CHECK( ii->rule == "8a" && std::regex_match( ii->synthetic, std::regex( "([0-9]{6}-){8}[0-9]{6}" ) ) );
	TEST_ASSERT( t.installation_ids.size() == 2 );
	TEST_CHECK( t.installation_ids[ 0 ].license.product_key_channel == "Retail" && t.installation_ids[ 0 ].license.name == "Windows(R), Professional edition" );
	const IdentityEntry* sus = pg_find( t.identity, "sus_client_id", "{1234abcd-5678-4ef0-9abc-def012345678}" );
	TEST_ASSERT( sus != nullptr );
	TEST_CHECK( sus->synthetic.front() == '{' && sus->synthetic[ 15 ] == '4' && sus->synthetic != sus->original && std::none_of( sus->synthetic.begin(), sus->synthetic.end(), []( char c ) { return c >= 'A' && c <= 'F'; } ) );
	const IdentityEntry* val = pg_find( t.identity, "sus_client_id_validation", "06 02 00 00 1A 2B 3C 4D" );
	TEST_ASSERT( val != nullptr );
	TEST_CHECK( std::regex_match( val->synthetic, std::regex( "([0-9A-F]{2} ){7}[0-9A-F]{2}" ) ) && val->synthetic != val->original );
	TEST_CHECK( pg_find( t.identity, "telemetry_id", "f0e1d2c3-b4a5-4968-8776-655443322110" ) != nullptr );
	const IdentityEntry* ds = pg_find( t.identity, "disk_serial", "S5GXNF0R123456K" );
	TEST_ASSERT( ds != nullptr );
	TEST_CHECK( std::regex_match( ds->synthetic, std::regex( "[A-Z][0-9][A-Z]{4}[0-9][A-Z][0-9]{6}[A-Z]" ) ) );
	const IdentityEntry* usb = pg_find( t.identity, "usb_serial", "4142433132333435" );
	TEST_ASSERT( usb != nullptr );
	TEST_CHECK( !pg_find( t.identity, "hid_serial", "4142433132333435" ) );
	bool in_hid = false;
	for ( const FieldMapEntry& f : t.fields ) in_hid |= f.id == usb->id && f.area == "hid.txt";
	TEST_CHECK_( in_hid, "the HID serial = the USB serial: one identity, occurrences in hid.txt too" );
	TEST_CHECK( pg_find( t.identity, "gpu_guid", "{a1a2a3a4-b4a5-4968-8776-655443322110}" ) != nullptr && pg_find( t.identity, "gpu_unique", "GPU12345XY" ) != nullptr );
	const IdentityEntry* bt = pg_find( t.identity, "bt_device", "70:1A:B8:11:22:33" );
	TEST_ASSERT( bt != nullptr );
	TEST_CHECK( bt->synthetic.substr( 0, 9 ) == "70:1A:B8:" && bt->synthetic != bt->original );
	TEST_CHECK_( !pg_find( t.identity, "bt_device", "3C:A9:F4:10:20:34" ), "the local radio address is its adapter MAC identity (Bluetooth PAN = Wi-Fi + 4)" );
	const IdentityEntry* pan = pg_find( t.identity, "mac", "3C:A9:F4:10:20:34" );
	TEST_ASSERT( pan != nullptr );
	bool in_bt = false;
	for ( const FieldMapEntry& f : t.fields ) in_bt |= f.id == pan->id && f.area == "bluetooth.txt";
	TEST_CHECK( in_bt );
	TEST_CHECK( pg_find( t.identity, "audio_endpoint", "{A1B2C3D4-E5F6-4789-8ABC-DEF012345678}" ) != nullptr );
	TEST_CHECK( !pg_find( t.identity, "gpu_unique", "Fixture GPU" ) && !pg_find( t.identity, "bt_device", "Fixture Headset" ) );	// names exact
}

// the CHID check: unchanged fields pass; a changed CHID input (type 1 manufacturer) is counted
static void pg_chid_check( void )
{
	Bytes raw = pg_fixture( false );
	Bytes syn = pg_gen( Environment::bare_host, 0xD6 )->gen_smbios( { raw } ).synthetic;
	ProfileGenerator::ChidCheck c = ProfileGenerator::check_chid_fields( raw, syn );
	TEST_CHECK( c.checked >= 6 && c.changed == 0 );
	Bytes bad = syn;
	SmbiosTable t = parse_smbios( bad );
	bad[ t.structures[ 0 ].strings[ 0 ].offset ] ^= 0x01;	// type 1 Manufacturer
	TEST_CHECK( ProfileGenerator::check_chid_fields( raw, bad ).changed == 1 );
}

// a new identity: another seed gives a different value for EVERY mapped field, while every consistency relation holds
static void pg_new_identity( void )
{
	CaptureInputs in = pg_capture( false, pg_text_fixture3() );
	SmbiosTable t = parse_smbios( in.rsmb );
	const uint8_t wired[ 6 ] = { 0xA0, 0xB1, 0xC2, 0x01, 0x02, 0x03 };
	memcpy( &in.rsmb[ t.structures[ 0 ].offset + 8 + 10 ], wired, 6 );
	in.edid = { { "edid_0.bin", pg_edid( 0x2701, 0x12345678, "FIXSER12345", false ) }, { "edid_1.bin", pg_edid( 0x2701, 0x12345678, "FIXSER12345", false ) } };
	auto A = std::make_unique<ProfileGenerator>( pg_cfg( Environment::bare_host, 0xE1 ) );
	auto ProfileGen = std::make_unique<ProfileGenerator>( pg_cfg( Environment::bare_host, 0xE1 ) );
	Seed s = ProfileGen->gen_new_seed();
	TEST_CHECK( s.hex.size() == 64 && ProfileGen->config().seed == s.hex && s.hex != A->config().seed );
	GenResult ra = A->gen_build( in ), rb = ProfileGen->gen_build( in );
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
		const IdentityEntry* d = pg_find( r->identity.identity, "install_date", "1654698434" );
		const IdentityEntry* it = pg_find( r->identity.identity, "install_time", "132991720341234567" );
		TEST_CHECK( std::stoull( it->synthetic ) / 10000000ull - 11644473600ull == std::stoull( d->synthetic ) );
		TEST_CHECK( r->synthetic.at( "edid\\edid_0.bin" ) == r->synthetic.at( "edid\\edid_1.bin" ) );	// the same monitor on two ports
	}
	GenResult ra2 = std::make_unique<ProfileGenerator>( pg_cfg( Environment::bare_host, 0xE1 ) )->gen_build( in );
	TEST_CHECK( ra2.ok && ra2.synthetic == ra.synthetic );	// the same seed again: byte-identical
}

// rule 4c on software-device instance IDs: every GUID inside an Enum\SWD instance (braced or not) is synthetic, one mapping
// everywhere (the MMDEVAPI endpoint GUID = the audio.txt endpoint); the all-zero GUID and the PnP hashes stay exact
static void pg_swd_guids( void )
{
	std::string swd = "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\MMDEVAPI\\{0.0.0.00000000}.{a1b2c3d4-e5f6-4789-8abc-def012345678}\n"
					  "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\DRIVERENUM\\{12ab34cd-56ef-4a1b-9c2d-3e4f5a6b7c8d}#Fixture_component_123&1&2c3d4e5f&0\n"
					  "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\PRINTENUM\\{ABCDEF12-3456-4789-ABCD-EF0123456789}\n"
					  "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\FIXENUM\\FIXTURE_9f8e7d6c-5b4a-4321-8fed-cba987654321&0&3e2f1a0b&0\n"
					  "## HKLM\\SYSTEM\\CurrentControlSet\\Enum\\SWD\\NULLENUM\\{00000000-0000-0000-0000-000000000000}\n";
	auto files = pg_text_fixture2( swd );
	files.push_back( { "audio.txt", [] { std::string a = "endpoint [IDENTIFIER - keep local]: \"{0.0.0.00000000}.{a1b2c3d4-e5f6-4789-8abc-def012345678}\"\n"; return Bytes( a.begin(), a.end() ); }() } );
	TextIdentityGen t = pg_gen( Environment::bare_host, 0xD7 )->gen_text_identity( { files } );
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

TEST_LIST = {
	{ "pg_smbios_decided_bytes_only", pg_smbios_decided_bytes_only },
	{ "pg_smbios_layout_preserved", pg_smbios_layout_preserved },
	{ "pg_same_seed_identical", pg_same_seed_identical },
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
	{ NULL, NULL } };
// ── end of the pg_ block (U1511) ─────────────────────────────────────────────────────────────────────────────────────────
