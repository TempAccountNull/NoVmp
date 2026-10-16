// profile_generator.hpp - the platform identity profiles of the emulator (plan 1.J.49, ledger U1510-U1512).
//
// ProfileGenerator BUILDS both platform profiles the emulator presents to a sample (--guest / --vm and --host), comparable
// to per-CPU definition files: packer HWID hashes and anti-VM checks must see ONE consistent real-looking machine, so the
// internal relations of the capture are kept (UUID tail = NIC MAC, Wi-Fi Direct = Wi-Fi + 1, version-1 GUID nodes, one USB
// serial per physical device in both profiles, ...) while every real identifier is replaced. From ONE clean fwid capture
// (read-only, outside the repository) it writes two profiles of the same layout:
//   SYNTHETIC  Emulator\data\platform\<env>\ (for the repository): the raw blobs with only the decided identifier fields
//              replaced by synthetic values ("synthetic, not host data"), everything else byte-exact;
//   ORIGINAL   <capture root>\profiles_original\<env>\ (LOCAL ONLY): the same files with the real values. Never written into
//              a git work tree (check_git_work_tree / save_profile refuse it).
// Environments (one pinned capture each, never mixed; BuildConfig::pinned):
//   --guest / --vm  vmware_guest: the clean VMware guest WITHOUT the VmLoader driver (Run 3, fwid 1.2). Runs 1 / 2
//                   (driver-modified) are never used.
//   --host          bare_host: the clean bare host, fwid 1.3 capture (its firmware tables are byte-identical to the fwid 1.2
//                   host capture; it adds edid / os_ids).
// Files of a profile: smbios.bin (RawSMBIOSData as GetSystemFirmwareTable('RSMB') returns it), acpi\<name>.bin (every
// acpi_*.bin / hwacpi_*.bin, byte-exact, never modified), edid\edid_<n>.bin (fwid >= 1.3, rule 6), identity.tsv (the
// identifiers of the fwid text outputs), fieldmap.tsv (every replaced field as LOCATION + LENGTH + RULE, never a value;
// identical in both profiles), build_config.txt (the BuildConfig incl. the seed: a build is reproducible from it; a later
// ImGui front end loads / saves the same file), manifest.txt (SHA-256 of every file, provenance, tool version).
// Code style (user rule 2026-10-16): every generation step is a member gen_<what> taking and returning named structs, used
// as ProfileGen->gen_<what>( ... ); parsers / loaders / savers / validators are parse_ / load_ / save_ / check_. The class has
// no UI and no console I/O: it returns a GenResult with a structured BuildLog that the caller prints (pg_main.cpp).
//
// FIELD RULES (user decisions 2026-10-16; everything not listed is copied byte-exact). Locations are those of the clean
// captures: smbios.bin offsets are absolute byte offsets in smbios.bin (incl. the 8-byte RawSMBIOSData header); "formatted
// offset" = inside the structure's formatted area; SMBIOS layout as in System Informer's phnt smbios.h (DMTF DSP0134 is not
// on disk). No values are written here.
//
//   Rule 1   SMBIOS type 1 (System Information) UUID, formatted offset 0x08, 16 bytes (gen_system_uuid).
//            vmware_guest (SMBIOS 2.7, 189 structures, smbios.bin 14082 bytes): type 1 handle 0x0001 at 0x005E, length
//              0x1B -> UUID 0x0066..0x0075. VMware layout: bytes 0-1 (0x0066..0x0067) = VMware's fixed "VM" prefix 56h 4Dh,
//              kept; bytes 2-15 synthetic. No UUID-MAC relation in the guest capture.
//            bare_host (SMBIOS 3.6, 105 structures, smbios.bin 5211 bytes): type 1 handle 0x0001 at 0x005B, length 0x1B ->
//              UUID 0x0063..0x0072. RFC 4122 layout: the version nibble under both byte orders (byte 6 bits 7:4, byte 7 bits
//              7:4) and the variant bits of byte 8 kept exactly as in the real UUID; the rest synthetic. UUID-MAC relation:
//              bytes 10-15 (0x006D..0x0072) are the onboard wired NIC's MAC in the capture -> the synthetic UUID's bytes
//              10-15 = that NIC's synthetic MAC (detected generically: the UUID tail against every adapter MAC).
//   Rule 2   real serial numbers / asset tags (gen_smbios_string): same length, same character class per position (digit ->
//            digit, A-Z -> A-Z, a-z -> a-z, other characters kept). Filler stays: "Default string", "To Be Filled By O.E.M.",
//            "None", "No Asset Tag", "N/A", "Not Specified", "Unknown", "NO DIMM", "Empty", "Not Available", "Not Installed",
//            "Undefined", the placeholders "00000000" / "9876543210" (case-insensitive, trailing blanks ignored), empty or no
//            string. Fields: type 2 SerialNumber (0x07), type 17 SerialNumber (0x18), type 17 AssetTag (0x19).
//            vmware_guest: type 17 handle 0x0031 (at 0x0980, length 0x28) SerialNumber = string 2 at 0x09BB, 8 bytes (VMware's
//              sequence-style module serial: synthetic, NOT filler - user decision 2026-10-16); type 2
//              handle 0x0002 SerialNumber is filler; the other 127 type 17 structures (handles 0x0032..0x00B0) have no
//              serial / asset-tag string.
//            bare_host: type 2 handle 0x0002 (at 0x00D3, length 0x0F) SerialNumber = string 4 at 0x012A, 18 bytes (AssetTag,
//              string 5, filler). Type 17 handles 0x003C (at 0x0824) / 0x003E (at 0x094E): SerialNumber (string 4 at 0x08A2 /
//              0x09CC, 8 bytes) and AssetTag (string 5 at 0x08AB / 0x09D5, 10 bytes) hold placeholder filler, exact.
//   Rule 2h  bare_host ONLY: type 17 DeviceLocator (0x10), BankLocator (0x11), Manufacturer (0x17), PartNumber (0x1A), rule-2
//            classes, filler kept. Handles 0x003B (at 0x07A2) / 0x003D (at 0x08CC): DeviceLocator string 1 at 0x07FE / 0x0928
//            (18 bytes), BankLocator string 2 at 0x0811 / 0x093B (6 bytes); handles 0x003C / 0x003E: DeviceLocator string 1 at
//            0x0880 / 0x09AA (18), BankLocator string 2 at 0x0893 / 0x09BD (6), Manufacturer string 3 at 0x089A / 0x09C4 (7),
//            PartNumber string 6 at 0x08B6 / 0x09E0 (20; trailing blanks kept). vmware_guest: never applied, every type 17
//            string is VMware's own value.
//   Rule 2o  an UNREFERENCED string of a type 2 / 17 structure equal to a rule-2 / 2h real value gets the same synthetic value.
//            No hit in either capture (the host's empty-slot orphan strings, handles 0x003B / 0x003D string 3 at 0x0818 /
//            0x0942, equal the placeholder asset tag and stay).
//   Guard    type 1 SerialNumber 0x07 / SKUNumber 0x19, type 2 AssetTag 0x08, type 3 SerialNumber 0x07 / AssetTag 0x08, type 4
//            SerialNumber 0x20 / AssetTag 0x21, type 39 SerialNumber 0x08 / AssetTag 0x09 must hold filler, else the run stops
//            (undecided identifier field). CHID check: the fields ComputerHardwareIds are computed from (type 0 Vendor /
//            Version / release bytes, type 1 Manufacturer / Family / ProductName / SKU, type 2 Manufacturer / Product, type 3
//            enclosure type) must be unchanged in the synthetic table (check_chid_fields).
//   Rule 3   MACs (network_adapters.txt; gen_mac / gen_mac_family): OUI exact, last 3 bytes synthetic, ONE synthetic MAC per
//            physical NIC / radio; dependent MACs re-derived with the relation the capture shows (+ delta within the OUI,
//            |delta| <= 16, or the locally administered variant). Bare host: Wi-Fi Direct = Wi-Fi + 1, Wi-Fi Direct #2 = the
//            locally administered variant of Wi-Fi, Bluetooth PAN = Wi-Fi + 4; the I226-V's EUI-64 form in its PCI instance
//            ID (OUI + FFFF + NIC part + 00) re-derived (gen_eui64_instance_id). Relations are recorded in the field map by
//            adapter description ("3r"). VMware VMnet adapters (fixed pattern 00:50:56:C0:00:<n>) exact; Hyper-V keeps its
//            OUI. Remote Bluetooth device addresses (bluetooth.txt, fwid 1.4; the captured Enum roots hold no BTH* root):
//            OUI kept, the rest synthetic, names exact; the local radio address = its adapter MAC identity.
//   Rule 4a  computer name (capture_info.txt os.computer_name; gen_computer_name): "DESKTOP-" + 7 synthetic [A-Z0-9].
//   Rule 4b  Enum\SCSI instance IDs "N&hash&N&address": PnP parent-prefix hashes (topology-derived, not unique) - EXACT (user
//            decision 2026-10-16), like every other PnP instance hash (PCI / USB / DISPLAY / SWD). Another Enum\SCSI instance
//            form (e.g. a drive serial) stops the run as undecided. Volume labels: EXACT.
//   Rule 4c  GUIDs (gen_guid): container IDs, interface GUIDs, volume GUIDs (Enum\STORAGE\Volume and os_ids volume GUID
//            paths - one mapping), software-device (Enum\SWD) instance GUIDs, braced or not (MMDEVAPI, PRINTENUM, RADIO,
//            DRIVERENUM, WPDBUSENUM, ...), MachineGuid / HwProfileGuid / SQM MachineId / SusClientId / telemetry IDs (own style kept),
//            audio endpoint GUIDs, and every version-1 GUID whose node is an adapter MAC: format, case, version digit and
//            variant bits kept, the rest synthetic; a version-1 node = the adapter's synthetic MAC, Windows' no-NIC node
//            806E6F6E6963 exact; MBR-style volume GUIDs {SSSSSSSS-0000-0000-0000-OOOOOOOOOOOO}: only the disk signature
//            synthetic. One real GUID = one synthetic GUID everywhere. The "no container" ID {00000000-0000-0000-FFFF-
//            FFFFFFFFFFFF} stays.
//   Rule 4d  USB device serials (Enum\USB instance IDs without '&', incl. the host's VMware USB arbitration mirror and the
//            guest's passthrough entries) and HID serial strings (hid.txt): same length + classes, hex-encoded ASCII stays
//            hex-encoded ASCII, other hex stays hex (gen_usb_serial). ONE mapping per physical device for BOTH profiles: a
//            shared table (bare-host capture, then guest capture) - the index of first appearance keys a stream shared by both
//            environments, never a hash of the value. A HID serial equal to a USB serial is that identity. Placeholders (all
//            '0', "MSFT30" + zeros) exact.
//   Rule 6   EDID (edid_<n>.bin, fwid >= 1.3; gen_edid): the numeric serial (bytes 12-15) and the FFh serial-string descriptor
//            same-format synthetic, ONE mapping per physical monitor (manufacturer + product code + real serial: the same
//            monitor on several ports / instances gets one synthetic serial); manufacturer / product code / FCh name / week /
//            year exact; the base-block checksum (byte 127) recomputed (gen_edid_checksum); extension blocks untouched (scanned).
//   Rule 7   os_ids.txt (fwid >= 1.3): 7a ProductId - first and last group exact, middle groups rule-2 classes; 7b InstallDate
//            (unix) / InstallTime (FILETIME) - ONE synthetic instant strictly between the OS build release and the capture,
//            both fields the same instant (gen_install_time); 7c user name (and RegisteredOwner when equal) rule-2 classes, the
//            same synthetic name everywhere, NameSamCompatible "DOMAIN\user" with DOMAIN = computer name -> synthetic computer
//            name; 7d SIDs: S-1-5-21 structure and the RID exact, the three sub-authorities synthetic once per machine (user SID
//            and machine prefix consistent, gen_sid); 7e volume serial numbers XXXX-XXXX synthetic hex (gen_volume_serial).
//   Rule 8   fwid 1.4 (optional files; formats not released yet - values = the last quoted string of a line fwid marks
//            "[IDENTIFIER", else the text after its last ": "): 8a Windows installation ID (gen_win_installation_id) - same
//            length, digits stay digits, group layout kept, check digits NOT modelled; LicenseStatus / ProductKeyChannel /
//            Name / Description exact (WinLicenseInfo); 8b non-GUID telemetry IDs, 8d physical disk serials (one per disk,
//            consistent everywhere), 8e GPU per-device unique values - rule-2 classes; 8c SusClientIdValidation (binary) -
//            same-length hex (the identifier hash inside it is NOT modelled). A marked line no rule consumes stops the run.
//   Exact    vendor / product / version strings, part numbers (except rule 2h), firmware filler, ACPI tables, CPUID,
//            topology, the guest's .vmx disk vendor / product strings and NIC OUI, monitor EDID model codes, CHIDs, Secure
//            Boot / TPM info, time zone, locale, keyboard layouts, domain / workgroup, total RAM, screen metrics, Wi-Fi
//            profile counts, printers, HID / Bluetooth / audio / GPU names. Volatile, not part of the profile: uptime. Not part
//            of the profile: the certificate stores (only "test_signing_certificate_present" is recorded for the bare host).
//            ACPI tables are searched for every real identifier: a hit is only reported, never changed.
// Synthetic values: every gen_ draws from an HMAC-SHA256 stream keyed with the 32-byte seed of the BuildConfig (created once
// with BCryptGenRandom); stream labels name a field LOCATION (environment + field id), never a value, so no synthetic byte is
// derived from a real value. A fixed BuildConfig + seed always gives the same identity; gen_new_seed creates a fresh seed:
// a different, equally consistent identity. Equal real values get equal synthetic values; a synthetic value equal to the
// real one is drawn again.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace emu::platform
{
	enum class Environment { vmware_guest, bare_host };
	const char* env_dir_name( Environment e );	// "vmware_guest" | "bare_host" (the profile folder name)
	const char* env_switch( Environment e );	// "--guest" | "--host"

	using Bytes = std::vector<uint8_t>;

	// ── CNG helpers (bcrypt.lib) ──────────────────────────────────────────────────────────────────────────────────────
	std::array<uint8_t, 32> sha256( const void* data, size_t size );
	std::string sha256_hex( const Bytes& b );	// uppercase hex, as Get-FileHash prints it
	std::array<uint8_t, 32> hmac_sha256( const Bytes& key, const std::string& message );
	Bytes random_bytes( size_t n );	// BCryptGenRandom (system-preferred RNG)

	// ── the seeded synthetic source: HMAC-SHA256(seed, "emu-profilegen/1|<env or shared>|<label>|<block counter>") ──────
	class SyntheticStream
	{
	public:
		SyntheticStream( const Bytes& seed, Environment env, const std::string& label );
		static SyntheticStream shared( const Bytes& seed, const std::string& label );	// the same stream for both environments (rule 4d)
		uint8_t next_byte();
		char digit();		// '0'..'9' (rejection sampling, unbiased)
		char upper();		// 'A'..'Z'
		char lower();		// 'a'..'z'
		char hex( bool upper_case );
		char alnum_upper();	// [A-Z0-9]
		uint64_t next_u64();

	private:
		Bytes key_;
		std::string prefix_;
		uint32_t counter_ = 0;
		std::array<uint8_t, 32> block_ {};
		size_t pos_ = 32;
	};

	// ── SMBIOS (RawSMBIOSData: Used20CallingMethod, Major, Minor, DmiRevision, DWORD Length, then the table) ───────────
	struct SmbiosString { size_t offset = 0; size_t length = 0; };	// absolute offset in the blob, length without the NUL
	struct SmbiosStructure
	{
		size_t offset = 0;	// absolute offset of the header in the blob
		uint8_t type = 0, length = 0;
		uint16_t handle = 0;
		std::vector<SmbiosString> strings;	// string 1 = strings[ 0 ]
		size_t end = 0;	// one past the double NUL
	};
	struct SmbiosTable
	{
		uint8_t major = 0, minor = 0;
		uint32_t length = 0;
		std::vector<SmbiosStructure> structures;
	};
	SmbiosTable parse_smbios( const Bytes& raw );	// throws std::runtime_error on any malformed structure

	// ── records ─────────────────────────────────────────────────────────────────────────────────────────────────────────
	// one replaced field (a fieldmap.tsv row): location, length, rule - never a value
	struct FieldMapEntry
	{
		std::string area;		// "smbios.bin", "edid\edid_<n>.bin", or the fwid text file of an occurrence / relation
		std::string id;			// "F01" SMBIOS, "D01" EDID, "M01" MAC, "E01" EUI-64, "N01" computer name, "G01" GUID, "S01"
								// (unused since rule 4b keeps the PnP hashes exact), "U01" USB / HID serial, "X01" EDID serial (edid.txt), "O01" OS identifier, "K01" fwid 1.4 identifier
		std::string rule;		// "1-vmware" "1-rfc4122" "2" "2h" "2o" "3" "3r" "4a" "4b" "4c" "4cr" "4d" "6" "6c" "7a".."7e" "8a".."8e"
		std::string location;	// human-readable: structure / handle / offset, the identity kind, or the relation (adapter descriptions)
		uint64_t offset = 0;	// blobs: absolute byte offset; text: 1-based byte column; relation rows ("..r"): none
		uint64_t line = 0;		// text: 1-based line; blobs: 0
		uint32_t length = 0;	// replaced bytes
	};
	// one identifier of the fwid text outputs (an identity.tsv row)
	struct IdentityEntry
	{
		std::string id, kind, rule, source;	// source = "<file>:<line>" of the first anchor
		size_t occurrences = 0;				// matches in the capture's searched text files (fieldmap.tsv rows)
		std::string original, synthetic;	// same length
	};
	struct TextFile { std::string name; Bytes data; };	// a fwid text output as captured
	// an identifier whose synthetic value comes from another gen_ (the EDID serials of gen_edid), searched in only_file
	struct IdentityPreset
	{
		std::string kind, rule, original, synthetic;
		std::vector<std::string> needles;	// lower-case search forms
		std::string only_file;				// empty: every searched text file
	};
	struct StoragePath { std::string original, synthetic; };	// an Enum\SCSI / Enum\STORAGE\Volume instance path
	struct ByteRange { size_t first = 0, last = 0; };			// [first, last)

	// ── value structs of the gen_ steps (input = the real value + its location label, output = the same struct with the
	//    synthetic value) ─────────────────────────────────────────────────────────────────────────────────────────────
	// rule 1: type 1 UUID, 16 raw bytes as stored at formatted offset 0x08 (guest smbios.bin 0x0066, host 0x0063)
	struct SmbiosUuid
	{
		uint16_t handle = 0;				// type 1 handle (0x0001 in both captures)
		std::array<uint8_t, 16> bytes {};	// raw SMBIOS order
		bool tail_is_mac = false;			// output: bytes 10-15 were set to an adapter's synthetic MAC (gen_smbios)
		std::string tail_adapter;			// output: that adapter's description
	};
	// rules 2 / 2h: one SMBIOS string field (the string-set bytes, no NUL)
	struct SmbiosStringField
	{
		uint8_t type = 0, offset = 0;		// structure type, formatted offset of the string index
		uint16_t handle = 0;
		std::string value;					// same length in and out
	};
	// rule 3: a MAC "XX:XX:XX:XX:XX:XX" (network_adapters.txt / bluetooth.txt)
	struct MacAddress
	{
		std::string text;					// upper case, colon form
		std::string adapter;				// the adapter's Description (a product name), may be empty
		std::string id;						// identity id ("M01"), also the stream label
	};
	struct MacPair { MacAddress real, synthetic; };
	// rule 3: one MAC of a capture with the relation gen_mac_family detects and re-applies
	struct MacMember
	{
		MacAddress mac;
		int parent = -1;					// output: index of the MAC this one follows (-1 = a base)
		long long delta = 0;				// output: + delta (48-bit) from the parent, or
		bool locally_administered = false;	// output: the parent with byte 0 bit 1 toggled
	};
	struct MacFamily { std::vector<MacMember> members; };
	// rule 3: a MAC's EUI-64 form in an instance ID: OUI + FFFF + NIC part + 00 (ffff) or OUI + FFFE + NIC part
	struct Eui64Id { std::string text; bool ffff = true; };
	// rule 4c: a GUID text (with or without braces) and the stream label of its identity
	struct GuidValue { std::string text; std::string label; };
	struct NodeMap { std::map<std::string, std::string> real_to_synthetic; };	// lower-case 12-hex MAC nodes
	// rule 4d: a USB / HID device serial; table_index >= 0: its index in the shared table (stream shared by both profiles)
	struct UsbSerial { std::string text; long table_index = -1; std::string label; };
	struct ComputerName { std::string text; std::string label; };	// rule 4a
	// rules 2 / 4b / 7c / 8b / 8c / 8d / 8e: a plain string value and its stream label
	struct TextValue { std::string text; std::string label; };
	// rule 8a: the license data of license.txt (fwid >= 1.4), exact, carried with the installation ID
	struct WinLicenseInfo
	{
		std::string license_status;			// LicenseStatus
		std::string product_key_channel;	// ProductKeyChannel
		std::string name, description;		// Name / Description of the licensing product
	};
	// rule 8a: the Windows installation ID (OfflineInstallationId, license.txt): decimal digit groups
	struct WinInstallationId
	{
		std::string text;					// digits and separators; same length in and out
		std::vector<size_t> group_lengths;	// output: the digit-group layout (kept)
		std::string source;					// "license.txt:<line>"
		std::string rule = "8a";
		bool check_digits_modelled = false;	// the internal check digits are NOT modelled (no local, citable description)
		std::string label;
		WinLicenseInfo license;
	};
	// rule 6: the EDID numeric serial (bytes 12-15, little-endian) of one monitor
	struct EdidSerial { uint32_t value = 0; std::string label; };
	// rule 6: one EDID value (edid_<n>.bin): base block (128 bytes) + any extension blocks
	struct EdidBlock { std::string name; Bytes bytes; };
	struct ProductId { std::string text; std::string label; };	// rule 7a: "ddddd-ddddd-ddddd-AAddd"
	// rule 7b: the install instant; input: os_build + capture_utc (+ the real values, only to never return them)
	struct InstallTime
	{
		unsigned os_build = 0;				// CurrentBuild (capture_info.txt os.registry)
		std::string capture_utc;			// run.start_utc of the capture
		int64_t unix_seconds = 0;			// InstallDate (REG_DWORD)
		uint64_t filetime = 0;				// InstallTime (REG_QWORD), the same instant
	};
	struct InstallWindow { int64_t lo = 0, hi = 0; };	// unix seconds, exclusive bounds
	struct SidParts { std::string prefix; std::string label; };	// rule 7d: "S-1-5-21-a-b-c" (the RID is not part of it)
	struct VolumeId { std::string serial; std::string label; };	// rule 7e: "XXXX-XXXX"
	struct Seed { std::array<uint8_t, 32> bytes {}; std::string hex; };

	// ── which field rules a build applies (BuildConfig "rules") ─────────────────────────────────────────────────────────
	// A disabled rule keeps the real values; the leak guard then refuses the SYNTHETIC profile, so a rule can only be switched
	// off for a capture that has no field of that rule.
	struct RuleSet
	{
		bool r1 = true, r2 = true, r2h = true, r2o = true, r3 = true, r4a = true, r4b = true, r4c = true, r4d = true, r6 = true, r7 = true,
			 r8 = true;	// r2h: bare_host only
		std::string to_string() const;						// "1 2 2h 2o 3 4a 4b 4c 4d 6 7 8"
		static RuleSet parse( const std::string& list );	// the same form; throws on an unknown rule
		bool operator==( const RuleSet& ) const = default;
	};

	// ── BuildConfig: every input of one build, serializable (build_config.txt next to each output) ─────────────────────
	// The SYNTHETIC copy holds no identifier value and no user-specific absolute path (capture folder = name pattern + pinned
	// manifest SHA-256; out / seed_file relative to repo_root; original_out / capture_parent / companions relative to the
	// capture root; roots "auto"); the ORIGINAL copy adds resolved.capture_root / resolved.capture_folder. The seed (random,
	// not host data) is part of it: a saved BuildConfig reproduces the build byte for byte. A later ImGui front end loads /
	// saves this text through load() / save().
	struct BuildConfig
	{
		static constexpr const char* k_file = "build_config.txt";
		int format = 1;
		std::string tool;						// the tool version that wrote it (informational)
		Environment env = Environment::vmware_guest;
		std::string capture_root = "auto";		// "auto": NOVMP_CAPTURE_ROOT, else the first ancestor folder "capture" holding capture_parent
		std::string capture_parent;				// relative to the capture root
		std::string capture_folder;				// folder-name pattern with a trailing '*' ("*": the one folder there)
		std::string capture_manifest_sha256;	// must be the pinned clean capture of env
		std::vector<std::pair<std::string, std::string>> companions;	// hash list (relative to the capture root), SHA-256
		std::string peer_capture_parent, peer_capture_folder, peer_capture_manifest_sha256;	// the other environment's pinned capture (rule 4d table)
		std::string repo_root = "auto";			// "auto": the first ancestor of the exe folder holding NoVmp.sln and Emulator\data
		std::string out;						// SYNTHETIC profile folder, relative to repo_root (or absolute)
		std::string original_out;				// ORIGINAL profile folder, relative to capture_root (or absolute); "none" = not written
		std::string seed_file;					// the seed store, relative to repo_root (used when seed is empty)
		std::string seed;						// 64 hex digits; empty: read (or create) seed_file
		RuleSet rules;

		static BuildConfig defaults( Environment e );		// the pinned clean capture of e and the documented default paths
		static const BuildConfig& pinned( Environment e );	// the built-in pin (capture_parent / folder / manifest / companions)
		std::string save() const;
		static BuildConfig load( const std::string& text );	// throws std::runtime_error naming the line
		void save_file( const std::filesystem::path& file ) const;
		static BuildConfig load_file( const std::filesystem::path& file );
		bool operator==( const BuildConfig& ) const = default;
	};

	// ── structured log (the caller prints it) ───────────────────────────────────────────────────────────────────────────
	struct BuildLog
	{
		enum class Level { info, warning, error };
		struct Entry { Level level; std::string step, text; };
		std::vector<Entry> entries;
		void info( const std::string& step, const std::string& text ) { entries.push_back( { Level::info, step, text } ); }
		void warning( const std::string& step, const std::string& text ) { entries.push_back( { Level::warning, step, text } ); }
		void error( const std::string& step, const std::string& text ) { entries.push_back( { Level::error, step, text } ); }
		std::string text() const;	// "[profilegen] <step>: <text>" lines
	};

	// the verified contents of one capture folder (load_capture; the unit tests build one in memory)
	struct CaptureInputs
	{
		std::string folder_name;			// the real folder name: redacted before it reaches any synthetic file
		std::string manifest_sha256, tool_line;
		size_t manifest_entries = 0;
		Bytes rsmb;
		std::vector<std::pair<std::string, Bytes>> acpi;	// acpi_*.bin / hwacpi_*.bin, by name
		std::vector<EdidBlock> edid;						// edid_<n>.bin (fwid >= 1.3), by name
		std::vector<TextFile> texts;						// every *.txt of the capture manifest
		std::vector<std::pair<std::string, std::string>> companions;	// verified hash lists, SHA-256
		std::vector<Bytes> usb_sources;		// enum_registry.txt of the bare host, then of the guest (rule 4d shared table); empty: own only
	};

	// ── gen_ inputs / outputs ───────────────────────────────────────────────────────────────────────────────────────────
	struct SmbiosInput { Bytes raw; std::vector<MacPair> macs; };	// macs: for the UUID-MAC relation
	struct SmbiosGen
	{
		Bytes synthetic;							// same size and layout as the input
		std::vector<FieldMapEntry> fields;			// F rows
		std::vector<std::string> needles;			// real values (leak guard), never written
		std::vector<Bytes> binary_needles;
	};
	struct TextIdentityInput { std::vector<TextFile> files; std::vector<std::string> usb_table; std::vector<IdentityPreset> presets; };
	struct TextIdentityGen
	{
		std::vector<IdentityEntry> identity;
		std::vector<StoragePath> storage_paths;
		std::vector<FieldMapEntry> fields;			// occurrence + relation rows
		std::vector<std::string> needles;
		std::vector<Bytes> binary_needles;
		std::vector<MacPair> macs;					// every adapter MAC, real + synthetic
		std::vector<WinInstallationId> installation_ids;	// rule 8a, real + synthetic in pairs (license.txt)
	};
	struct EdidInput { std::vector<EdidBlock> blocks; };
	struct EdidGen
	{
		std::vector<EdidBlock> synthetic;
		std::vector<FieldMapEntry> fields;			// D rows
		std::vector<IdentityPreset> presets;		// the serials for the edid.txt occurrences
		std::vector<std::string> needles;
		size_t ext_blocks = 0, ext_hits = 0;		// extension blocks seen / those containing a real serial
	};
	// the whole identity of one environment (gen_identity), in memory: holds real values, never serialize it
	struct Identity
	{
		Environment env = Environment::vmware_guest;
		Bytes smbios_original, smbios_synthetic;
		std::vector<std::pair<std::string, Bytes>> acpi;	// exact
		std::vector<EdidBlock> edid_original, edid_synthetic;
		size_t edid_ext_blocks = 0, edid_ext_hits = 0;
		std::vector<IdentityEntry> identity;
		std::vector<StoragePath> storage_paths;
		std::vector<FieldMapEntry> fields;	// SMBIOS rows first, then EDID, then text rows
		size_t smbios_fields = 0, edid_fields = 0;
		std::vector<std::string> needles;	// leak guard
		std::vector<Bytes> binary_needles;
		std::vector<MacPair> macs;
	};
	struct GenResult
	{
		bool ok = false;
		std::string error;						// the failure (no identifier value), empty when ok
		BuildLog log;
		BuildConfig config;						// the configuration as used (seed included)
		Identity identity;						// in memory only
		std::map<std::string, Bytes> synthetic, original;	// the files of both profiles (relative path -> bytes)
		std::filesystem::path out, original_out;			// resolved folders (original_out empty: not written)
		bool checked = false;					// run_build( true ): the output folder matched the regeneration
	};

	class ProfileGenerator
	{
	public:
		explicit ProfileGenerator( BuildConfig c );
		const BuildConfig& config() const { return c_; }

		// resolve the paths, verify + read the pinned capture, gen_build, then write both profiles - or with check = true
		// compare the regeneration with the SYNTHETIC output folder byte for byte and write nothing
		GenResult run_build( bool check = false );
		// the generation, no file I/O: both profiles' files (incl. build_config.txt + manifest.txt) and every self-check
		GenResult gen_build( const CaptureInputs& in, const std::string& resolved_capture_root = {} );
		Identity gen_identity( const CaptureInputs& in );	// throws on any rule / self-check failure

		// ── the generation steps (all use this generator's environment and seed) ──────────────────────────────────────
		SmbiosGen gen_smbios( const SmbiosInput& in );						// rules 1, 2, 2h, 2o + guard
		SmbiosUuid gen_system_uuid( const SmbiosUuid& real );				// rule 1 (the MAC tail is applied by gen_smbios)
		SmbiosStringField gen_smbios_string( const SmbiosStringField& real );	// rules 2 / 2h
		TextIdentityGen gen_text_identity( const TextIdentityInput& in );	// rules 3, 4a-4d, 7, 8 on the text outputs
		MacAddress gen_mac( const MacAddress& real );						// rule 3, one base MAC
		MacFamily gen_mac_family( const MacFamily& real );					// rule 3, bases + re-derived dependents
		Eui64Id gen_eui64_instance_id( const Eui64Id& real, const MacAddress& synthetic_nic );
		GuidValue gen_guid( const GuidValue& real, const NodeMap& nodes = {} );	// rule 4c incl. the version-1 node and MBR form
		UsbSerial gen_usb_serial( const UsbSerial& real );					// rule 4d
		ComputerName gen_computer_name( const ComputerName& real );			// rule 4a
		TextValue gen_class_string( const TextValue& real );				// rule-2 classes (7c user name, 8b, 8d, 8e)
		TextValue gen_hex_token( const TextValue& real );					// rule 4b / 8c: hex digits of the same case and length
		WinInstallationId gen_win_installation_id( const WinInstallationId& real );	// rule 8a
		ProductId gen_product_id( const ProductId& real );					// rule 7a
		InstallTime gen_install_time( const InstallTime& real );			// rule 7b
		SidParts gen_sid( const SidParts& real );							// rule 7d
		VolumeId gen_volume_serial( const VolumeId& real );					// rule 7e
		EdidGen gen_edid( const EdidInput& in );							// rule 6
		EdidSerial gen_edid_serial( const EdidSerial& real );
		EdidBlock gen_edid_checksum( const EdidBlock& block );				// byte 127 so the base block sums to 0
		Seed gen_new_seed();												// a fresh identity: new seed in this generator's config

		// ── parsers, loaders, savers, validators ──────────────────────────────────────────────────────────────────────
		static std::vector<std::string> parse_usb_serial_table( const std::vector<Bytes>& enum_registry_texts );
		static InstallWindow parse_install_window( unsigned os_build, const std::string& capture_utc );
		static CaptureInputs load_capture( const std::filesystem::path& capture_folder, const std::filesystem::path& capture_root, const BuildConfig& c, BuildLog& log );
		static Seed load_seed( const std::filesystem::path& file, bool allow_create, BuildLog& log );
		static void save_seed( const std::filesystem::path& file, const Seed& seed );
		// writes the files (relative path -> bytes); an ORIGINAL profile inside a git work tree is refused (throws) before
		// anything is created; a file in dir that is not part of the profile is an error (stale output)
		static void save_profile( const std::filesystem::path& dir, const std::map<std::string, Bytes>& files, bool original );
		static bool check_filler( const std::string& s );
		static bool check_usb_placeholder( const std::string& s );			// all '0', or "MSFT30" + zeros
		// the folder holding a ".git" entry (directory or worktree file) at p or above it; empty = not in a git work tree
		static std::filesystem::path check_git_work_tree( const std::filesystem::path& p );
		static std::vector<ByteRange> check_diff_ranges( const Bytes& a, const Bytes& b );
		// the CHID-relevant SMBIOS fields: how many were compared, how many differ (must be 0)
		struct ChidCheck { size_t checked = 0, changed = 0; };
		static ChidCheck check_chid_fields( const Bytes& original, const Bytes& synthetic );

	private:
		SyntheticStream stream( const std::string& label ) const;
		Bytes seed_bytes() const;
		BuildConfig c_;
	};
}
