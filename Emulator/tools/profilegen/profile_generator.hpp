// profile_generator.hpp - the platform identity profiles of the emulator (plan 1.J.49, ledger U1510-U1512; fwid 1.4 inputs,
// tool exclusions, disk descriptors, SusClientIdValidation layout, per-build context / thread safety (plan 1.J.49.a): U1513-U1514;
// atomic profile writes U1515, version-1 GUID timestamps U1516, synthetic-only field view + single-value edits U1517,
// synthetic text views U1518).
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
//   --guest / --vm  vmware_guest: the clean VMware guest WITHOUT the VmLoader driver, fwid 1.4 capture Run3b (U1513; its
//                   firmware - rsmb.bin + the 14 ACPI table files - is byte-identical to the fwid 1.2 Run 3 capture, checked
//                   on every build against Run 3's pinned manifest: BuildConfig firmware_ref_*). Runs 1 / 2
//                   (driver-modified) are never used.
//   --host          bare_host: the clean bare host, fwid 1.4 capture (its firmware - rsmb.bin, 45 ACPI table files, 7 EDID
//                   values - is byte-identical to the fwid 1.3 host capture, checked the same way).
// Files of a profile: smbios.bin (RawSMBIOSData as GetSystemFirmwareTable('RSMB') returns it), acpi\<name>.bin (every
// acpi_*.bin / hwacpi_*.bin, byte-exact, never modified), edid\edid_<n>.bin (fwid >= 1.3, rule 6), disk\disk_<n>_descriptor.bin
// (fwid >= 1.4: the raw STORAGE_DEVICE_DESCRIPTOR of \\.\PhysicalDrive<n>, rule 8d), identity.tsv (the identifiers of the fwid
// text outputs), fieldmap.tsv (every replaced field as LOCATION + LENGTH + RULE, never a value; identical in both profiles),
// exclusions.tsv (the capture ranges of tool artifacts that are NOT part of the presented machine, BuildConfig "exclude"),
// volatile.tsv (the captured values the emulator may advance: uptime, free memory, DiagTrack counters / timestamps, ...),
// identity_map.tsv (every synthetic value of the identity by field-location key: generation records it, every later build
// applies it; the original copy alone adds the install-history offset), views\<fwid file> (U1518, BuildConfig "views =
// yes": the synthetic text views - every recorded occurrence replaced in its own form, the install-history timestamps a view
// shows (registry last-write times, code-integrity times) moved by the rule 7b offset, see gen_text_identity),
// build_config.txt (the BuildConfig: with the
// identity map a build is reproducible from it; a later ImGui front end loads / saves
// the same file), manifest.txt (SHA-256 of every file, provenance, tool version).
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
//            "Undefined", the placeholders "00000000" / "9876543210", any value of ONE repeated character (length >= 2, e.g. the
//            Sabrent bridge's repeated-digit disk serial; user decision J 2026-10-16) (case-insensitive, trailing blanks
//            ignored), empty or no string. Fields: type 2 SerialNumber (0x07), type 17 SerialNumber (0x18), type 17 AssetTag (0x19).
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
//            OUI. Remote Bluetooth device addresses (bluetooth.txt, fwid 1.4: the subkey names of BTHPORT\Parameters\Devices,
//            12 hex digits; the captured Enum roots hold no BTH* root): OUI kept, the rest synthetic, names exact; the local
//            radio address (LocalInfo, and the "ServicesFor<address>" subkey name of each device) = its adapter MAC identity
//            (bare host: the Bluetooth PAN adapter = Wi-Fi + 4).
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
//            FFFFFFFFFFFF} stays. RFC 4122 version-1 GUIDs (U1516, decision E "valid v1" + rule 7b): the 60-bit timestamp =
//            the real one + the install-history offset of rule 7b (map key "identity/install"; derived, never drawn), so
//            HwProfileGuid, the v1 container / interface / volume / WPD GUIDs keep their real ordering and spacing to the
//            install; the clock sequence synthetic (variant bits kept, Randomize->gen_clock_seq_like); the node as above
//            (adapter MAC -> its synthetic MAC, 806E6F6E6963 exact, any other node synthetic). A capture without an install
//            history cannot build a version-1 GUID (the run stops). Application re-checks every saved v1 value against the
//            rule; BuildContext::upgrade_v1_time re-derives the timestamp of a map written before U1516 (clock sequence and
//            node of the saved value kept; emu-profilegen --upgrade-v1-time). U1518: also the 16-byte REG_BINARY form of a
//            GUID (Windows byte order): a known GUID's binary form = its synthetic binary form; a version-1 GUID with an
//            adapter-MAC node found only in binary form is a mac_node_guid identity of its own (MBR-style GUIDs: no binary form).
//   Rule 4d  USB device serials (Enum\USB instance IDs without '&', incl. the host's VMware USB arbitration mirror and the
//            guest's passthrough entries) and HID serial strings (hid.txt): same length + classes, hex-encoded ASCII stays
//            hex-encoded ASCII, other hex stays hex (gen_usb_serial). ONE mapping per physical device for BOTH profiles: a
//            shared table (bare-host capture, then guest capture) - the index of first appearance keys a stream shared by both
//            environments, never a hash of the value. A HID serial equal to a USB serial is that identity (fwid 1.4 bare host:
//            all 3 real HID serials); a real HID serial whose device's USB instance serial is a placeholder (user decision J)
//            is synthetic with a stream of this environment, the USB placeholder stays (U1518: a USB serial TRUNCATED in a
//            text output - a proper prefix of >= 16 characters as a token, the host's SetupAPI instance ID of the flash drive -
//            = the same prefix of that device's synthetic serial, relation row "4dr"; in the fwid 1.4 host capture the
//            Basilisk's 7 HID serial lines hold the all-zero placeholder too). Placeholders (one repeated character, e.g. all
//            '0'; "MSFT30" + zeros) and empty HID serials exact.
//   Rule 6   EDID (edid_<n>.bin, fwid >= 1.3; gen_edid): the numeric serial (bytes 12-15) and the FFh serial-string descriptor
//            same-format synthetic, ONE mapping per physical monitor (manufacturer + product code + real serial: the same
//            monitor on several ports / instances gets one synthetic serial); manufacturer / product code / FCh name / week /
//            year exact; the base-block checksum (byte 127) recomputed (gen_edid_checksum); extension blocks untouched (scanned).
//   Rule 7   os_ids.txt (fwid >= 1.3): 7a ProductId - first and last group exact, middle groups rule-2 classes; 7b the install
//            history SHIFTED TOGETHER (user decision 2026-10-16): ONE offset per profile, whole seconds, drawn from RDRAND at generation
//            (gen_install_time), added to InstallDate (unix) / InstallTime (FILETIME) and to every install-history timestamp
//            - the per-device DEVPKEY_Device_FirstInstallDate / InstallDate / LastArrivalDate ([machine-specific],
//            pci_devices.txt; FILETIME hex + ISO), the SQM first-session time (WinSqmFirstSessionStartTime), the DiagTrack
//            FILETIME timestamps (LastFreeNetworkLossTime, LastConnectivityHeartBeatTime, LastPersistedEventTime; volatile,
//            shifted so they stay after the install) and the Bluetooth device events (LastSeen, LastConnected,
//            FingerprintTimestamp) - wherever the value occurs (hex, decimal; the ISO form only on these lines). Real
//            ordering and spacing are kept exactly (synthetic - real = the offset for every one; the self-check counts it);
//            every shifted event stays strictly between the build release and the capture, so the offset is at least one
//            day earlier (the latest event, the last arrival, lies minutes before the capture). Not shifted: driver dates,
//            registry key last-write times, code-integrity cache times (no install event: driver build dates precede an
//            install anyway, last-write / cache times stay after the earlier install). The offset is never written to a
//            profile; 7c user name (and RegisteredOwner when equal) rule-2 classes, the
//            same synthetic name everywhere, NameSamCompatible "DOMAIN\user" with DOMAIN = computer name -> synthetic computer
//            name; 7d SIDs: S-1-5-21 structure and the RID exact, the three sub-authorities synthetic once per machine (user SID
//            and machine prefix consistent, gen_sid); 7e volume serial numbers XXXX-XXXX synthetic hex (gen_volume_serial).
//   Rule 8   fwid 1.4 (the formats of fwid_probe 1.4; every line fwid marks "[IDENTIFIER" outside '#' comments must be
//            consumed, else the run stops - it needs a field decision; "[IDENTIFIER?]" marks the value line above it):
//            8a Windows installation ID (license.txt, one per SoftwareLicensingProduct row; gen_win_installation_id) - same
//               length, digits stay digits, the group layout kept (a 63-digit ID: 9 groups of 7), check digits
//               VALID ((d0 + 2 d1 + d2 + 2 d3 + d4 + 2 d5) mod 7 per group of 7, check_installation_id), stable per identity map; ID (SKU) / Name / Description / LicenseStatus / ProductKeyChannel
//               exact (WinLicenseInfo of the same row).
//            8b non-GUID telemetry IDs - rule-2 classes (none in the fwid 1.4 captures). DiagTrack / SQMClient counters and
//               timestamps exact, the DiagTrack ones listed in volatile.tsv (user decision J: a plausible snapshot the
//               emulator may advance); SusClientId and the SQM MachineId: rule 4c.
//            8c SusClientIdValidation (REG_BINARY) - the layout both captures show is kept: header 06h 02h len1 01h len2 len3,
//               UTF-16LE string 1 (len1 bytes) = the system disk's serial, 06h + 6 bytes = an adapter MAC, UTF-16LE string 2
//               (len2) = the SMBIOS baseboard serial, string 3 (len3) = a further SMBIOS serial (filler in both captures);
//               every component = its synthetic identity (disk serial 8d, MAC 3, SMBIOS rule 2), filler kept
//               (gen_sus_client_id_validation). Another layout stops the run.
//            8d physical disk serials (disks.txt + disk\disk_<n>_descriptor.bin at SerialNumberOffset; one per disk,
//               consistent everywhere): rule-2 classes on the serial without its padding; a serial that is the start of a
//               USB serial of the capture (a USB flash drive) = the start of that USB serial's synthetic value; filler (one
//               repeated character: the Sabrent bridge) exact. Vendor / product / revision exact.
//            8e GPU (gpu.txt, display-class registry): UserModeDriverGUID and driver-store paths exact (driver-package
//               constants, user decision J); any other "[IDENTIFIER?]" GUID (bare host: the DriverState device directory
//               GUID of KmFileIoBaseFilePath, also a device property in pci_devices.txt) is a per-device value: rule 4c.
//            Printers: names / drivers exact; a WSD port "WSD-<GUID>" - the GUID rule 4c (one mapping per GUID). Audio
//            endpoint GUIDs, the Wi-Fi interface GUID (= the network interface GUID): rule 4c. HID device paths, CHIDs,
//            the workgroup / domain name, Secure Boot / TPM data: exact.
//   Exclude  tool artifacts of the capture setup are NOT part of the presented machine (user decision J): the BuildConfig's
//            ToolExclusion list (name, case-insensitive patterns, reason; reviewable, reproducible; defaults: RustDesk Printer,
//            RustDesk - its service and any driver / registry / device trace, user decision 2026-10-16 -, System Informer,
//            DebugView, VmLoader; a unit is named after the first item any of its lines matches). A pattern hit outside
//            '#' comments of a profile text file (capture_info.txt = run metadata and certificates.txt = not part of the
//            profile are not scanned) excludes its unit: a quoted list item or numbered list line alone; else its "## " /
//            "=== " block, a registry key block with all its subkey blocks and the parent's subkey line; a block that
//            records the item as absent ("exists: no", "not present", "(not found)") is not excluded. Identifiers anchored
//            only inside excluded ranges are dropped (their values stay in the leak guard); one of them occurring outside
//            the ranges stops the run (capture_info.txt lines are excluded with it). exclusions.tsv lists the ranges.
//   Exact    vendor / product / version strings, part numbers (except rule 2h), firmware filler, ACPI tables, CPUID,
//            topology, the guest's .vmx disk vendor / product strings and NIC OUI, monitor EDID model codes, CHIDs, Secure
//            Boot / TPM info, time zone, locale, keyboard layouts, domain / workgroup, total RAM, screen metrics, Wi-Fi
//            profile counts, printers, HID / Bluetooth / audio / GPU names, VMware Workstation's host-side services and VMnet
//            adapters (--host: real host state). Volatile (volatile.tsv): every line fwid marks "[volatile" (uptime, free
//            memory, the active keyboard layout, the daylight state) and the DiagTrack counters / timestamps. Not part of
//            the profile: the certificate stores (only "test_signing_certificate_present" is recorded for the bare host).
//            ACPI tables are searched for every real identifier: a hit is only reported, never changed.
// Synthetic values (user decision 2026-10-17; design: the user's rid demo): GENERATION and APPLICATION are split.
//   GENERATION (the first build of an environment, or --new-identity / ProfileGen->gen_new_identity()): every synthetic
//     value comes live from RDRAND through the ONE random source, the Randomizer (pg_randomize.hpp, pg_rand.asm):
//     Randomize->gen_<what>() returns a finished value in its final Windows text form; no other function draws random
//     numbers. Each value is recorded in the identity map under a key that names a field LOCATION (environment + field id,
//     e.g. "identity/G05", "smbios/t1/h0001/uuid", "shared/usb/3"), never a value.
//   APPLICATION (every later build, --check, test.cmd's pg_profile_check suites): the saved identity map
//     (identity_map.tsv of the profile) is applied to the clean capture - deterministic, byte for byte, no randomness. A map
//     that does not fit the capture (a missing or unused key, another kind / length) stops the build.
//   The repository copy of the map holds synthetic values only. The install-history offset (rule 7b) is recorded only in
//   the LOCAL original profile's copy: offset + synthetic times would give the real times. The USB serials both profiles
//   share are "shared/" keys: a build takes them from the other environment's map when its own has none.
//   Values travel as const char* (the generator's interned value pool owns them, process lifetime); a blob field is converted from
//   its text at the single write point (bytes_of_hex, le32_of_decimal). Equal real values get equal synthetic values; no
//   synthetic value equals its real value.
// Instances: ProfileGenerator is a singleton like the demo's Randomizer (ProfileGen->..., ProfileGen.get_instance()); no
// other instance exists. Everything ONE build changes lives in a BuildContext (user decision 2026-10-17 "Per-build context",
// plan 1.J.49.a) that every step takes explicitly:
//     BuildContext ctx = ProfileGen->new_context( cfg );          // a snapshot of cfg, no identity loaded (generation)
//     ProfileGen->load_identity_maps( ctx, own, peer );           // or: application of a saved identity
//     GenResult r = ProfileGen->run_build( ctx, check );          // or gen_build( ctx, capture ) / gen_<what>( ctx, struct )
// Threading: two builds with different contexts (--guest and --host) may run in parallel threads; ONE context is used by one
// thread at a time. The singleton itself holds only shared state, each part behind its own lock:
//   default BuildConfig    std::shared_mutex  load_config() writes it, config() / new_context() return a COPY
//   interned value pool    std::mutex         keep(): every const char* a gen_ step returns points into it (node-based
//                                             std::set: never moved, never freed - valid for the process lifetime, also
//                                             after the context that asked for it is gone)
//   shared/ registry       std::mutex         the "shared/" values (one USB serial per physical device for BOTH profiles)
//                                             of this process: when two fresh generations run concurrently, the first build
//                                             that needs a shared/ key records it, the other takes the same value (the peer
//                                             map on disk still has precedence; application never consults the registry)
// Lock order: the three locks are leaves - no code path holds two of them at once, and none is held while calling
// Randomize-> (a value is drawn first, then interned, then registered insert-if-absent: a concurrent first writer wins and
// the later draw is discarded) or while doing file I/O. reset() / reset_shared_values() must not run while a build runs.
// The Randomizer is thread-safe and lock-free on its own (pg_randomize.hpp). Files (user decision 2026-10-17 "Atomic
// writes", U1515): a build reads its own and the peer identity_map.tsv ONCE at its start (load_identity_map: a snapshot in
// the BuildContext; nothing reads them again) and writes each profile folder atomically (save_profile): every file goes
// into the new sibling folder "<dir>.partial" (created with CREATE_NEW semantics, each file CREATE_NEW + flushed), then
// the folder is switched into place by rename - the previous folder is renamed to "<dir>.old" first and, once the new one
// is in place, exactly its listed profile files and emptied sub-folders are removed (never a recursive delete; the folder
// must be a validated profile folder, check_profile_dir). A ".partial" / ".old" sibling left by an interrupted run stops
// the write and is reported, never deleted. A reader of a peer map that meets the switch takes the complete previous map
// from "<dir>.old" or the complete new one, never a half-written file - so two builds may write both profiles in parallel.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
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

	// ── CNG hashing (bcrypt.lib): the manifests' SHA-256 - no randomness here ────────────────────────────────────────────
	std::array<uint8_t, 32> sha256( const void* data, size_t size );
	std::string sha256_hex( const Bytes& b );	// uppercase hex, as Get-FileHash prints it
	// the single write-point conversions of a text value into blob bytes
	Bytes bytes_of_hex( const char* hex );		// "564d..." -> bytes (an even number of hex digits)
	uint32_t le32_of_decimal( const char* dec );	// "123456" -> the 32-bit value (stored little-endian by the caller)

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
		std::string area;		// "smbios.bin", "edid\edid_<n>.bin", "disk\disk_<n>_descriptor.bin", or the fwid text file of an occurrence / relation
		std::string id;			// "F01" SMBIOS, "D01" EDID, "R01" disk descriptor, "M01" MAC, "E01" EUI-64, "N01" computer name, "G01" GUID, "S01"
								// (unused since rule 4b keeps the PnP hashes exact), "U01" USB / HID serial, "X01" EDID serial (edid.txt), "O01" OS identifier, "K01" fwid 1.4 identifier
		std::string rule;		// "1-vmware" "1-rfc4122" "2" "2h" "2o" "3" "3r" "4a" "4b" "4c" "4cr" "4d" "4dr" "6" "6c" "7a".."7e" "8a".."8e" "8cr" "8dr"
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
	//    synthetic value). Value fields are const char* in the final text form; an output points into the generator's
	//    interned value pool (valid for the process lifetime; never into a context's identity map) ─────────────────────
	// rule 1: type 1 UUID, the 16 raw bytes stored at formatted offset 0x08 (guest smbios.bin 0x0066, host 0x0063) as 32 hex
	// digits in raw SMBIOS order
	struct SmbiosUuid
	{
		uint16_t handle = 0;				// type 1 handle (0x0001 in both captures)
		const char* hex = nullptr;			// 32 hex digits, raw SMBIOS order
		bool tail_is_mac = false;			// output: bytes 10-15 were set to an adapter's synthetic MAC (gen_smbios)
		std::string tail_adapter;			// output: that adapter's description
	};
	// rules 2 / 2h: one SMBIOS string field (the string-set bytes, no NUL)
	struct SmbiosStringField
	{
		uint8_t type = 0, offset = 0;		// structure type, formatted offset of the string index
		uint16_t handle = 0;
		const char* value = nullptr;		// same length in and out
	};
	// rule 3: a MAC "XX:XX:XX:XX:XX:XX" (network_adapters.txt / bluetooth.txt)
	struct MacAddress
	{
		const char* text = nullptr;			// upper case, colon form
		std::string adapter;				// the adapter's Description (a product name), may be empty
		std::string id;						// identity id ("M01"), also the identity-map key
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
	struct Eui64Id { const char* text = nullptr; bool ffff = true; };
	// rule 4c: a GUID text (with or without braces) and the identity-map key of its identity
	struct GuidValue { const char* text = nullptr; std::string label; };
	// rule 4c (U1516): the fields of an RFC 4122 version-1 (time-based) GUID text; ok = false for any other GUID (another
	// version, another variant, the MBR form {SSSSSSSS-0000-0000-0000-OOOOOOOOOOOO})
	struct GuidV1
	{
		bool ok = false;
		uint64_t timestamp = 0;				// 60 bits: 100-ns intervals since 1582-10-15 00:00 UTC (time_hi & 0x0FFF, time_mid, time_low)
		std::string clock_seq;				// group 4: 4 hex digits (variant bits + 14-bit clock sequence), as written
		std::string node;					// group 5: 12 hex digits, as written
	};
	struct NodeMap { std::map<std::string, std::string> real_to_synthetic; };	// lower-case 12-hex MAC nodes
	// rule 4d: a USB / HID device serial; table_index >= 0: its index in the shared table (key "shared/usb/<index>": the
	// same value in both profiles)
	struct UsbSerial { const char* text = nullptr; long table_index = -1; std::string label; };
	struct ComputerName { const char* text = nullptr; std::string label; };	// rule 4a
	// rules 2 / 4b / 7c / 8b / 8e: a plain string value and its identity-map key
	struct TextValue { const char* text = nullptr; std::string label; };
	// rule 8a: the license data of license.txt (fwid >= 1.4), exact, carried with the installation ID
	struct WinLicenseInfo
	{
		std::string sku_id;					// ID (the SKU GUID of the SoftwareLicensingProduct row)
		std::string license_status;			// LicenseStatus (the number fwid prints first)
		std::string product_key_channel;	// ProductKeyChannel
		std::string name, description;		// Name / Description of the licensing product
	};
	// rule 8a: the Windows installation ID (OfflineInstallationId, license.txt): decimal digit groups
	struct WinInstallationId
	{
		const char* text = nullptr;			// 63 digits (+ separators of the real one); same length in and out
		std::vector<size_t> group_lengths;	// output: the digit-group layout (9 groups of 7)
		std::string source;					// "license.txt:<line>"
		std::string rule = "8a";
		bool check_digits_modelled = true;	// output: each group = 6 digits + (d0 + 2 d1 + d2 + 2 d3 + d4 + 2 d5) mod 7
		std::string label;
		WinLicenseInfo license;
	};
	// rule 6: the EDID numeric serial (bytes 12-15, little-endian) of one monitor, as its decimal value
	struct EdidSerial { const char* text = nullptr; std::string label; };
	// rule 6: one EDID value (edid_<n>.bin): base block (128 bytes) + any extension blocks
	struct EdidBlock { std::string name; Bytes bytes; };
	struct ProductId { const char* text = nullptr; std::string label; };	// rule 7a: "ddddd-ddddd-ddddd-AAddd"
	// rule 7b: the install-history shift; input: os_build + capture_utc + the real InstallDate / InstallTime and the earliest /
	// latest install-history event (unix seconds); output: the shifted install and the one offset (whole seconds) every
	// install-history timestamp of the profile is shifted by (never written to a profile file)
	struct InstallTime
	{
		unsigned os_build = 0;				// CurrentBuild (capture_info.txt os.registry)
		std::string capture_utc;			// run.start_utc of the capture
		int64_t unix_seconds = 0;			// InstallDate (REG_DWORD)
		uint64_t filetime = 0;				// InstallTime (REG_QWORD)
		int64_t earliest_event = 0, latest_event = 0;	// input: the install-history events (0: only the install itself)
		int64_t shift_seconds = 0;			// output: synthetic = real + shift for every install-history timestamp
	};
	// the self-check of the shift (TextIdentityGen / Identity; counts only)
	struct TimeShiftCheck
	{
		size_t shifted = 0, exact = 0;		// install-history identities / those whose synthetic - real = the one offset
		size_t before_install_real = 0, before_install_synthetic = 0;	// events earlier than the OS install (setup phase)
		size_t outside_window = 0;			// shifted events not strictly between the build release and the capture
		std::map<std::string, size_t> per_kind;
	};
	struct InstallWindow { int64_t lo = 0, hi = 0; };	// unix seconds, exclusive bounds
	// the self-check of the version-1 GUID timestamps (U1516; counts only)
	struct V1GuidCheck
	{
		size_t guids = 0, exact = 0;		// RFC 4122 version-1 GUID identities / those whose synthetic timestamp = real + the offset
		size_t before_install = 0;			// real timestamps earlier than the OS install (they stay earlier by the same spacing)
		size_t after_capture = 0;			// real timestamps later than the capture (none expected)
	};
	struct SidParts { const char* prefix = nullptr; std::string label; };	// rule 7d: "S-1-5-21-a-b-c" (the RID is not part of it)
	struct VolumeId { const char* serial = nullptr; std::string label; };	// rule 7e: "XXXX-XXXX"

	// ── the identity map (identity_map.tsv): every synthetic value of one environment by key (a field location, never a
	//    value) - what GENERATION records and APPLICATION applies ─────────────────────────────────────────────────────
	struct IdentityMapEntry
	{
		std::string kind;					// "mac", "guid", "uuid", "installation_id", "install", ...
		std::string value;					// the synthetic value, final text form
		bool operator==( const IdentityMapEntry& ) const = default;
	};
	struct IdentityMap
	{
		std::map<std::string, IdentityMapEntry> entries;
		bool has_install_shift = false;		// the ORIGINAL (local-only) copy: the rule 7b offset in seconds
		int64_t install_shift = 0;
		bool operator==( const IdentityMap& ) const = default;
	};
	// one identity_map.tsv read at the start of a build (load_identity_map, U1515): found = false when the profile folder
	// has no map (and no replacement of it is in progress)
	struct IdentityMapFile
	{
		bool found = false;
		IdentityMap map;
		std::string source;					// "profile" or "previous profile (a replacement was switching it)": the folder read
		size_t attempts = 0;				// reads tried (a reader that meets a folder switch tries again)
	};
	// check_profile_dir (U1515): may save_profile replace this existing folder? ok = the folder is a profile folder of the
	// expected layout: "<...>\Emulator\data\platform\<env>" or "<...>\profiles_original\<env>" with <env> = vmware_guest |
	// bare_host, and (when it exists) holds the manifest.txt this tool writes for <env>
	struct ProfileDirCheck
	{
		bool ok = false;
		std::string reason;					// why not (locations only)
	};
	// what save_profile did (U1515): the folder written atomically ("<dir>.partial" -> "<dir>"), the previous one replaced
	struct ProfileSaveResult
	{
		std::filesystem::path dir;			// the profile folder (absolute)
		size_t files = 0;					// files written
		bool replaced = false;				// a previous profile folder existed and was switched out
		bool old_removed = false;			// "<dir>.old" (the previous folder) removed after the switch
		size_t old_files_removed = 0;		// its listed profile files removed
		std::string note;					// set when "<dir>.old" had to stay (what is left), else empty
	};

	// rule 8d: one raw STORAGE_DEVICE_DESCRIPTOR (disk_<n>_descriptor.bin, fwid >= 1.4; layout: Windows SDK winioctl.h -
	// Version, Size, DeviceType, DeviceTypeModifier, RemovableMedia, CommandQueueing, VendorIdOffset @12, ProductIdOffset @16,
	// ProductRevisionOffset @20, SerialNumberOffset @24, BusType @28, RawPropertiesLength @32, RawDeviceProperties @36)
	struct DiskDescriptor { std::string name; Bytes bytes; };
	// rule 8c: the SusClientIdValidation layout of both clean fwid 1.4 captures (see the header); ok = false: another layout
	struct SusValidationLayout
	{
		bool ok = false;
		std::string disk_serial, board_serial, other_serial;	// the three UTF-16LE strings (ASCII), string 1 / 2 / 3
		std::array<uint8_t, 6> mac {};
		size_t mac_offset = 0;									// offset of the 6 MAC bytes in the blob
	};
	struct SusValidationInput
	{
		Bytes real;
		std::vector<std::pair<std::string, std::string>> strings;	// real -> synthetic: disk serials, SMBIOS rule-2 values
		std::vector<MacPair> macs;									// the adapter MACs, real + synthetic
	};
	struct SusValidation
	{
		const char* hex = nullptr;					// the synthetic blob as upper-case hex text, same length and layout
		SusValidationLayout layout;					// of the real blob
		std::vector<std::string> components;		// output: how each component was set (no values)
	};

	// ── tool artifacts of the capture setup (BuildConfig "exclude"; user decision J 2026-10-16) ───────────────────────────
	struct ToolExclusion
	{
		std::string name;						// "RustDesk Printer"
		std::vector<std::string> patterns;		// case-insensitive substrings identifying it
		std::string reason;
		bool operator==( const ToolExclusion& ) const = default;
	};
	// one excluded range of a capture text file (an exclusions.tsv row; 1-based lines, inclusive)
	struct ExclusionRange { std::string item, file, unit; size_t first_line = 0, last_line = 0; };
	struct ExclusionScan
	{
		std::vector<ExclusionRange> ranges;
		size_t absence_records = 0;				// pattern hits inside records of the item's absence (not excluded)
		size_t scanned_files = 0;
		bool excluded( const std::string& file, size_t line ) const;
		const ExclusionRange* range_of( const std::string& file, size_t line ) const;
	};
	// a captured value the emulator may advance (a volatile.tsv row; the name, never the value)
	struct VolatileItem { std::string file; size_t line = 0; std::string name, mark; };
	// the firmware of the capture compared with the reference capture of the same machine (BuildConfig firmware_ref_*)
	struct FirmwareRefCheck { std::string manifest_sha256; size_t compared = 0, identical = 0; };

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
	// manifest SHA-256; out / peer_identity_map relative to repo_root; identity_map relative to out; original_out /
	// capture_parent / companions relative to the capture root; roots "auto"); the ORIGINAL copy adds resolved.capture_root /
	// resolved.capture_folder. With its identity map a saved BuildConfig reproduces the build byte for byte (APPLICATION).
	// A later ImGui front end loads / saves this text through load() / save().
	struct BuildConfig
	{
		static constexpr const char* k_file = "build_config.txt";
		int format = 3;							// 3 since U1513: identity_map / peer_identity_map instead of a seed (formats 1 / 2 are refused)
		std::string tool;						// the tool version that wrote it (informational)
		Environment env = Environment::vmware_guest;
		std::string capture_root = "auto";		// "auto": NOVMP_CAPTURE_ROOT, else the first ancestor folder "capture" holding capture_parent
		std::string capture_parent;				// relative to the capture root
		std::string capture_folder;				// folder-name pattern with a trailing '*' ("*": the one folder there)
		std::string capture_manifest_sha256;	// must be the pinned clean capture of env
		std::vector<std::pair<std::string, std::string>> companions;	// hash list (relative to the capture root), SHA-256
		std::string peer_capture_parent, peer_capture_folder, peer_capture_manifest_sha256;	// the other environment's pinned capture (rule 4d table)
		std::string firmware_ref_parent, firmware_ref_folder, firmware_ref_manifest_sha256;	// an older capture of the same machine whose firmware entries must be identical
		std::vector<ToolExclusion> exclusions;	// tool artifacts excluded from the profile (default_exclusions)
		std::string repo_root = "auto";			// "auto": the first ancestor of the exe folder holding NoVmp.sln and Emulator\data
		std::string out;						// SYNTHETIC profile folder, relative to repo_root (or absolute)
		std::string original_out;				// ORIGINAL profile folder, relative to capture_root (or absolute); "none" = not written
		std::string identity_map = "identity_map.tsv";	// this environment's identity map, relative to out (a profile file)
		std::string peer_identity_map;			// the other environment's identity map, relative to repo_root (the shared USB serials)
		RuleSet rules;
		// U1518: write the synthetic text views (views\<fwid file>) into both profiles (user decision 2026-10-17 "Export
		// synthetic views"; registry last-write times shifted, "Shift by the offset"). BuildConfig::defaults turns it on;
		// in the file "views = yes" (absent = no: a config written before U1518 keeps its meaning and its bytes)
		bool views = false;

		static BuildConfig defaults( Environment e );		// the pinned clean capture of e and the documented default paths
		static const BuildConfig& pinned( Environment e );	// the built-in pin (capture_parent / folder / manifest / companions / firmware ref)
		static std::vector<ToolExclusion> default_exclusions();	// RustDesk Printer, RustDesk, System Informer, DebugView, VmLoader
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
		std::vector<DiskDescriptor> disks;					// disk_<n>_descriptor.bin (fwid >= 1.4), by name
		std::vector<TextFile> texts;						// every *.txt of the capture manifest
		FirmwareRefCheck firmware_ref;						// the firmware entries compared with the reference capture
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
		std::vector<std::pair<std::string, std::string>> strings;	// rule 2 / 2h values, real -> synthetic (rule 8c)
		std::vector<std::string> vendor_needles;	// U1518: the rule-2h memory Manufacturer values (a brand name, also among the needles)
	};
	struct TextIdentityInput
	{
		std::vector<TextFile> files;
		std::vector<std::string> usb_table;
		std::vector<IdentityPreset> presets;
		ExclusionScan exclusions;									// parse_tool_exclusions (empty: nothing excluded)
		std::vector<std::pair<std::string, std::string>> smbios_strings;	// SmbiosGen::strings (rule 8c)
	};
	struct TextIdentityGen
	{
		std::vector<IdentityEntry> identity;
		std::vector<StoragePath> storage_paths;
		std::vector<FieldMapEntry> fields;			// occurrence + relation rows
		std::vector<std::string> needles;
		std::vector<Bytes> binary_needles;
		std::vector<MacPair> macs;					// every adapter MAC, real + synthetic
		std::vector<WinInstallationId> installation_ids;	// rule 8a, real + synthetic in pairs (license.txt)
		std::vector<VolatileItem> volatiles;		// volatile.tsv
		std::vector<ExclusionRange> extra_exclusions;	// capture_info.txt lines excluded with an excluded identifier
		size_t excluded_anchors = 0, excluded_rows = 0;	// identifier anchors / occurrence rows inside excluded ranges (dropped)
		size_t filler_serials = 0;					// disk / HID / USB serials kept as placeholders (one repeated character, ...)
		TimeShiftCheck time_shift;					// rule 7b install-history shift
		V1GuidCheck v1_guids;						// rule 4c version-1 GUID timestamps (U1516)
		// U1518: the synthetic text views (views\<file> of the profile): each presentable text file with every recorded
		// occurrence replaced, excluded tool-artifact lines as a marker line; the original profile's copy without replacement
		std::vector<TextFile> views, views_original;
		size_t view_replacements = 0, view_excluded_lines = 0;
		size_t view_nested = 0;						// occurrences inside another replaced one (checked to agree with it)
		size_t view_shifted_times = 0;				// registry last-write times moved by the install-history offset (views only)
		std::vector<std::string> view_skipped;		// the text files that are no view (run metadata, decoded firmware copies, ...)
	};
	struct DiskInput { std::vector<DiskDescriptor> descriptors; std::vector<IdentityEntry> identity; };
	struct DiskGen
	{
		std::vector<DiskDescriptor> synthetic;
		std::vector<FieldMapEntry> fields;			// R rows
		std::vector<std::string> needles;
		size_t serials = 0, filler = 0, none = 0;	// descriptors with a replaced / placeholder / no serial
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
		std::vector<DiskDescriptor> disk_original, disk_synthetic;
		size_t disk_serials = 0, disk_filler = 0, disk_none = 0;
		std::vector<IdentityEntry> identity;
		std::vector<StoragePath> storage_paths;
		std::vector<FieldMapEntry> fields;	// SMBIOS rows first, then EDID, then disk descriptors, then text rows
		size_t smbios_fields = 0, edid_fields = 0, disk_fields = 0;
		ExclusionScan exclusions;			// incl. the extra capture_info.txt lines
		size_t excluded_anchors = 0, excluded_rows = 0, filler_serials = 0;
		std::vector<VolatileItem> volatiles;
		TimeShiftCheck time_shift;
		V1GuidCheck v1_guids;
		std::vector<TextFile> views, views_original;	// U1518: views\<file> of the synthetic / original profile
		size_t view_replacements = 0, view_excluded_lines = 0, view_nested = 0, view_shifted_times = 0;
		std::vector<std::string> view_skipped;
		std::vector<std::string> needles;	// leak guard
		std::vector<Bytes> binary_needles;
		std::vector<std::string> vendor_needles;	// U1518: rule-2h memory Manufacturer values (brand names, see the leak guard)
		std::vector<MacPair> macs;
	};
	struct GenResult
	{
		bool ok = false;
		std::string error;						// the failure (no identifier value), empty when ok
		BuildLog log;
		BuildConfig config;						// the configuration as used
		Identity identity;						// in memory only
		IdentityMap identity_map;				// the identity as applied / generated (has_install_shift: the original copy's data)
		bool generated = false;					// true: every value was drawn fresh from RDRAND; false: a saved map was applied
		std::map<std::string, Bytes> synthetic, original;	// the files of both profiles (relative path -> bytes)
		std::filesystem::path out, original_out;			// resolved folders (original_out empty: not written)
		bool checked = false;					// run_build( true ): the output folder matched the regeneration
	};

	// ── the synthetic-only view of a profile (gap P1 #8 / #9, U1517): what the ImGui profile builder (emu_profile_builder)
	//    and the MCP shared-memory pipe may see and edit. Built by gen_build into the BuildContext from synthetic values and
	//    metadata ONLY - the identity map's values, the identity rows' id / kind / rule / anchor / count / synthetic value,
	//    the field map's locations and relation rows - never IdentityEntry::original, Identity or GenResult::original (they
	//    hold real values and must never reach the pipe); gen_build re-checks every text of the view against the leak guard ──
	struct FieldInfo
	{
		std::string key;					// the identity-map key (a field location: "identity/M01", "smbios/t1/h0001/uuid", "shared/usb/0",
											// "edid/n01", ...), or "derived/<identity id>" for a value derived at build time (no map entry)
		std::string kind;					// the identity-map kind ("mac", "guid", "uuid", ...) or, derived, the identity kind
		std::string rule;					// the field rule ("1-rfc4122", "2", "3", "4c", "4d", "6", "7b", "8a", ...)
		std::string location;				// the first anchor "<capture file>:<line>", or the blob field ("smbios.bin 0x0063 len 16: ...")
		size_t occurrences = 0;				// occurrence rows of its identities (fieldmap.tsv); blob fields: the field rows
		size_t length = 0;					// characters of the value
		std::vector<std::string> identities;	// the identity.tsv ids the value belongs to
		std::vector<std::string> relations;	// what it follows / what follows it (field-map relation rows, map dependents), no values
		bool editable = false;				// set_value / gen_value accept the key
		std::string not_editable;			// why not (a derived value, the install window, ...)
		const char* value = nullptr;		// the synthetic value, final text form (interned: valid for the process lifetime)
	};
	struct ProfileView
	{
		Environment env = Environment::vmware_guest;
		std::string profile;				// "vmware_guest" | "bare_host"
		bool built = false;					// false: no gen_build on this context yet (no fields)
		std::vector<FieldInfo> fields;		// the identity-map keys (sorted), then the derived identities (identity.tsv order)
	};
	// the guard of one identity-map key's real value (U1517): enough to refuse the real value in set_value, never the value -
	// the SHA-256 of a domain tag + the lower-case real text; with the kind / length rules of draw()
	struct RealValueGuard
	{
		std::string kind;
		size_t length = 0;
		bool same_length = true;
		std::array<uint8_t, 32> digest {};
		unsigned mac_room = 0;				// rule 3 base MACs: the largest family delta the tail must leave room for
	};

	// ── the per-build state (user decision 2026-10-17 "Per-build context", plan 1.J.49.a): everything one build reads or
	//    changes besides the shared state of the singleton. One context per build, used by one thread at a time; two builds
	//    with different contexts may run in parallel. Created by ProfileGen->new_context( cfg ) ──────────────────────────
	struct BuildContext
	{
		BuildConfig config;					// this build's configuration (a copy: later load_config() calls do not change it);
											// run_build replaces it by the portable form it saves next to the outputs
		IdentityMap identity_map;			// this environment's map: application takes every value from it, generation records
											// every drawn value under its field-location key (identity_map.tsv of the profile)
		IdentityMap peer_map;				// the other environment's map: only its "shared/" keys are used (rule 4d)
		bool applying = false;				// identity_map was loaded non-empty: a missing key stops the build, nothing is drawn
		bool fresh = false;					// gen_new_identity(): run_build generates instead of applying the saved map
		std::set<std::string> used_keys;	// the keys this build asked for (application: every map key must be one of them)
		// U1516: application of a map written before the version-1 GUID timestamp rule - a saved v1 value whose timestamp is not
		// real + the install-history offset gets that timestamp (its clock sequence and node kept) instead of stopping the
		// build; every such key is listed in upgraded_keys (emu-profilegen --upgrade-v1-time; never in check mode)
		bool upgrade_v1_time = false;
		std::vector<std::string> upgraded_keys;
		// U1518: application of a map that lacks the keys of identities a newer rule finds (the binary-form version-1 GUIDs):
		// exactly those keys are drawn (generation), every saved value applied; listed in added_keys (emu-profilegen
		// --add-new-keys; never in check mode)
		bool add_new_keys = false;
		std::vector<std::string> added_keys;
		// U1517: the synthetic-only field metadata of the last gen_build (list_fields) and the real-value guards draw() records
		// per key (hashes, never values): the context holds no real value
		std::vector<FieldInfo> view_fields;
		std::map<std::string, RealValueGuard> guards;
	};

	// the builder of both profiles: a singleton like the demo's Randomizer - ProfileGen->new_context( cfg ), then
	// ProfileGen->run_build( ctx ) / ProfileGen->gen_<what>( ctx, struct ). Threading model and lock order: the file header
	class ProfileGenerator
	{
	public:
		static ProfileGenerator& get_instance();
		ProfileGenerator( const ProfileGenerator& ) = delete;
		ProfileGenerator& operator=( const ProfileGenerator& ) = delete;

		// ── shared state (thread-safe) ─────────────────────────────────────────────────────────────────────────────────
		void load_config( const BuildConfig& c );			// the default configuration of later new_context() calls (unique lock)
		BuildConfig config() const;							// a copy of the default configuration (shared lock)
		BuildContext new_context() const;					// a context from the default configuration, no identity loaded
		BuildContext new_context( const BuildConfig& c ) const;	// a context from c, no identity loaded (generation)
		// the default configuration back to the defaults and the shared/ registry emptied (a new identity session for both
		// environments); the value pool is kept: every value already handed out stays valid. Not while a build runs
		void reset();
		void reset_shared_values();							// only the shared/ registry emptied (not while a build runs)
		size_t check_shared_values() const;					// how many shared/ values the registry holds (diagnostics, tests)

		// ── per-build state (ctx) ───────────────────────────────────────────────────────────────────────────────────────
		// APPLY a saved identity: own = this environment's map (non-empty: application - every value from it, a missing key
		// stops the build), peer = the other environment's map (its "shared/" values: one USB serial per device for both)
		void load_identity_maps( BuildContext& ctx, const IdentityMap& own, const IdentityMap& peer );

		// resolve the paths, verify + read the pinned capture, load the identity maps (application) unless a new identity was
		// asked for or none exists yet (generation), gen_build, then write both profiles - or with check = true compare the
		// regeneration (application of the saved map) with the SYNTHETIC output folder byte for byte and write nothing
		GenResult run_build( BuildContext& ctx, bool check = false );
		GenResult run_build( bool check = false );			// the same with a new context of the default configuration
		// the build, no file I/O: both profiles' files (incl. identity_map.tsv, build_config.txt + manifest.txt) and every self-check
		GenResult gen_build( BuildContext& ctx, const CaptureInputs& in, const std::string& resolved_capture_root = {} );
		Identity gen_identity( BuildContext& ctx, const CaptureInputs& in );	// throws on any rule / self-check failure
		// a NEW identity: drop ctx's identity map; the next build draws every value fresh from RDRAND (the "shared/" values still
		// come from the peer map, else from the shared/ registry, so both profiles keep one serial per physical device)
		IdentityMap gen_new_identity( BuildContext& ctx );

		// ── the generation steps (each uses ctx's environment, rules and identity map) ─────────────────────────────────
		SmbiosGen gen_smbios( BuildContext& ctx, const SmbiosInput& in );						// rules 1, 2, 2h, 2o + guard
		SmbiosUuid gen_system_uuid( BuildContext& ctx, const SmbiosUuid& real );				// rule 1 (the MAC tail is applied by gen_smbios)
		SmbiosStringField gen_smbios_string( BuildContext& ctx, const SmbiosStringField& real );	// rules 2 / 2h
		TextIdentityGen gen_text_identity( BuildContext& ctx, const TextIdentityInput& in );	// rules 3, 4a-4d, 7, 8 on the text outputs
		MacAddress gen_mac( BuildContext& ctx, const MacAddress& real );						// rule 3, one base MAC
		MacFamily gen_mac_family( BuildContext& ctx, const MacFamily& real );					// rule 3, bases + re-derived dependents
		Eui64Id gen_eui64_instance_id( BuildContext& ctx, const Eui64Id& real, const MacAddress& synthetic_nic );
		GuidValue gen_guid( BuildContext& ctx, const GuidValue& real, const NodeMap& nodes = {} );	// rule 4c incl. the version-1 node and MBR form
		UsbSerial gen_usb_serial( BuildContext& ctx, const UsbSerial& real );					// rule 4d
		ComputerName gen_computer_name( BuildContext& ctx, const ComputerName& real );			// rule 4a
		TextValue gen_class_string( BuildContext& ctx, const TextValue& real );				// rule-2 classes (7c user name, 8b, 8d, 8e)
		TextValue gen_hex_token( BuildContext& ctx, const TextValue& real );					// rule 4b / 8c: hex digits of the same case and length
		WinInstallationId gen_win_installation_id( BuildContext& ctx, const WinInstallationId& real );	// rule 8a
		ProductId gen_product_id( BuildContext& ctx, const ProductId& real );					// rule 7a
		InstallTime gen_install_time( BuildContext& ctx, const InstallTime& real );			// rule 7b: the install-history offset (map key "identity/install")
		SidParts gen_sid( BuildContext& ctx, const SidParts& real );							// rule 7d
		VolumeId gen_volume_serial( BuildContext& ctx, const VolumeId& real );					// rule 7e
		EdidGen gen_edid( BuildContext& ctx, const EdidInput& in );							// rule 6
		EdidSerial gen_edid_serial( BuildContext& ctx, const EdidSerial& real );
		EdidBlock gen_edid_checksum( BuildContext& ctx, const EdidBlock& block );				// byte 127 so the base block sums to 0 (uses no per-build state)
		DiskGen gen_disk_descriptors( BuildContext& ctx, const DiskInput& in );				// rule 8d on disk_<n>_descriptor.bin
		SusValidation gen_sus_client_id_validation( BuildContext& ctx, const SusValidationInput& in );	// rule 8c, layout kept (no randomness of its own)

		// ── the synthetic-only view and single-value edits (U1517; the listing is list_, not a generator) ─────────────────
		// the fields of ctx's last gen_build, every value from ctx's identity map; empty (built = false) before a build
		ProfileView list_fields( const BuildContext& ctx );
		// ONE value fresh from RDRAND: Randomize->gen_<what> with the current synthetic value as the pattern (same format,
		// OUI, version / variant, digit layout; a version-1 GUID: a new clock sequence - its timestamp and node are derived),
		// then the checks of set_value; returns the field with its new value. The next gen_build( ctx, ... ) applies it
		FieldInfo gen_value( BuildContext& ctx, const std::string& key );
		// set ONE value (final text form): an editable key of the view, the kind / length / format rules of its current value,
		// never the captured value (its guard), unique among the keys of its kind. Map dependents follow (a base MAC's
		// version-1 GUID nodes); build-time dependents (MAC family, UUID tail, EUI-64 form, SusClientIdValidation, SIDs,
		// NameSamCompatible) are re-derived by the next gen_build. Throws naming the key and the rule, never a value
		FieldInfo set_value( BuildContext& ctx, const std::string& key, const char* value );
		// how many texts of the view contain one of needles as a token (the leak guard's matching); 0 = clean
		static size_t check_view( const ProfileView& view, const std::vector<std::string>& needles );

		// ── parsers, loaders, savers, validators (no per-build state) ──────────────────────────────────────────────────
		static std::vector<std::string> parse_usb_serial_table( const std::vector<Bytes>& enum_registry_texts );
		static InstallWindow parse_install_window( unsigned os_build, const std::string& capture_utc );
		static ExclusionScan parse_tool_exclusions( const std::vector<TextFile>& files, const std::vector<ToolExclusion>& items );
		static SusValidationLayout parse_sus_client_id_validation( const Bytes& blob );
		// license.txt (fwid 1.4): one WinInstallationId per SoftwareLicensingProduct row (text + source + the row's license data;
		// the text lives in the generator's interned value pool)
		std::vector<WinInstallationId> parse_license_rows( const TextFile& license );
		static CaptureInputs load_capture( const std::filesystem::path& capture_folder, const std::filesystem::path& capture_root, const BuildConfig& c, BuildLog& log );
		// identity_map.tsv: "key<TAB>kind<TAB>value" rows; the original copy adds the "install_shift" line (never the repository copy)
		static IdentityMap parse_identity_map( const std::string& text );	// throws naming the line
		static std::string save_identity_map( const IdentityMap& map, Environment env, bool original );
		// one identity_map.tsv of a profile folder, read with full sharing; while that folder is being switched by save_profile
		// the complete previous map ("<dir>.old") or the complete new one is returned (U1515). Throws on a malformed map
		static IdentityMapFile load_identity_map( const std::filesystem::path& file );
		// writes the files (relative path -> bytes) atomically (U1515, see the header): "<dir>.partial" (CREATE_NEW), then the
		// switch by rename, the previous folder kept as "<dir>.old" until the switch succeeded, then exactly its listed files
		// removed. Refused (throws) before anything is created: an ORIGINAL profile inside a git work tree; an existing dir
		// that is no validated profile folder (check_profile_dir) or that holds a file which is not part of the profile (stale
		// output); an existing "<dir>.partial" / "<dir>.old" (an interrupted run: reported, never deleted)
		static ProfileSaveResult save_profile( const std::filesystem::path& dir, const std::map<std::string, Bytes>& files, bool original );
		static ProfileDirCheck check_profile_dir( const std::filesystem::path& dir );
		static bool check_filler( const std::string& s );
		static bool check_usb_placeholder( const std::string& s );			// one repeated character (all '0', ...), or "MSFT30" + zeros
		// the folder holding a ".git" entry (directory or worktree file) at p or above it; empty = not in a git work tree
		static std::filesystem::path check_git_work_tree( const std::filesystem::path& p );
		static std::vector<ByteRange> check_diff_ranges( const Bytes& a, const Bytes& b );
		// the CHID-relevant SMBIOS fields: how many were compared, how many differ (must be 0)
		struct ChidCheck { size_t checked = 0, changed = 0; };
		static ChidCheck check_chid_fields( const Bytes& original, const Bytes& synthetic );
		// every one of the 9 groups of 7 digits ends with its check digit (d0 + 2 d1 + d2 + 2 d3 + d4 + 2 d5) mod 7
		static bool check_installation_id( const char* text );
		// the fields of an RFC 4122 version-1 GUID text (with or without braces); ok = false for any other GUID (U1516)
		static GuidV1 parse_guid_v1( const char* text );

	private:
		ProfileGenerator();
		MacAddress gen_mac_with_room( BuildContext& ctx, const MacAddress& real, unsigned max_delta );	// gen_mac with room for a MAC family's largest delta
		// the value of key: ctx's identity map, then (a "shared/" key) ctx's peer map; application without one stops;
		// generation takes a "shared/" key from the shared/ registry or registers fn() (a Randomize->gen_ result) there
		// (insert-if-absent), any other key records fn() in ctx's map. kind / length (same_length) and "not the real value"
		// are checked either way. The result is interned (keep): valid for the process lifetime
		const char* draw( BuildContext& ctx, const std::string& key, const char* kind, const std::string& real, bool same_length, const std::function<const char*()>& fn );
		const char* keep( const std::string& value );	// intern value in the pool (pool_mutex_), stable for the process lifetime
		// U1517: the synthetic-only view of ctx's last build (gen_build), from ctx's identity map and the synthetic side of p
		void save_field_view( BuildContext& ctx, const Identity& p );
		// U1517: why value cannot be key's value (kind / length / format / the real value / not unique), empty = acceptable
		std::string check_value( const BuildContext& ctx, const std::string& key, const std::string& value ) const;
		// one value of the shared/ registry
		struct SharedValue
		{
			std::string kind;					// the identity-map kind of the key (rule 4d: "usb_serial")
			const char* value = nullptr;		// interned (keep): the synthetic value both environments use
		};
		mutable std::shared_mutex config_mutex_;	// guards default_config_ (leaf lock)
		BuildConfig default_config_;				// the configuration of new_context() without an argument
		std::mutex pool_mutex_;						// guards pool_ (leaf lock)
		std::set<std::string> pool_;				// the interned values: node-based, never erased
		mutable std::mutex shared_mutex_;			// guards shared_ (leaf lock)
		std::map<std::string, SharedValue> shared_;	// "shared/<kind>/<index>" key -> its value for both environments (this process)
	};

	// the global handle (the demo's spelling): ProfileGen->gen_mac( ... ), ProfileGen.get_instance().gen_mac( ... )
	struct ProfileGenHandle
	{
		static ProfileGenerator& get_instance() { return ProfileGenerator::get_instance(); }
		ProfileGenerator* operator->() const { return &ProfileGenerator::get_instance(); }
	};
	inline constexpr ProfileGenHandle ProfileGen {};
}
