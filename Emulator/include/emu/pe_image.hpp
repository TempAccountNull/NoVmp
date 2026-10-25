// emu::pe (plan 3.M4 = 3.B 6.6 part, ledger U1982): a 64-bit PE image read AS DATA and mapped into the engine's guest memory.
// The input is untrusted (the sample later, our test PEs now): every field is bounds-checked with 64-bit arithmetic before
// it is used, and anything this milestone does not model is refused loudly with the reason (never skipped silently).
// Nothing here loads, maps or executes the file on the host (no LoadLibrary, no image section, no executable host memory):
// the bytes come from emu::read_input_file and go only to Unicorn's guest memory through emu::engine (AGENT_RULES
// "Host safety").
//
// Layouts: the Windows SDK 10.0.26100.0 um\winnt.h structures IMAGE_DOS_HEADER, IMAGE_FILE_HEADER, IMAGE_OPTIONAL_HEADER64,
// IMAGE_SECTION_HEADER, IMAGE_DATA_DIRECTORY, IMAGE_BASE_RELOCATION, IMAGE_EXPORT_DIRECTORY (their sizes are
// static_asserted in pe_image.cpp) and the constants IMAGE_FILE_MACHINE_AMD64 (8664H), IMAGE_NT_OPTIONAL_HDR64_MAGIC (20BH),
// IMAGE_SCN_MEM_EXECUTE / READ / WRITE, IMAGE_REL_BASED_ABSOLUTE (0) / HIGHLOW (3) / DIR64 (10), IMAGE_FILE_RELOCS_STRIPPED.
//
// Mapping (port of emu_extentions\win_env.hpp map_image (old :771-852), REWRITTEN with checks; see P34): the image is built
// in a host buffer of SizeOfImage bytes - the headers (SizeOfHeaders bytes), every section's raw data (at most its mapped
// extent = VirtualSize, or SizeOfRawData when VirtualSize is 0, rounded up to SectionAlignment; the rest zero) - then base
// relocations are applied when the image is not at its preferred ImageBase, the buffer is
// written into guest memory and each part is mapped with its protection: the headers read-only, each section as its
// characteristics say (R / W / X -> UC_PROT_READ / WRITE / EXEC; old section_protect :287). Holes between sections stay
// unmapped (an access there is a fault, like the reserved-but-uncommitted pages of a Windows image view).
// Refused in M4 (loud): not AMD64 / not PE32+, SectionAlignment below 4 KiB or not a power of two, overlapping or
// unaligned sections, raw data outside the file, a relocation type other than ABSOLUTE / HIGHLOW / DIR64, a relocated image
// without relocations (or RELOCS_STRIPPED), and - by the caller (win_env, milestone M5) - imports and TLS callbacks.
#pragma once

