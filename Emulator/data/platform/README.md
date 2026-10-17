# Platform identity profiles (plan 1.J.49, ledger U1510-U1512; fwid 1.4 rebuild U1513-U1514; per-build context + threading 1.J.49.a)

The firmware and device identity the emulator presents for `--guest` / `--vm` (the VMware guest) and `--host` (the bare
i5-13600K host): packer HWID hashes and anti-VM checks must see ONE consistent, real-looking machine, so the internal
relations of the capture are kept while every real identifier is replaced. Built by `ProfileGenerator`
(`Emulator\tools\profilegen\profile_generator.hpp` / `.cpp`, the builder of both profiles - comparable to per-CPU
definition files) through the console tool `emu-profilegen` (`build\x64\Release\tests\emu-profilegen.exe`). Design: the
user's rid demo - ProfileGenerator is a singleton reached as `ProfileGen->gen_<what>( ctx, struct )` with the build's
`BuildContext` (`ProfileGen->new_context( cfg )`, see "Threading" below), and the ONE random source is the Randomizer
singleton `Randomize->gen_<what>()`
(`pg_randomize.hpp` / `.cpp`) over RDRAND in `pg_rand.asm` (MASM, x64; CPUID.1:ECX[30] check, 10 retries, 0 / ~0 rejected;
no fallback: without RDRAND a new identity fails cleanly). Values travel as `const char*` in their final text form. The
class is UI-free (no console I/O; it returns a `GenResult` with a structured `BuildLog`); `pg_main.cpp` is a thin CLI over
it. A later ImGui front end will load and save the same build information (`build_config.txt`, `identity_map.tsv`) and
call the same `gen_` members (no UI code yet).

