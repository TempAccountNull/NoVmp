// emu::pe (plan 3.M4, ledger U1982): see include/emu/pe_image.hpp. Every read of the file goes through rd() (bounds-checked);
// every size / offset is 64-bit; nothing is executed or mapped on the host.
#include <emu/pe_image.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // winnt.h: the IMAGE_* structures and constants (Windows SDK 10.0.26100.0), types only

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace emu::pe
{
	// the structure sizes the parser relies on (Windows SDK um\winnt.h)
	static_assert( sizeof( IMAGE_DOS_HEADER ) == 64 && offsetof( IMAGE_DOS_HEADER, e_lfanew ) == 0x3C, "IMAGE_DOS_HEADER" );
	static_assert( sizeof( IMAGE_FILE_HEADER ) == 20, "IMAGE_FILE_HEADER" );
	static_assert( sizeof( IMAGE_OPTIONAL_HEADER64 ) == 240 && offsetof( IMAGE_OPTIONAL_HEADER64, DataDirectory ) == 112, "IMAGE_OPTIONAL_HEADER64" );
	static_assert( offsetof( IMAGE_OPTIONAL_HEADER64, ImageBase ) == 24 && offsetof( IMAGE_OPTIONAL_HEADER64, SizeOfStackReserve ) == 72,
				   "IMAGE_OPTIONAL_HEADER64 fields" );
	static_assert( sizeof( IMAGE_SECTION_HEADER ) == 40, "IMAGE_SECTION_HEADER" );
	static_assert( sizeof( IMAGE_EXPORT_DIRECTORY ) == 40, "IMAGE_EXPORT_DIRECTORY" );
	static_assert( sizeof( IMAGE_BASE_RELOCATION ) == 8, "IMAGE_BASE_RELOCATION" );
	static_assert( IMAGE_FILE_MACHINE_AMD64 == 0x8664 && IMAGE_NT_OPTIONAL_HDR64_MAGIC == 0x20B, "AMD64 / PE32+" );
	static_assert( IMAGE_REL_BASED_ABSOLUTE == 0 && IMAGE_REL_BASED_HIGHLOW == 3 && IMAGE_REL_BASED_DIR64 == 10, "relocation types" );
	static_assert( sizeof( IMAGE_IMPORT_DESCRIPTOR ) == 20 && offsetof( IMAGE_IMPORT_DESCRIPTOR, Name ) == 12 && offsetof( IMAGE_IMPORT_DESCRIPTOR, FirstThunk ) == 16,
				   "IMAGE_IMPORT_DESCRIPTOR (U2008)" );
	static_assert( sizeof( IMAGE_TLS_DIRECTORY64 ) == 40 && offsetof( IMAGE_TLS_DIRECTORY64, AddressOfCallBacks ) == 24 && offsetof( IMAGE_TLS_DIRECTORY64, SizeOfZeroFill ) == 32,
				   "IMAGE_TLS_DIRECTORY64 (U2008)" );
	static_assert( IMAGE_ORDINAL_FLAG64 == 0x8000000000000000ull, "IMAGE_ORDINAL_FLAG64 (U2008)" );

	namespace
	{
		std::string hex( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}

		// copy [off, off + n) of the file into out; false when it is not entirely inside the file
		bool rd( const std::vector<uint8_t>& f, uint64_t off, void* out, uint64_t n )
		{
			if ( off > f.size() || n > f.size() - off ) return false;
			if ( n ) std::memcpy( out, f.data() + off, size_t( n ) );
			return true;
		}

		bool pow2( uint64_t v ) { return v && !( v & ( v - 1 ) ); }

		// round V up to a power-of-two A; false on overflow
		bool align_up( uint64_t v, uint64_t a, uint64_t& out )
		{
			if ( v > UINT64_MAX - ( a - 1 ) ) return false;
			out = ( v + a - 1 ) & ~( a - 1 );
			return true;
		}

		std::string section_name( const uint8_t raw[ 8 ] )
		{
			std::string s;
			for ( int i = 0; i < 8 && raw[ i ]; i++ ) s += ( raw[ i ] >= 0x20 && raw[ i ] < 0x7F ) ? char( raw[ i ] ) : '?';
			return s;
		}
	}

	uint32_t uc_prot_of( uint32_t ch )
	{
		uint32_t p = UC_PROT_NONE;
		if ( ch & IMAGE_SCN_MEM_READ ) p |= UC_PROT_READ;
		if ( ch & IMAGE_SCN_MEM_WRITE ) p |= UC_PROT_WRITE;
		if ( ch & IMAGE_SCN_MEM_EXECUTE ) p |= UC_PROT_EXEC;
		return p;
	}

	std::string prot_text( uint32_t p )
	{
		std::string s;
		s += p & UC_PROT_READ ? 'r' : '-';
		s += p & UC_PROT_WRITE ? 'w' : '-';
		s += p & UC_PROT_EXEC ? 'x' : '-';
		return s;
	}

	bool parse( const std::vector<uint8_t>& f, image& o, std::string& error )
	{
		o = image{};
		o.file_bytes = f.size();
		auto fail = [ & ]( const std::string& why ) {
			error = "PE: " + why;
			return false;
		};
		if ( f.size() > max_image_file_bytes ) return fail( "file larger than " + hex( max_image_file_bytes ) + " bytes" );
		IMAGE_DOS_HEADER dos{};
		if ( !rd( f, 0, &dos, sizeof( dos ) ) ) return fail( "file shorter than the 64-byte DOS header" );
		if ( dos.e_magic != IMAGE_DOS_SIGNATURE ) return fail( "no MZ signature" );
		if ( dos.e_lfanew < 0 ) return fail( "negative e_lfanew" );
		o.e_lfanew = uint32_t( dos.e_lfanew );
		uint32_t sig = 0;
		IMAGE_FILE_HEADER fh{};
		if ( !rd( f, o.e_lfanew, &sig, 4 ) || !rd( f, uint64_t( o.e_lfanew ) + 4, &fh, sizeof( fh ) ) ) return fail( "e_lfanew " + hex( o.e_lfanew ) + " outside the file" );
		if ( sig != IMAGE_NT_SIGNATURE ) return fail( "no PE\\0\\0 signature at e_lfanew" );
		o.machine = fh.Machine;
		o.file_characteristics = fh.Characteristics;
		if ( fh.Machine != IMAGE_FILE_MACHINE_AMD64 ) return fail( "Machine " + hex( fh.Machine ) + " is not AMD64 (8664H); 32-bit images are plan 1.I" );
		if ( fh.NumberOfSections == 0 || fh.NumberOfSections > max_sections ) return fail( "NumberOfSections " + std::to_string( fh.NumberOfSections ) + " (1.." + std::to_string( max_sections ) + ")" );
		if ( fh.SizeOfOptionalHeader < offsetof( IMAGE_OPTIONAL_HEADER64, DataDirectory ) ) return fail( "SizeOfOptionalHeader " + hex( fh.SizeOfOptionalHeader ) + " below 70H" );
		const uint64_t opt_off = uint64_t( o.e_lfanew ) + 4 + sizeof( IMAGE_FILE_HEADER );
		IMAGE_OPTIONAL_HEADER64 oh{};
		if ( opt_off > f.size() || fh.SizeOfOptionalHeader > f.size() - opt_off ) return fail( "optional header (SizeOfOptionalHeader " + hex( fh.SizeOfOptionalHeader ) + ") outside the file" );
		rd( f, opt_off, &oh, std::min<uint64_t>( fh.SizeOfOptionalHeader, sizeof( oh ) ) );
		if ( oh.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ) return fail( "optional header Magic " + hex( oh.Magic ) + " is not PE32+ (20BH)" );
		o.image_base = oh.ImageBase;
		o.entry_rva = oh.AddressOfEntryPoint;
		o.section_alignment = oh.SectionAlignment;
		o.file_alignment = oh.FileAlignment;
		o.size_of_image = oh.SizeOfImage;
		o.size_of_headers = oh.SizeOfHeaders;
		o.subsystem = oh.Subsystem;
		o.subsystem_major = oh.MajorSubsystemVersion;
		o.subsystem_minor = oh.MinorSubsystemVersion;
		o.dll_characteristics = oh.DllCharacteristics;
		o.stack_reserve = oh.SizeOfStackReserve;
		o.stack_commit = oh.SizeOfStackCommit;
		o.heap_reserve = oh.SizeOfHeapReserve;
		o.heap_commit = oh.SizeOfHeapCommit;
		o.number_of_rva_and_sizes = oh.NumberOfRvaAndSizes;
		if ( oh.NumberOfRvaAndSizes > 16 ) return fail( "NumberOfRvaAndSizes " + std::to_string( oh.NumberOfRvaAndSizes ) + " above 16" );
		if ( uint64_t( offsetof( IMAGE_OPTIONAL_HEADER64, DataDirectory ) ) + 8ull * oh.NumberOfRvaAndSizes > fh.SizeOfOptionalHeader )
			return fail( "the data directories do not fit SizeOfOptionalHeader" );
		for ( uint32_t i = 0; i < oh.NumberOfRvaAndSizes; i++ ) o.dirs[ i ] = { oh.DataDirectory[ i ].VirtualAddress, oh.DataDirectory[ i ].Size };

		if ( !pow2( oh.SectionAlignment ) || oh.SectionAlignment < page_size )
			return fail( "SectionAlignment " + hex( oh.SectionAlignment ) + ": milestone M4 maps power-of-two alignments of at least 4 KiB only" );
		if ( !pow2( oh.FileAlignment ) || oh.FileAlignment < 0x200 || oh.FileAlignment > 0x10000 || oh.FileAlignment > oh.SectionAlignment )
			return fail( "FileAlignment " + hex( oh.FileAlignment ) + ": milestone M4 needs a power of two in [200H, 10000H] not above SectionAlignment" );
		if ( oh.SizeOfImage == 0 || oh.SizeOfImage > max_size_of_image ) return fail( "SizeOfImage " + hex( oh.SizeOfImage ) + " (1.." + hex( max_size_of_image ) + ")" );
		if ( oh.SizeOfHeaders == 0 || oh.SizeOfHeaders > oh.SizeOfImage || oh.SizeOfHeaders > f.size() )
			return fail( "SizeOfHeaders " + hex( oh.SizeOfHeaders ) + " (must be inside SizeOfImage and the file)" );
		if ( oh.ImageBase & ( page_size - 1 ) ) return fail( "ImageBase " + hex( oh.ImageBase ) + " is not 4 KiB aligned" );
		uint64_t image_end = 0;
		if ( !align_up( oh.SizeOfImage, page_size, image_end ) || oh.ImageBase > UINT64_MAX - image_end ) return fail( "ImageBase + SizeOfImage above 2^64" );
		if ( oh.AddressOfEntryPoint >= oh.SizeOfImage ) return fail( "AddressOfEntryPoint " + hex( oh.AddressOfEntryPoint ) + " outside SizeOfImage" );

		// the section table: right after the optional header, inside the headers and the file
		const uint64_t sec_off = opt_off + fh.SizeOfOptionalHeader;
		const uint64_t sec_bytes = uint64_t( fh.NumberOfSections ) * sizeof( IMAGE_SECTION_HEADER );
		if ( sec_off > f.size() || sec_bytes > f.size() - sec_off ) return fail( "section table outside the file" );
		if ( sec_off + sec_bytes > oh.SizeOfHeaders ) return fail( "section table beyond SizeOfHeaders" );
		uint64_t headers_mapped = 0;
		align_up( oh.SizeOfHeaders, page_size, headers_mapped );
		uint64_t next_free = headers_mapped;   // sections ascending, non-overlapping, after the headers
		uint64_t size_aligned = 0;
		if ( !align_up( oh.SizeOfImage, oh.SectionAlignment, size_aligned ) ) return fail( "SizeOfImage overflow" );
		for ( uint32_t i = 0; i < fh.NumberOfSections; i++ )
		{
			IMAGE_SECTION_HEADER sh{};
			rd( f, sec_off + uint64_t( i ) * sizeof( sh ), &sh, sizeof( sh ) );
			section s;
			s.name = section_name( sh.Name );
			s.rva = sh.VirtualAddress;
			s.virtual_size = sh.Misc.VirtualSize;
			s.raw_offset = sh.PointerToRawData;
			s.raw_size = sh.SizeOfRawData;
			s.characteristics = sh.Characteristics;
			s.prot = uc_prot_of( sh.Characteristics );
			const std::string what = "section " + std::to_string( i ) + " \"" + s.name + "\"";
			const uint64_t extent = s.virtual_size ? s.virtual_size : s.raw_size;
			if ( !align_up( extent, oh.SectionAlignment, s.mapped_size ) ) return fail( what + ": size overflow" );
			s.copy_size = std::min<uint64_t>( s.raw_size, s.mapped_size );
			if ( s.rva % oh.SectionAlignment ) return fail( what + ": VirtualAddress " + hex( s.rva ) + " not a multiple of SectionAlignment" );
			if ( s.mapped_size && s.rva < next_free ) return fail( what + ": VirtualAddress " + hex( s.rva ) + " overlaps the headers or the previous section (sections must ascend)" );
			if ( uint64_t( s.rva ) + s.mapped_size > size_aligned ) return fail( what + ": [" + hex( s.rva ) + ", +" + hex( s.mapped_size ) + ") beyond SizeOfImage" );
			if ( s.copy_size )
			{
				if ( s.raw_offset % oh.FileAlignment ) return fail( what + ": PointerToRawData " + hex( s.raw_offset ) + " not a multiple of FileAlignment (M4: the loader's rounding is not modelled)" );
				if ( uint64_t( s.raw_offset ) > f.size() || s.copy_size > f.size() - s.raw_offset ) return fail( what + ": raw data [" + hex( s.raw_offset ) + ", +" + hex( s.copy_size ) + ") outside the file" );
			}
			if ( s.mapped_size ) next_free = uint64_t( s.rva ) + s.mapped_size;
			o.sections.push_back( s );
		}
		return true;
	}

	bool rva_to_offset( const image& img, uint32_t rva, uint64_t size, uint64_t& offset )
	{
		const uint64_t end = uint64_t( rva ) + size;
		if ( end <= std::min<uint64_t>( img.size_of_headers, img.file_bytes ) )
		{
			offset = rva;
			return true;
		}
		for ( const auto& s : img.sections )
			if ( rva >= s.rva && end <= uint64_t( s.rva ) + s.copy_size )
			{
				offset = uint64_t( s.raw_offset ) + ( rva - s.rva );
				return true;
			}
		return false;
	}

	bool exports( const std::vector<uint8_t>& f, const image& img, std::vector<export_entry>& out, std::string& error )
	{
		out.clear();
		const data_dir d = img.dirs[ IMAGE_DIRECTORY_ENTRY_EXPORT ];
		if ( !d.rva || !d.size ) return true;
		auto fail = [ & ]( const std::string& why ) {
			error = "PE exports: " + why;
			out.clear();
			return false;
		};
		uint64_t off = 0;
		IMAGE_EXPORT_DIRECTORY ed{};
		if ( !rva_to_offset( img, d.rva, sizeof( ed ), off ) || !rd( f, off, &ed, sizeof( ed ) ) ) return fail( "export directory " + hex( d.rva ) + " outside the file's sections" );
		constexpr uint32_t max_exports = 1u << 16;
		if ( ed.NumberOfFunctions > max_exports || ed.NumberOfNames > max_exports ) return fail( "more than 65536 functions / names" );
		uint64_t fo = 0, no = 0, oo = 0;
		if ( ed.NumberOfFunctions && !rva_to_offset( img, ed.AddressOfFunctions, 4ull * ed.NumberOfFunctions, fo ) ) return fail( "AddressOfFunctions outside the file" );
		if ( ed.NumberOfNames && ( !rva_to_offset( img, ed.AddressOfNames, 4ull * ed.NumberOfNames, no ) || !rva_to_offset( img, ed.AddressOfNameOrdinals, 2ull * ed.NumberOfNames, oo ) ) )
			return fail( "AddressOfNames / AddressOfNameOrdinals outside the file" );
		std::vector<std::string> names( ed.NumberOfFunctions );
		for ( uint32_t i = 0; i < ed.NumberOfNames; i++ )
		{
			uint32_t name_rva = 0;
			uint16_t index = 0;
			rd( f, no + 4ull * i, &name_rva, 4 );
			rd( f, oo + 2ull * i, &index, 2 );
			if ( index >= ed.NumberOfFunctions ) return fail( "name " + std::to_string( i ) + ": ordinal index " + std::to_string( index ) + " outside the function table" );
			uint64_t so = 0;
			if ( !rva_to_offset( img, name_rva, 1, so ) ) return fail( "name " + std::to_string( i ) + " RVA " + hex( name_rva ) + " outside the file" );
			std::string n;
			for ( uint64_t k = 0;; k++ )
			{
				if ( k >= 512 ) return fail( "name " + std::to_string( i ) + " longer than 512 characters" );
				uint64_t co = 0;
				char c = 0;
				if ( !rva_to_offset( img, uint32_t( name_rva + k ), 1, co ) || !rd( f, co, &c, 1 ) ) return fail( "name " + std::to_string( i ) + " runs out of the file" );
				if ( !c ) break;
				if ( uint8_t( c ) < 0x20 || uint8_t( c ) >= 0x7F ) return fail( "name " + std::to_string( i ) + " has a non-printable character" );
				n += c;
			}
			if ( !names[ index ].empty() ) continue;   // a second name for the same function: the first one is kept
			names[ index ] = n;
		}
		for ( uint32_t i = 0; i < ed.NumberOfFunctions; i++ )
		{
			uint32_t rva = 0;
			rd( f, fo + 4ull * i, &rva, 4 );
			if ( !rva ) continue;
			export_entry e;
			e.name = names[ i ];
			e.rva = rva;
			e.ordinal = ed.Base + i;
			e.forwarder = rva >= d.rva && uint64_t( rva ) < uint64_t( d.rva ) + d.size;
			out.push_back( e );
		}
		return true;
	}

	// U2008 (plan 3.M5): imports from the file bytes, bounds-checked
	bool imports( const std::vector<uint8_t>& f, const image& img, std::vector<import_module>& out, std::string& error )
	{
		out.clear();
		const data_dir d = img.dirs[ IMAGE_DIRECTORY_ENTRY_IMPORT ];
		if ( !d.rva && !d.size ) return true;
		auto fail = [ & ]( const std::string& why ) {
			error = "PE imports: " + why;
			out.clear();
			return false;
		};
		auto cstr = [ & ]( uint32_t rva, std::string& s ) {
			s.clear();
			for ( uint64_t k = 0;; k++ )
			{
				if ( k >= 512 ) return false;
				uint64_t o = 0;
				char c = 0;
				if ( rva > UINT32_MAX - k || !rva_to_offset( img, uint32_t( rva + k ), 1, o ) || !rd( f, o, &c, 1 ) ) return false;
				if ( !c ) return true;
				if ( uint8_t( c ) < 0x20 || uint8_t( c ) >= 0x7F ) return false;
				s += c;
			}
		};
		for ( uint32_t i = 0;; i++ )
		{
			if ( i >= max_import_modules ) return fail( "more than " + std::to_string( max_import_modules ) + " import descriptors (no terminating null descriptor)" );
			IMAGE_IMPORT_DESCRIPTOR id{};
			uint64_t o = 0;
			const uint64_t at = uint64_t( d.rva ) + 20ull * i;
			if ( at > UINT32_MAX || !rva_to_offset( img, uint32_t( at ), sizeof( id ), o ) || !rd( f, o, &id, sizeof( id ) ) )
				return fail( "descriptor " + std::to_string( i ) + " at RVA " + hex( at ) + " outside the file's sections" );
			if ( !id.Name && !id.FirstThunk && !id.OriginalFirstThunk ) break;
			import_module m;
			if ( !id.Name || !cstr( id.Name, m.dll ) || m.dll.empty() ) return fail( "descriptor " + std::to_string( i ) + ": DLL name at RVA " + hex( id.Name ) + " unreadable" );
			if ( !id.FirstThunk ) return fail( m.dll + ": FirstThunk 0" );
			const uint32_t lookup = id.OriginalFirstThunk ? id.OriginalFirstThunk : id.FirstThunk;
			for ( uint32_t k = 0;; k++ )
			{
				if ( k >= max_import_entries ) return fail( m.dll + ": more than " + std::to_string( max_import_entries ) + " entries (no terminating 0)" );
				uint64_t t = 0, to = 0;
				const uint64_t trva = uint64_t( lookup ) + 8ull * k, irva = uint64_t( id.FirstThunk ) + 8ull * k;
				if ( trva > UINT32_MAX || irva > UINT32_MAX || !rva_to_offset( img, uint32_t( trva ), 8, to ) || !rd( f, to, &t, 8 ) )
					return fail( m.dll + ": thunk " + std::to_string( k ) + " outside the file's sections" );
				if ( !t ) break;
				uint64_t io = 0;
				if ( !rva_to_offset( img, uint32_t( irva ), 8, io ) ) return fail( m.dll + ": IAT slot " + std::to_string( k ) + " (RVA " + hex( irva ) + ") outside the file's sections" );
				import_entry e;
				e.iat_rva = uint32_t( irva );
				if ( t & IMAGE_ORDINAL_FLAG64 )
				{
					if ( t & 0x7FFFFFFFFFFF0000ull ) return fail( m.dll + ": ordinal thunk " + hex( t ) + " with bits 62:16 set" );
					e.ordinal = uint32_t( t & 0xFFFF );
				}
				else
				{
					if ( t > 0x7FFFFFFFull ) return fail( m.dll + ": name thunk " + hex( t ) + " above 31 bits" );
					uint64_t ho = 0;
					if ( !rva_to_offset( img, uint32_t( t ), 2, ho ) || !rd( f, ho, &e.hint, 2 ) || !cstr( uint32_t( t ) + 2, e.name ) || e.name.empty() )
						return fail( m.dll + ": IMAGE_IMPORT_BY_NAME at RVA " + hex( t ) + " unreadable" );
				}
				m.entries.push_back( e );
			}
			out.push_back( m );
		}
		return true;
	}

	bool tls( const engine& e, const image& img, const mapping& m, tls_directory& out, std::string& error )
	{
		out = tls_directory{};
		const data_dir d = img.dirs[ IMAGE_DIRECTORY_ENTRY_TLS ];
		if ( !d.rva && !d.size ) return true;
		auto fail = [ & ]( const std::string& why ) {
			error = "PE TLS: " + why;
			return false;
		};
		if ( d.size < sizeof( IMAGE_TLS_DIRECTORY64 ) ) return fail( "directory size " + hex( d.size ) + " below sizeof IMAGE_TLS_DIRECTORY64 (28H)" );
		auto in_image = [ & ]( uint64_t va, uint64_t n ) { return va >= m.base && n <= m.size && va - m.base <= m.size - n; };
		IMAGE_TLS_DIRECTORY64 t{};
		std::string err;
		if ( !in_image( m.base + d.rva, sizeof( t ) ) || !e.read( m.base + d.rva, &t, sizeof( t ), err ) ) return fail( "directory at RVA " + hex( d.rva ) + " not in the mapped image" );
		out.present = true;
		out.start = t.StartAddressOfRawData;
		out.end = t.EndAddressOfRawData;
		out.index_va = t.AddressOfIndex;
		out.callbacks_va = t.AddressOfCallBacks;
		out.zero_fill = t.SizeOfZeroFill;
		out.characteristics = t.Characteristics;
		if ( out.end < out.start || out.end - out.start > max_tls_template || ( out.end > out.start && !in_image( out.start, out.end - out.start ) ) )
			return fail( "template [" + hex( out.start ) + ", " + hex( out.end ) + ") not inside the mapped image (or above 1 MiB)" );
		if ( out.zero_fill > max_tls_template ) return fail( "SizeOfZeroFill " + hex( out.zero_fill ) + " above 1 MiB" );
		if ( !in_image( out.index_va, 4 ) ) return fail( "AddressOfIndex " + hex( out.index_va ) + " not inside the mapped image" );
		if ( out.callbacks_va )
		{
			for ( uint32_t k = 0;; k++ )
			{
				if ( k >= max_tls_callbacks ) return fail( "more than " + std::to_string( max_tls_callbacks ) + " callbacks (no terminating 0)" );
				uint64_t cb = 0;
				if ( !in_image( out.callbacks_va + 8ull * k, 8 ) || !e.read( out.callbacks_va + 8ull * k, &cb, 8, err ) )
					return fail( "callback array entry " + std::to_string( k ) + " not inside the mapped image" );
				if ( !cb ) break;
				if ( !in_image( cb, 1 ) ) return fail( "callback " + hex( cb ) + " outside the mapped image" );
				out.callbacks.push_back( cb );
			}
		}
		return true;
	}

	bool map( engine& e, const std::vector<uint8_t>& f, const image& img, uint64_t base, const std::string& name, mapping& out, std::string& error, uint64_t reloc_for )
	{
		const uint64_t target = reloc_for ? reloc_for : base;   // p3cm (U2290): the address the relocations are applied for
		out = mapping{};
		auto fail = [ & ]( const std::string& why ) {
			error = "PE map " + name + ": " + why;
			out.log.push_back( "REFUSED " + error );
			return false;
		};
		uint64_t size = 0;
		align_up( img.size_of_image, page_size, size );
		if ( base & ( page_size - 1 ) ) return fail( "base " + hex( base ) + " not 4 KiB aligned" );
		if ( base > UINT64_MAX - size ) return fail( "base + SizeOfImage above 2^64" );
		out.base = base;
		out.size = size;
		out.relocated = target != img.image_base;
		out.entry = img.entry_rva ? base + img.entry_rva : 0;

		// the mapped parts in host buffers (data only; only the parts that are mapped, never a whole SizeOfImage buffer):
		// the headers, then each section's raw data (parse(): every copy is inside the file and inside its part)
		struct part
		{
			uint64_t rva = 0, size = 0;
			uint32_t prot = 0;
			std::string what;
			std::vector<uint8_t> bytes;
		};
		std::vector<part> parts;
		uint64_t hdr = 0;
		align_up( img.size_of_headers, page_size, hdr );
		parts.push_back( { 0, hdr, UC_PROT_READ, "headers", std::vector<uint8_t>( size_t( hdr ), 0 ) } );
		std::memcpy( parts.back().bytes.data(), f.data(), size_t( img.size_of_headers ) );
		for ( const auto& s : img.sections )
		{
			if ( !s.mapped_size )
			{
				out.log.push_back( "  section " + s.name + ": no extent (VirtualSize and SizeOfRawData 0), not mapped" );
				continue;
			}
			char ch[ 16 ];
			std::snprintf( ch, sizeof( ch ), "%08X", s.characteristics );
			parts.push_back( { s.rva, s.mapped_size, s.prot, "section " + s.name + " (characteristics " + ch + ")", std::vector<uint8_t>( size_t( s.mapped_size ), 0 ) } );
			if ( s.copy_size ) std::memcpy( parts.back().bytes.data(), f.data() + s.raw_offset, size_t( s.copy_size ) );
		}
		// [rva, rva + n) inside ONE part, or nullptr (a hole between sections is not part of the mapped image)
		auto locate = [ & ]( uint64_t rva, uint64_t n ) -> uint8_t* {
			for ( auto& p : parts )
				if ( rva >= p.rva && n <= p.size && rva - p.rva <= p.size - n ) return p.bytes.data() + ( rva - p.rva );
			return nullptr;
		};
		out.log.push_back( "image " + name + ": ImageBase " + hex( img.image_base ) + ", SizeOfImage " + hex( img.size_of_image ) + ", " +
						   std::to_string( img.sections.size() ) + " section(s), entry RVA " + hex( img.entry_rva ) + " -> mapped at " + hex( base ) +
						   ( out.relocated ? " (not the preferred base: relocations applied below)" : " (the preferred base)" ) );

		if ( out.relocated )
		{
			const data_dir d = img.dirs[ IMAGE_DIRECTORY_ENTRY_BASERELOC ];
			if ( ( img.file_characteristics & IMAGE_FILE_RELOCS_STRIPPED ) || !d.rva || !d.size )
				return fail( "the image is not at its preferred base " + hex( img.image_base ) + " and has no base relocations (RELOCS_STRIPPED or no directory 5)" );
			const uint8_t* dir = locate( d.rva, d.size );
			if ( !dir ) return fail( "base relocation directory [" + hex( d.rva ) + ", +" + hex( d.size ) + ") is not inside one mapped part of the image" );
			const uint64_t delta = target - img.image_base;   // modulo 2^64, as the fixups add it
			uint64_t off = 0, dir64 = 0, highlow = 0;
			while ( off < d.size )
			{
				if ( d.size - off < sizeof( IMAGE_BASE_RELOCATION ) ) return fail( "truncated relocation block header at directory offset " + hex( off ) );
				IMAGE_BASE_RELOCATION b{};
				std::memcpy( &b, dir + off, sizeof( b ) );
				if ( b.SizeOfBlock < sizeof( b ) || b.SizeOfBlock > d.size - off || ( b.SizeOfBlock & 1 ) )
					return fail( "relocation block at directory offset " + hex( off ) + " has SizeOfBlock " + hex( b.SizeOfBlock ) );
				for ( uint64_t k = sizeof( b ); k + 2 <= b.SizeOfBlock; k += 2 )
				{
					uint16_t ent = 0;
					std::memcpy( &ent, dir + off + k, 2 );
					const uint32_t type = ent >> 12;
					const uint64_t target = uint64_t( b.VirtualAddress ) + ( ent & 0xFFF );
					if ( type == IMAGE_REL_BASED_ABSOLUTE ) continue;
					if ( type == IMAGE_REL_BASED_DIR64 )
					{
						uint8_t* at = locate( target, 8 );
						if ( !at ) return fail( "DIR64 fixup at RVA " + hex( target ) + " outside the mapped parts of the image" );
						uint64_t v = 0;
						std::memcpy( &v, at, 8 );
						v += delta;
						std::memcpy( at, &v, 8 );
						dir64++;
					}
					else if ( type == IMAGE_REL_BASED_HIGHLOW )
					{
						uint8_t* at = locate( target, 4 );
						if ( !at ) return fail( "HIGHLOW fixup at RVA " + hex( target ) + " outside the mapped parts of the image" );
						uint32_t v = 0;
						std::memcpy( &v, at, 4 );
						v += uint32_t( delta );
						std::memcpy( at, &v, 4 );
						highlow++;
					}
					else
						return fail( "relocation type " + std::to_string( type ) + " at RVA " + hex( target ) + " (milestone M4 models ABSOLUTE, HIGHLOW and DIR64)" );
				}
				off += b.SizeOfBlock;
			}
			out.relocations = dir64 + highlow;
			out.log.push_back( "relocated by " + hex( delta ) + ": " + std::to_string( dir64 ) + " DIR64 + " + std::to_string( highlow ) + " HIGHLOW fixup(s)" +
							   " (OptionalHeader.ImageBase in the mapped headers is left as in the file)" );
		}

		// guest memory: headers read-only, each section with its own protection; the bytes written after each map
		for ( const auto& p : parts )
		{
			std::string err;
			if ( !e.map( base + p.rva, p.size, p.prot, name + " " + p.what, err ) ) return fail( err );
			if ( !e.write( base + p.rva, p.bytes.data(), p.size, err ) ) return fail( err );
			out.log.push_back( "  " + p.what + " [" + hex( base + p.rva ) + ", +" + hex( p.size ) + ") " + prot_text( p.prot ) );
		}
		return true;
	}
}
