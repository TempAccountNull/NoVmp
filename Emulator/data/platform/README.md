# Platform identity profiles (plan 1.J.49, ledger U1510-U1512)

The firmware and device identity the emulator presents for `--guest` / `--vm` (the VMware guest) and `--host` (the bare
i5-13600K host): packer HWID hashes and anti-VM checks must see ONE consistent, real-looking machine, so the internal
relations of the capture are kept while every real identifier is replaced. Built by `ProfileGenerator`
(`Emulator\tools\profilegen\profile_generator.hpp` / `.cpp`, the builder of both profiles - comparable to per-CPU
definition files) through the console tool `emu-profilegen` (`build\x64\Release\tests\emu-profilegen.exe`). The class is
UI-free (no console I/O; it returns a `GenResult` with a structured `BuildLog`); `pg_main.cpp` is a thin CLI over it. A
later ImGui front end will load and save the same build information (`build_config.txt`) through `BuildConfig::load` /
`save` and call the same `gen_` members (no UI code yet).

## Folders and files

| path | what it is |
|---|---|
| `profilegen_seed.txt` | the seed store: 32 bytes from `BCryptGenRandom`, created on the first run. Every synthetic value is an HMAC-SHA256 stream keyed with the seed; the stream labels name a field location (environment + field id), never a value. Not derived from host data. |
| `vmware_guest\` | the `--guest` profile, built ONLY from the clean VMware guest capture without the VmLoader driver (Run 3, fwid 1.2) |
| `bare_host\` | the `--host` profile, built ONLY from the clean bare-host capture (fwid 1.3, 2026-10-16; firmware byte-identical to the fwid 1.2 host capture) |
| `<env>\smbios.bin` | the SMBIOS table in `RawSMBIOSData` form (`GetSystemFirmwareTable('RSMB')`: 8-byte header, then the structures), byte-exact except the field-map ranges |
| `<env>\acpi\*.bin` | every ACPI table of the capture (`acpi_*.bin` firmware-table provider, `hwacpi_*.bin` `HKLM\HARDWARE\ACPI` copies), byte-exact, never modified |
| `<env>\edid\edid_<n>.bin` | (fwid >= 1.3) every monitor EDID value; only the serial fields replaced, the base-block checksum recomputed |
| `<env>\identity.tsv` | the identifiers of the fwid text outputs with their synthetic values (MACs and EUI-64 forms, computer name, USB / HID serials, GUIDs incl. software-device instance GUIDs, EDID serials, OS identifiers), plus the Enum\STORAGE\Volume instance paths with the replacement applied |
| `<env>\fieldmap.tsv` | every replaced field as LOCATION + LENGTH + RULE (never a value): blob rows (byte offset in `smbios.bin` / `edid\edid_<n>.bin`), text rows (file:line:column of every occurrence in the capture), relation rows (`3r`, `4cr`; offset `-`) naming by adapter description what a derived value follows |
| `<env>\build_config.txt` | the BuildConfig of the build: every input (environment, capture folder pattern + pinned manifest SHA-256, companion hash lists, the peer capture for the shared USB table, output paths, the seed, enabled rules, tool version). No identifier values, no user-specific absolute paths. A build is reproducible from it byte for byte. |
| `<env>\manifest.txt` | SHA-256 of every file, provenance (capture folder relative to the capture root with the computer-name part withheld, manifest SHA-256, companions, peer), tool version, rules, optional inputs present, summary counts; bare host also `test_signing_certificate_present` |

## Generators (`ProfileGen->gen_<what>`, each takes and returns a named struct)

`gen_build` (all files of both profiles + self-checks) / `gen_identity` (the whole identity) / `gen_smbios` /
`gen_system_uuid` / `gen_smbios_string` / `gen_text_identity` / `gen_mac` / `gen_mac_family` / `gen_eui64_instance_id` /
`gen_guid` (incl. the version-1 node and MBR form) / `gen_usb_serial` / `gen_computer_name` / `gen_class_string` /
`gen_hex_token` / `gen_win_installation_id` (with `WinLicenseInfo`) / `gen_product_id` / `gen_install_time` / `gen_sid` /
`gen_volume_serial` / `gen_edid` / `gen_edid_serial` / `gen_edid_checksum` / `gen_new_seed` (a new identity). Non-generators:
`run_build`, `parse_smbios`, `parse_usb_serial_table`, `parse_install_window`, `load_capture`, `load_seed`, `save_seed`,
`save_profile`, `check_filler`, `check_usb_placeholder`, `check_git_work_tree`, `check_diff_ranges`, `check_chid_fields`.

## Field rules and the replaced locations

User decisions 2026-10-16; everything not listed is byte-exact from the capture. Full rule text with every location: the
comment block at the top of `profile_generator.hpp`. Offsets are absolute byte offsets in `smbios.bin`.

| rule | what | vmware_guest (SMBIOS 2.7, 189 structures) | bare_host (SMBIOS 3.6, 105 structures) |
|---|---|---|---|
| 1 | type 1 UUID (formatted offset 0x08, 16 bytes) | handle 0x0001: 0x0066, 16 bytes; VMware layout, bytes 0-1 (VMware prefix) kept; no UUID-MAC relation | handle 0x0001: 0x0063, 16 bytes; version nibbles (bytes 6 / 7) and variant bits (byte 8) as in the real UUID; bytes 10-15 = the synthetic MAC of the onboard wired NIC (UUID-MAC relation kept) |
| 2 | real serial numbers / asset tags, same length + character class per position | type 17 handle 0x0031 SerialNumber, string 2: 0x09BB, 8 bytes (VMware's sequence-style module serial: synthetic, not filler - decision 2026-10-16) | type 2 handle 0x0002 SerialNumber, string 4: 0x012A, 18 bytes |
| 2h | bare host only: type 17 DeviceLocator / BankLocator / Manufacturer / PartNumber, rule-2 classes | not applied: every type 17 string stays VMware's own | handles 0x003B / 0x003D: DeviceLocator 0x07FE / 0x0928 (18), BankLocator 0x0811 / 0x093B (6); handles 0x003C / 0x003E: DeviceLocator 0x0880 / 0x09AA (18), BankLocator 0x0893 / 0x09BD (6), Manufacturer 0x089A / 0x09C4 (7), PartNumber 0x08B6 / 0x09E0 (20) |
| 2o | unreferenced string equal to a rule-2 / 2h value | none | none |
| 3 | MACs: OUI exact, last 3 bytes synthetic, one synthetic MAC per NIC / radio, dependents re-derived; VMnet (00:50:56:C0:00:n) exact | 1 MAC | 6 MACs (Wi-Fi Direct = Wi-Fi + 1, Wi-Fi Direct #2 = locally administered variant of Wi-Fi, Bluetooth PAN = Wi-Fi + 4; Hyper-V OUI kept; the I226-V EUI-64 PCI instance ID re-derived) |
| 4a | computer name: `DESKTOP-` + 7 synthetic [A-Z0-9] | 1 | 1 |
| 4b | Enum\SCSI instance IDs "N&hash&N&address": PnP parent-prefix hashes - EXACT (decision 2026-10-16; topology-derived, not unique), like every PnP instance hash; another SCSI instance form stops the build | exact | exact |
| 4c | GUIDs (container, interface, volume incl. os_ids volume GUID paths, software-device instance GUIDs in Enum\SWD - braced or not, MachineGuid / HwProfileGuid / SQM, version-1 GUIDs with an adapter-MAC node): format, case, version digit and variant bits kept; one mapping per real GUID everywhere; v1 node = synthetic MAC, Windows' no-NIC node exact; MBR-style volume GUIDs: only the disk signature | 44 (21 container, 9 interface, 6 volume, 8 SWD) | 167 (53 container, 48 interface, 16 volume, 46 SWD, MachineGuid, HwProfileGuid, SQM MachineId, 1 version-1 GUID found by its MAC node) |
| 4d | USB device serials (+ HID serials): same length + classes, hex-encoded ASCII stays hex-encoded ASCII; ONE synthetic serial per physical device in both profiles (shared table: bare-host capture, then guest capture); placeholders exact | 2 | 6 |
| 6 | EDID (fwid >= 1.3): serial field + FFh serial string, one synthetic serial per physical monitor, checksum recomputed; model / name / week / year exact | not in the capture (fwid 1.2) | 7 EDID values: 9 serial fields / strings + 7 checksums |
| 7 | os_ids.txt (fwid >= 1.3): 7a ProductId middle groups; 7b one install instant between the OS build release and the capture (InstallDate = InstallTime); 7c user name everywhere + NameSamCompatible; 7d SIDs (sub-authorities synthetic, RID exact, user / machine consistent); 7e volume serials | not in the capture | ProductId, InstallDate / InstallTime, user name, NameSamCompatible, user SID + machine SID, 8 volume serials, MachineGuid / HwProfileGuid / SQM MachineId |
| 8 | fwid 1.4 (optional, formats not released): 8a installation ID (check digits not modelled), 8b telemetry IDs, 8c SusClientIdValidation (embedded hash not modelled), 8d disk serials, 8e GPU unique values; HID / Bluetooth (remote device addresses OUI kept + synthetic tail, names exact, the local radio = its adapter MAC) / audio endpoint GUIDs | not in the capture | not in the capture |

Filler that is never replaced: "Default string", "To Be Filled By O.E.M.", "None", "No Asset Tag", "N/A",
"Not Specified", "Unknown", "NO DIMM", "Empty", "Not Available", "Not Installed", "Undefined", the placeholder values
"00000000" and "9876543210" (case-insensitive, trailing blanks ignored), empty strings, fields without a string; the "no
container" GUID; USB placeholders. A real value in another serial / asset-tag / SKU field (types 1, 2, 3, 4, 39) and an
identifier line fwid marks `[IDENTIFIER` that no rule consumes stop the build as undecided. Exact by decision as well: the
guest UUID's VMware layout, the .vmx disk vendor / product strings, NIC OUIs, PnP instance hashes, volume labels, EDID
model codes, CHIDs (the CHID-relevant
SMBIOS fields are checked unchanged), Secure Boot / TPM, time zone, locale, keyboard layouts, domain / workgroup, RAM, screen
metrics, Wi-Fi counts, printers, HID / Bluetooth / audio / GPU names. Uptime is volatile and not part of the profile; the
certificate stores are not part of it (`certificates.txt` is never searched). ACPI tables are searched for every real
identifier; a hit is only reported. Both clean captures: 0 hits.

## Provenance

| profile | capture (relative to the capture root) | capture manifest SHA-256 |
|---|---|---|
| vmware_guest | `fwid\baseline\Run3_clean_guest_<computer-name>_20261015_225503` (+ companion `baseline\Run3_clean_guest_probes_20261015.sha256`) | 57268E68D23B488C74AC0ABA57EDD275DCEC9680153367A8C4A8786A30AE8E1C |
| bare_host | `baseline\Host_clean_20261016_fwid13\<computer-name>_20261016_115901_native-host` (+ `baseline\Host_clean_20261016_fwid13.sha256`, `baseline\Host_clean_20261016.sha256`, `baseline\Host_clean_20261016_xinuse_all_lps.sha256`) | C9B008D860BEB95F3E7BFD5724FE6670A01EC4024E9C5471D7EF34851D706B89 |

Both capture manifests and the companion hash lists are pinned (`BuildConfig::pinned`): a build refuses any other capture
and re-verifies every manifest entry (size + SHA-256). Each build also reads the OTHER environment's pinned capture
(`peer_capture_*`; its manifest and `enum_registry.txt` verified) for the USB serial table both profiles share.

## Regenerate, reproduce, new identity

```
emu-profilegen --guest | --vm | --host [--capture-root DIR] [--out DIR] [--original-out DIR | --no-original] [--seed FILE] [--rules LIST]
emu-profilegen --config Emulator\data\platform\<env>\build_config.txt [--check]
emu-profilegen --guest --new-identity          (a fresh seed: a different, equally consistent identity)
emu-profilegen --host --save-config FILE       (write the effective BuildConfig and stop)
```

- Capture root: `--capture-root`, else `NOVMP_CAPTURE_ROOT`, else the first ancestor folder `capture` of the repository
  that holds the input (as `Emulator\tools\bare_host_cpuid.py`).
- The BuildConfig carries the seed: the same `build_config.txt` (+ the same capture) always gives byte-identical files;
  `--check` regenerates in memory and compares (test.cmd suites `pg_profile_check_guest` / `pg_profile_check_host`).
- `--new-identity` (`ProfileGen->gen_new_seed()`) creates a fresh seed: every mapped field gets another value while every
  relation (MAC family, UUID tail, SIDs, install instant, one serial per monitor / USB device) still holds (unit test
  `pg_new_identity`). The seed store is updated after the build; rebuild the other environment too, because the USB serial
  mapping is shared by both profiles.

## The original profile stays local

The ORIGINAL profile (same layout, the real values) is written to `<capture root>\profiles_original\<env>\` (or
`--original-out`), never into the repository: the tool refuses any path inside a git work tree (a `.git` entry in the path
or an ancestor), and the repository's `.gitignore` ignores any `profiles_original` folder. Its `build_config.txt` also
records the resolved capture root and folder. Never commit, copy or paste values from it.