**Generation and application.** The first build of an environment (or `--new-identity`) GENERATES the identity: every
synthetic value is drawn live from RDRAND and recorded in `identity_map.tsv` under a field-location key. Every later build
(and test.cmd's `pg_profile_check_*`) APPLIES that map to the clean capture - deterministic, byte for byte, no randomness;
a map that does not fit the capture stops the build. The USB serials both profiles share are `shared/` keys taken from the
other environment's map. There is no seed.

## Folders and files

| path | what it is |
|---|---|
| `vmware_guest\` | the `--guest` profile, built ONLY from the clean VMware guest capture without the VmLoader driver (fwid 1.4 Run3b, 2026-10-16; its firmware is checked byte-identical to the fwid 1.2 Run 3 capture on every build) |
| `bare_host\` | the `--host` profile, built ONLY from the clean bare-host capture (fwid 1.4, 2026-10-16; its firmware is checked byte-identical to the fwid 1.3 host capture on every build) |
| `<env>\smbios.bin` | the SMBIOS table in `RawSMBIOSData` form (`GetSystemFirmwareTable('RSMB')`: 8-byte header, then the structures), byte-exact except the field-map ranges |
| `<env>\acpi\*.bin` | every ACPI table of the capture (`acpi_*.bin` firmware-table provider, `hwacpi_*.bin` `HKLM\HARDWARE\ACPI` copies), byte-exact, never modified |
| `<env>\edid\edid_<n>.bin` | (fwid >= 1.3) every monitor EDID value; only the serial fields replaced, the base-block checksum recomputed |
| `<env>\disk\disk_<n>_descriptor.bin` | (fwid >= 1.4) the raw `STORAGE_DEVICE_DESCRIPTOR` of `\\.\PhysicalDrive<n>`; only the serial string at `SerialNumberOffset` replaced (rule 8d) |
| `<env>\identity.tsv` | the identifiers of the fwid text outputs with their synthetic values (MACs and EUI-64 forms, Bluetooth addresses, computer name, USB / HID serials, GUIDs incl. software-device instance GUIDs and audio endpoints, EDID serials, OS identifiers, installation IDs, SusClientIdValidation, disk serials), plus the Enum\STORAGE\Volume instance paths with the replacement applied |
| `<env>\fieldmap.tsv` | every replaced field as LOCATION + LENGTH + RULE (never a value): blob rows (byte offset in `smbios.bin` / `edid\edid_<n>.bin` / `disk\disk_<n>_descriptor.bin`), text rows (file:line:column of every occurrence in the capture), relation rows (`3r`, `4cr`, `8cr`, `8dr`; offset `-`) naming what a derived value follows |
| `<env>\exclusions.tsv` | the capture ranges of tool artifacts that are NOT part of the presented machine (item, file, first / last line, unit); a presenter omits them |
| `<env>\volatile.tsv` | the captured values the emulator may advance (file, line, name, mark; never a value): every line fwid marks `[volatile` and the DiagTrack counters / timestamps |
| `<env>\identity_map.tsv` | the identity this profile presents: every synthetic value (final text form) by key - a field location (environment + field id, e.g. `identity/G05`, `smbios/t1/h0001/uuid`, `shared/usb/3`), never a value; no real value. The install-history offset is NOT here: only the local original profile's copy carries it (offset + synthetic times would give the real times). |
| `<env>\build_config.txt` | the BuildConfig of the build: every input (environment, capture folder pattern + pinned manifest SHA-256, companion hash lists, the peer capture for the shared USB table, the firmware reference capture, the tool exclusions, output paths, the identity map and the peer identity map, enabled rules, tool version). No identifier values, no user-specific absolute paths. With the identity map a build is reproducible from it byte for byte. |
| `<env>\manifest.txt` | SHA-256 of every file, provenance (capture folder relative to the capture root with the computer-name part withheld, manifest SHA-256, companions, peer, firmware reference with the identical-entry count), tool version, rules, optional inputs present, summary counts (SMBIOS, EDID, disks, identity, exclusions, volatile); bare host also `test_signing_certificate_present` |

## Generators (`ProfileGen->gen_<what>( ctx, ... )`, each takes the build's context and a named struct, returns a named struct)

`gen_build` (all files of both profiles + self-checks) / `gen_identity` (the whole identity) / `gen_smbios` /
`gen_system_uuid` / `gen_smbios_string` / `gen_text_identity` / `gen_mac` / `gen_mac_family` / `gen_eui64_instance_id` /
`gen_guid` (incl. the version-1 node and MBR form) / `gen_usb_serial` / `gen_computer_name` / `gen_class_string` /
`gen_hex_token` / `gen_win_installation_id` (with `WinLicenseInfo`) / `gen_product_id` / `gen_install_time` / `gen_sid` /
`gen_volume_serial` / `gen_edid` / `gen_edid_serial` / `gen_edid_checksum` / `gen_disk_descriptors` (`DiskInput` ->
`DiskGen`) / `gen_sus_client_id_validation` (`SusValidationInput` -> `SusValidation`) / `gen_new_identity( ctx )` (a new identity).
Each draw goes through the identity map: generation asks `Randomize->gen_<what>()` (`gen_like`, `gen_hex_like`,
`gen_usb_serial_like`, `gen_computer_name`, `gen_mac_like`, `gen_guid_like`, `gen_system_uuid_like`, `gen_edid_serial_like`,
`gen_sid_like`, `gen_installation_id_like`, `gen_decimal_in_range`, ...) for a finished value, application returns the saved one.
Non-generators: `run_build( ctx, check )` (and `run_build( check )` with a new context of the default configuration), `new_context`, `config`, `reset`,
`reset_shared_values`, `check_shared_values`, `parse_smbios`, `parse_usb_serial_table`, `parse_install_window`, `parse_tool_exclusions`
(`ToolExclusion` list -> `ExclusionScan`), `parse_sus_client_id_validation`, `parse_license_rows`, `parse_identity_map`,
`save_identity_map`, `load_capture`, `load_config`, `load_identity_maps`, `save_profile`, `check_filler`,
`check_usb_placeholder`, `check_git_work_tree`, `check_diff_ranges`, `check_chid_fields`, `check_installation_id`.

## Threading and the per-build context (plan 1.J.49.a, user decision 2026-10-17 "Per-build context")

Everything one build changes lives in a named struct `BuildContext`: its `config` (a copy of the BuildConfig), its own
`identity_map`, the `peer_map` (the other environment's map), `applying`, `fresh` and the `used_keys`. One build = one
context, used by one thread at a time; two builds with different contexts (`--guest` and `--host`) can run in parallel
threads. `emu-profilegen` creates one context per invocation:

```
BuildContext ctx = ProfileGen->new_context( cfg );        // no identity loaded: generation
ProfileGen->load_identity_maps( ctx, own, peer );         // application of saved maps (or ProfileGen->gen_new_identity( ctx ))
GenResult r = ProfileGen->run_build( ctx, check );        // or ProfileGen->gen_build( ctx, capture ): no file I/O
```

The singleton keeps only shared state, each part behind its own lock: the default BuildConfig (`load_config` /
`config()` returns a copy; `std::shared_mutex`), the interned value pool (`std::mutex`: every `const char*` a `gen_` step
returns points into it - valid for the process lifetime, also after its context is destroyed or its map replaced) and the
process-wide registry of `shared/` values (`std::mutex`): when both environments GENERATE concurrently, the first build
that needs a `shared/` key records it and the other takes the same value, so one physical USB device keeps one serial in
both profiles (the peer map on disk still has precedence; application never consults the registry; `reset()` /
`reset_shared_values()` start a new identity session and must not run while a build runs). Lock order: the three locks
are leaves - no code path holds two at once, none is held while calling `Randomize->` (a value is drawn, interned, then
registered insert-if-absent; a concurrent first writer wins) or during file I/O. The Randomizer is thread-safe and
lock-free: RDRAND is per-thread safe, the byte pool and every result buffer are `thread_local` (a result is valid until the
same `gen_` runs again on the same thread). Files: two `run_build` calls that write both profiles at the same time read
each other's `identity_map.tsv` (peer map) at their start while the other may write it at its end - run parallel builds
with `--check`, or generate both into memory (`gen_build`) and write afterwards. Unit tests: `pg_randomizer_threads`,
`pg_parallel_builds`, `pg_concurrent_fresh_shared`.

## Field rules and the replaced locations

User decisions 2026-10-16 (Identifier_fields.md sections C-J); everything not listed is byte-exact from the capture. Full
rule text with every location: the comment block at the top of `profile_generator.hpp`. Offsets are absolute byte offsets
in `smbios.bin`. Counts: identities of `identity.tsv` (occurrence rows in `fieldmap.tsv`).

| rule | what | vmware_guest (SMBIOS 2.7, 189 structures) | bare_host (SMBIOS 3.6, 105 structures) |
|---|---|---|---|
| 1 | type 1 UUID (formatted offset 0x08, 16 bytes) | handle 0x0001: 0x0066, 16 bytes; VMware layout, bytes 0-1 (VMware prefix) kept; no UUID-MAC relation | handle 0x0001: 0x0063, 16 bytes; version nibbles (bytes 6 / 7) and variant bits (byte 8) as in the real UUID; bytes 10-15 = the synthetic MAC of the onboard wired NIC (UUID-MAC relation kept) |
| 2 | real serial numbers / asset tags, same length + character class per position | type 17 handle 0x0031 SerialNumber, string 2: 0x09BB, 8 bytes (VMware's sequence-style module serial: synthetic, not filler - decision 2026-10-16) | type 2 handle 0x0002 SerialNumber, string 4: 0x012A, 18 bytes |
| 2h | bare host only: type 17 DeviceLocator / BankLocator / Manufacturer / PartNumber, rule-2 classes | not applied: every type 17 string stays VMware's own | handles 0x003B / 0x003D: DeviceLocator 0x07FE / 0x0928 (18), BankLocator 0x0811 / 0x093B (6); handles 0x003C / 0x003E: DeviceLocator 0x0880 / 0x09AA (18), BankLocator 0x0893 / 0x09BD (6), Manufacturer 0x089A / 0x09C4 (7), PartNumber 0x08B6 / 0x09E0 (20) |
| 2o | unreferenced string equal to a rule-2 / 2h value | none | none |
| 3 | MACs: OUI exact, last 3 bytes synthetic, one synthetic MAC per NIC / radio, dependents re-derived; VMnet (00:50:56:C0:00:n) exact; remote Bluetooth addresses OUI kept, the local radio = its adapter MAC | 1 MAC | 6 MACs (Wi-Fi Direct = Wi-Fi + 1, Wi-Fi Direct #2 = locally administered variant of Wi-Fi, Bluetooth PAN = Wi-Fi + 4 = the local radio incl. `ServicesFor<address>`; Hyper-V OUI kept; the I226-V EUI-64 PCI instance ID re-derived), 3 paired Bluetooth devices |
| 4a | computer name: `DESKTOP-` + 7 synthetic [A-Z0-9] | 1 | 1 |
| 4b | Enum\SCSI instance IDs "N&hash&N&address": PnP parent-prefix hashes - EXACT (decision 2026-10-16; topology-derived, not unique), like every PnP instance hash (also inside HID device paths); another SCSI instance form stops the build | exact | exact |
| 4c | GUIDs (container, interface - also the Wi-Fi interface -, volume incl. os_ids volume GUID paths, software-device instance GUIDs in Enum\SWD - braced or not, audio endpoints, MachineGuid / HwProfileGuid / SQM MachineId / SusClientId, a WSD printer port's GUID, the GPU DriverState device GUID, version-1 GUIDs with an adapter-MAC node): format, case, version digit and variant bits kept; one mapping per real GUID everywhere; v1 node = synthetic MAC, Windows' no-NIC node exact; MBR-style volume GUIDs: only the disk signature | 48 (20 container, 9 interface, 8 volume, 7 SWD, MachineGuid, HwProfileGuid, SQM MachineId, SusClientId) | 206 (53 container, 48 interface, 16 volume, 46 SWD incl. the WSD printer port's GUID, 38 audio endpoints, MachineGuid, HwProfileGuid, SQM MachineId, SusClientId, the GPU DriverState GUID) |
| 4d | USB device serials (+ HID serials): same length + classes, hex-encoded ASCII stays hex-encoded ASCII; ONE synthetic serial per physical device in both profiles (shared table: bare-host capture, then guest capture); a HID serial = its USB serial identity; placeholders (one repeated character, `MSFT30` + zeros) exact | 2 | 6 (the 3 real HID serials are USB serials; 7 HID placeholder serials exact) |
| 6 | EDID (fwid >= 1.3): serial field + FFh serial string, one synthetic serial per physical monitor, checksum recomputed; model / name / week / year exact | no EDID (VMware display) | 7 EDID values: 9 serial fields / strings + 7 checksums |
| 7 | os_ids.txt (fwid >= 1.3): 7a ProductId middle groups; 7b the install history shifted TOGETHER by one offset (RDRAND at generation, whole seconds, at least one day earlier; only the local original identity map holds it): InstallDate / InstallTime, the per-device first-install / install / last-arrival dates (`pci_devices.txt`, FILETIME + ISO), the SQM first session, the DiagTrack FILETIME timestamps, the Bluetooth LastSeen / LastConnected / FingerprintTimestamp - real ordering and spacing exact, every event between the build release and the capture (self-check); 7c user name everywhere + NameSamCompatible; 7d SIDs (sub-authorities synthetic, RID exact, user / machine consistent); 7e volume serials | ProductId, 56 install-history values (InstallDate / InstallTime, 24 device install / 13 last-arrival FILETIMEs, 13 ISO forms, SQM, 3 DiagTrack), user name, NameSamCompatible, user SID + machine SID, 2 volume serials | ProductId, 89 install-history values (InstallDate / InstallTime, 42 device install / 13 last-arrival FILETIMEs, 22 ISO forms, SQM, 3 DiagTrack, 6 Bluetooth; 34 precede InstallDate in the capture and in the profile - Windows setup), user name, NameSamCompatible, user SID + machine SID, 8 volume serials |
| 8 | fwid 1.4: 8a installation ID per license row (9 groups of six random digits + the check digit (d0 + 2 d1 + d2 + 2 d3 + d4 + 2 d5) mod 7 - valid in all 6 real IDs too; SKU / name / channel / status exact); 8c SusClientIdValidation layout kept (disk serial, MAC, board serial = their synthetic values); 8d disk serials (disks.txt + descriptor blobs; a USB flash drive's = the start of its USB serial's synthetic value; repeated-digit placeholder exact); GPU `UserModeDriverGUID` / driver-store paths exact | 1 installation ID, SusClientIdValidation (its MAC; the strings are filler), 2 disks without a serial | 5 installation IDs, SusClientIdValidation (disk serial, MAC, board serial), 6 disk serials (2 placeholder disks exact; 1 = the start of a USB serial) |

Filler that is never replaced: "Default string", "To Be Filled By O.E.M.", "None", "No Asset Tag", "N/A",
"Not Specified", "Unknown", "NO DIMM", "Empty", "Not Available", "Not Installed", "Undefined", the placeholder values
"00000000" and "9876543210", any value of one repeated character (length >= 2; decision J) (case-insensitive, trailing
blanks ignored), empty strings, fields without a string; the "no container" GUID; USB / HID placeholders. A real value in
another serial / asset-tag / SKU field (types 1, 2, 3, 4, 39) and an identifier line fwid marks `[IDENTIFIER` (outside its
`#` comments; `[IDENTIFIER?]` marks the line above it) that no rule consumes stop the build as undecided. Exact by decision
as well: the guest UUID's VMware layout, the .vmx disk vendor / product strings, NIC OUIs, PnP instance hashes, volume
labels, EDID model codes, CHIDs (the CHID-relevant SMBIOS fields are checked unchanged), Secure Boot / TPM, time zone,
locale, keyboard layouts, domain / workgroup, RAM, screen metrics, Wi-Fi counts, printer / driver names, HID / Bluetooth /
audio / GPU names, DiagTrack / SQMClient values (the DiagTrack counters / timestamps listed in `volatile.tsv`; their
FILETIME timestamps and the SQM first session move with the install history, rule 7b), driver dates, registry last-write
and code-integrity cache times, VMware
Workstation's host-side services and VMnet adapters (`--host`). The certificate stores are not part of the profile
(`certificates.txt` is never searched). ACPI tables are searched for every real identifier; a hit is only reported. Both
clean captures: 0 hits.

## Tool artifacts (BuildConfig `exclude`)

Tool artifacts of the capture setup are not part of the presented machine (decision J). The BuildConfig carries the list,
one line per item: `exclude = <name> | <pattern>; <pattern> | <reason>` (case-insensitive patterns). Defaults: RustDesk
Printer (`RustDesk Printer`, `RustDesk v4 Printer Driver`), RustDesk (`RustDesk`: its service and any driver / registry /
device trace), System Informer (`SystemInformer`), DebugView (`Dbgv`), VmLoader (`vmloader`). A unit is named after the
first list item any of its lines matches. A pattern hit outside `#` comments of a capture text file (`capture_info.txt` = run metadata and
`certificates.txt` are not scanned) excludes its unit: a quoted list item or numbered list line alone; else its `## ` /
`=== ` block; a registry key block with all its subkey blocks and the parent block's subkey line. A block that records the
item as absent ("exists: no", "not present", "(not found)") is not excluded. Identifiers anchored only inside the ranges are
dropped from `identity.tsv` (their values stay in the leak guard); if such a value occurs outside the ranges the build
stops - except in `capture_info.txt`, whose lines are excluded with it.

| profile | excluded |
|---|---|
| vmware_guest | RustDesk Printer: its Enum\SWD\PRINTENUM key + the parent's subkey line, its pci_devices.txt device block, its printers.txt block, 24 capture_info.txt run-metadata lines naming its device; RustDesk: the `RustDesk` services_vm.txt list item; System Informer: 2 loaded_drivers.txt lines (SystemInformer.sys, ksi.dll), the `KSystemInformer` services_vm.txt list item. DebugView, VmLoader: nothing present (3 VmLoader absence records). |
| bare_host | RustDesk: the `RustDesk` services_vm.txt list item. Nothing else present (3 VmLoader absence records). |

## Provenance

| profile | capture (relative to the capture root) | capture manifest SHA-256 | firmware reference |
|---|---|---|---|
| vmware_guest | `baseline\Run3b_clean_guest_fwid14_20261016\<computer-name>_20261016_171556_vmware-guest` (+ companions `baseline\Run3b_clean_guest_fwid14_20261016.sha256`, `baseline\Run3_clean_guest_probes_20261015.sha256`) | 2828D5201670AFFF41FAD5027E90C113A310AE2F8CAA11F59282FA7FB79F526D | `fwid\baseline\Run3_clean_guest_*` (manifest 57268E68D23B488C74AC0ABA57EDD275DCEC9680153367A8C4A8786A30AE8E1C): 15 of 15 firmware entries identical |
| bare_host | `baseline\Host_clean_20261016_fwid14\<computer-name>_20261016_181322_native-host` (+ `baseline\Host_clean_20261016_fwid14.sha256`, `baseline\Host_clean_20261016.sha256`, `baseline\Host_clean_20261016_xinuse_all_lps.sha256`) | FF2062E2E72AB2138CF4BD08942028E0E8D43E5F0FACBC105DB449E92CEC1C64 | `baseline\Host_clean_20261016_fwid13\*` (manifest C9B008D860BEB95F3E7BFD5724FE6670A01EC4024E9C5471D7EF34851D706B89): 53 of 53 firmware entries identical |

Both capture manifests, the companion hash lists and the firmware references are pinned (`BuildConfig::pinned`): a build
refuses any other capture, re-verifies every manifest entry (size + SHA-256) and compares every firmware entry (rsmb /
acpi / hwacpi / edid) with the reference capture's pinned manifest. Each build also reads the OTHER environment's pinned
capture (`peer_capture_*`; its manifest and `enum_registry.txt` verified) for the USB serial table both profiles share.

## Regenerate, reproduce, new identity

```
emu-profilegen --guest | --vm | --host [--capture-root DIR] [--out DIR] [--original-out DIR | --no-original] [--rules LIST]
emu-profilegen --config Emulator\data\platform\<env>\build_config.txt [--check]
emu-profilegen --guest --new-identity          (GENERATE: every value fresh from RDRAND; a different, equally consistent identity)
emu-profilegen --host --save-config FILE       (write the effective BuildConfig and stop)
```

- Capture root: `--capture-root`, else `NOVMP_CAPTURE_ROOT`, else the first ancestor folder `capture` of the repository
  that holds the input (as `Emulator\tools\bare_host_cpuid.py`).
- Default: APPLY the profile's `identity_map.tsv` (deterministic); without one the build GENERATES a new identity.
  `--check` applies the map in memory and compares every file byte for byte (test.cmd suites `pg_profile_check_guest` /
  `pg_profile_check_host`); it never draws randomness.
- `--new-identity` (`ProfileGen->gen_new_identity( ctx )`): every mapped field gets another value while every relation (MAC
  family, UUID tail, SIDs, install-history spacing, one serial per monitor / USB device, SusClientIdValidation components,
  the flash drive's disk serial) still holds (unit test `pg_new_identity`). The shared USB serials come from the other
  environment's map, so one physical device keeps one serial in both profiles.

## The original profile stays local

The ORIGINAL profile (same layout, the real values) is written to `<capture root>\profiles_original\<env>\` (or
`--original-out`), never into the repository: the tool refuses any path inside a git work tree (a `.git` entry in the path
or an ancestor), and the repository's `.gitignore` ignores any `profiles_original` folder. Its `build_config.txt` also
records the resolved capture root and folder. Never commit, copy or paste values from it.