#include <emu/engine.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace emu::pe
{
	inline constexpr uint64_t max_image_file_bytes = 512ull << 20;   // the largest PE file read (512 MiB)
	inline constexpr uint64_t max_size_of_image = 1ull << 30;        // the engine maps at most 1 GiB in all
	inline constexpr uint32_t max_sections = 96;                     // this parser's bound (a chosen limit, not a cited one)

	struct data_dir
	{
		uint32_t rva = 0, size = 0;
	};

	struct section
	{
		std::string name;              // up to 8 characters, printable ASCII only (others shown as '?')
		uint32_t rva = 0;              // VirtualAddress
		uint32_t virtual_size = 0;     // VirtualSize (0 = SizeOfRawData, as the old loader and the PE format allow)
		uint32_t raw_offset = 0;       // PointerToRawData
		uint32_t raw_size = 0;         // SizeOfRawData
		uint32_t characteristics = 0;
		uint64_t mapped_size = 0;      // the extent mapped: VirtualSize (SizeOfRawData when 0) rounded up to SectionAlignment
		uint64_t copy_size = 0;        // file bytes copied: min( SizeOfRawData, mapped_size ) (the rest of the extent is zero)
		uint32_t prot = 0;             // UC_PROT_* from the characteristics
	};

	struct image
	{
		uint32_t e_lfanew = 0;
		uint16_t machine = 0, file_characteristics = 0, subsystem = 0, dll_characteristics = 0;
		uint16_t subsystem_major = 0, subsystem_minor = 0;
		uint64_t image_base = 0;
		uint32_t entry_rva = 0, size_of_image = 0, size_of_headers = 0, section_alignment = 0, file_alignment = 0;
		uint64_t stack_reserve = 0, stack_commit = 0, heap_reserve = 0, heap_commit = 0;
		uint32_t number_of_rva_and_sizes = 0;
		std::array<data_dir, 16> dirs{};
		std::vector<section> sections;
		uint64_t file_bytes = 0;
	};

	// UC_PROT_* of a section's IMAGE_SCN_MEM_* characteristics (no access = UC_PROT_NONE)
	uint32_t uc_prot_of( uint32_t characteristics );
	std::string prot_text( uint32_t uc_prot );

	// parse and check the headers (no guest memory involved); false with the reason
	bool parse( const std::vector<uint8_t>& file, image& out, std::string& error );

	// the file offset of [rva, rva + size) - inside ONE section's raw data or inside the headers; false otherwise
	bool rva_to_offset( const image& img, uint32_t rva, uint64_t size, uint64_t& offset );

	struct export_entry
	{
		std::string name;      // "" for an export by ordinal only
		uint32_t rva = 0;      // the function's RVA (a forwarder's RVA points at its string)
		uint32_t ordinal = 0;  // biased ordinal (Base + index)
		bool forwarder = false;
	};
	// the export directory (IMAGE_DIRECTORY_ENTRY_EXPORT) of a parsed image, read from the FILE bytes
	bool exports( const std::vector<uint8_t>& file, const image& img, std::vector<export_entry>& out, std::string& error );

	// U2008 (plan 3.M5): the import directory (IMAGE_DIRECTORY_ENTRY_IMPORT) read from the FILE bytes: winnt.h
	// IMAGE_IMPORT_DESCRIPTOR {OriginalFirstThunk, TimeDateStamp, ForwarderChain, Name, FirstThunk}, IMAGE_THUNK_DATA64 (bit 63 =
	// IMAGE_ORDINAL_FLAG64: an ordinal, else the RVA of IMAGE_IMPORT_BY_NAME {Hint, Name}); every RVA bounds-checked; at most
	// max_import_modules descriptors and max_import_entries entries each (chosen limits); names printable ASCII, <= 512 chars
	inline constexpr uint32_t max_import_modules = 256, max_import_entries = 4096;
	struct import_entry
	{
		std::string name;          // "" = by ordinal
		uint32_t ordinal = 0;      // by ordinal: the ordinal (16 bits)
		uint16_t hint = 0;
		uint32_t iat_rva = 0;      // the IAT slot (FirstThunk + 8 * i) the loader writes
	};
	struct import_module
	{
		std::string dll;           // as written in the file
		std::vector<import_entry> entries;
	};
	bool imports( const std::vector<uint8_t>& file, const image& img, std::vector<import_module>& out, std::string& error );

	struct mapping
	{
		uint64_t base = 0;              // where the image was mapped
		uint64_t size = 0;              // SizeOfImage rounded up to 4 KiB
		bool relocated = false;         // base != ImageBase
		uint64_t relocations = 0;       // fixups applied (DIR64 + HIGHLOW)
		uint64_t entry = 0;             // base + AddressOfEntryPoint (0 when the image has none)
		std::vector<std::string> log;   // one line per step / region
	};

	// build the mapped image and map it into the engine at BASE (4 KiB aligned): headers R, sections per characteristics;
	// relocations applied when BASE != ImageBase. Every region is named "<name> <section>". False with the reason (the
	// engine may then hold some of the regions: the caller discards the engine).
	bool map( engine& e, const std::vector<uint8_t>& file, const image& img, uint64_t base, const std::string& name, mapping& out,
			  std::string& error );

	// U2008 (plan 3.M5): the TLS directory (IMAGE_DIRECTORY_ENTRY_TLS) read from the MAPPED image (its VAs are relocated there):
	// winnt.h IMAGE_TLS_DIRECTORY64 {StartAddressOfRawData, EndAddressOfRawData, AddressOfIndex, AddressOfCallBacks,
	// SizeOfZeroFill, Characteristics}; the callback array is a zero-terminated array of VAs (PIMAGE_TLS_CALLBACK). Every VA
	// must lie inside the mapped image; at most max_tls_callbacks callbacks (a chosen limit); the template at most 1 MiB.
	inline constexpr uint32_t max_tls_callbacks = 64;
	inline constexpr uint64_t max_tls_template = 1ull << 20;
	struct tls_directory
	{
		bool present = false;
		uint64_t start = 0, end = 0, index_va = 0, callbacks_va = 0;
		uint32_t zero_fill = 0, characteristics = 0;
		std::vector<uint64_t> callbacks;
	};
	bool tls( const engine& e, const image& img, const mapping& m, tls_directory& out, std::string& error );
}
