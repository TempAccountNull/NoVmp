// emu::win process state (plan 3.M5 part 2, ledgers U2025-U2027): see include/emu/win_process.hpp. Guest memory only; every
// field offset below is the member offset of the guest ntdll.pdb public-symbol type (emu/win_fields.hpp, static_asserted
// against the table by name); values are synthesized from the capture / platform facts or placeholders.
#include <emu/win_process.hpp>
#include <emu/win_env.hpp>
#include <emu/win_fields.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace emu::win
{
	namespace
	{
		std::string hex( uint64_t v )
		{
			char b[ 32 ];
			std::snprintf( b, sizeof( b ), "0x%llx", (unsigned long long)v );
			return b;
		}

		std::string lower( std::string s )
		{
			for ( auto& c : s ) c = char( std::tolower( uint8_t( c ) ) );
			return s;
		}

		// ---- offsets from the public-symbol tables (emu/win_fields.hpp), checked at compile time
#define EMU_OFF( T, M ) fields::offset_of( fields::T, M )
		constexpr uint32_t peb_ldr = EMU_OFF( peb, "Ldr" );
		static_assert( peb_ldr == 0x18, "_PEB Ldr" );
		// _PEB_LDR_DATA
		constexpr uint32_t ld_length = EMU_OFF( peb_ldr_data, "Length" ), ld_initialized = EMU_OFF( peb_ldr_data, "Initialized" ),
						   ld_load = EMU_OFF( peb_ldr_data, "InLoadOrderModuleList" ), ld_memory = EMU_OFF( peb_ldr_data, "InMemoryOrderModuleList" ),
						   ld_init = EMU_OFF( peb_ldr_data, "InInitializationOrderModuleList" );
		static_assert( ld_length == 0 && ld_initialized == 4 && ld_load == 0x10 && ld_memory == 0x20 && ld_init == 0x30, "_PEB_LDR_DATA" );
		// _LDR_DATA_TABLE_ENTRY
		constexpr uint32_t le_load = EMU_OFF( ldr_data_table_entry, "InLoadOrderLinks" ), le_memory = EMU_OFF( ldr_data_table_entry, "InMemoryOrderLinks" ),
						   le_init = EMU_OFF( ldr_data_table_entry, "InInitializationOrderLinks" ), le_base = EMU_OFF( ldr_data_table_entry, "DllBase" ),
						   le_entry = EMU_OFF( ldr_data_table_entry, "EntryPoint" ), le_size = EMU_OFF( ldr_data_table_entry, "SizeOfImage" ),
						   le_full = EMU_OFF( ldr_data_table_entry, "FullDllName" ), le_basename = EMU_OFF( ldr_data_table_entry, "BaseDllName" ),
						   le_flags = EMU_OFF( ldr_data_table_entry, "Flags" ), le_hash = EMU_OFF( ldr_data_table_entry, "HashLinks" ),
						   le_time = EMU_OFF( ldr_data_table_entry, "TimeDateStamp" ), le_original = EMU_OFF( ldr_data_table_entry, "OriginalBase" ),
						   le_reason = EMU_OFF( ldr_data_table_entry, "LoadReason" ), le_refs = EMU_OFF( ldr_data_table_entry, "ReferenceCount" );
		static_assert( le_load == 0 && le_memory == 0x10 && le_init == 0x20 && le_base == 0x30 && le_entry == 0x38 && le_size == 0x40 && le_full == 0x48 &&
						   le_basename == 0x58 && le_flags == 0x68 && le_hash == 0x70 && le_time == 0x80 && le_original == 0xF8 && le_reason == 0x10C && le_refs == 0x114,
					   "_LDR_DATA_TABLE_ENTRY" );
		// _RTL_USER_PROCESS_PARAMETERS (_CURDIR: DosPath 0, Handle 10H)
		constexpr uint32_t pp_max = EMU_OFF( process_parameters, "MaximumLength" ), pp_len = EMU_OFF( process_parameters, "Length" ),
						   pp_flags = EMU_OFF( process_parameters, "Flags" ), pp_curdir = EMU_OFF( process_parameters, "CurrentDirectory" ),
						   pp_dllpath = EMU_OFF( process_parameters, "DllPath" ), pp_image = EMU_OFF( process_parameters, "ImagePathName" ),
						   pp_cmdline = EMU_OFF( process_parameters, "CommandLine" ), pp_env = EMU_OFF( process_parameters, "Environment" ),
						   pp_title = EMU_OFF( process_parameters, "WindowTitle" ), pp_desktop = EMU_OFF( process_parameters, "DesktopInfo" ),
						   pp_shell = EMU_OFF( process_parameters, "ShellInfo" ), pp_runtime = EMU_OFF( process_parameters, "RuntimeData" ),
						   pp_envsize = EMU_OFF( process_parameters, "EnvironmentSize" ), pp_console = EMU_OFF( process_parameters, "ConsoleHandle" );   // P41 (U2069)
		static_assert( pp_max == 0 && pp_len == 4 && pp_flags == 8 && pp_curdir == 0x38 && pp_dllpath == 0x50 && pp_image == 0x60 && pp_cmdline == 0x70 && pp_env == 0x80 &&
						   pp_title == 0xB0 && pp_desktop == 0xC0 && pp_shell == 0xD0 && pp_runtime == 0xE0 && pp_envsize == 0x3F0 && pp_console == 0x10,
					   "_RTL_USER_PROCESS_PARAMETERS" );
		// _HEAP (the anonymous _HEAP_SEGMENT part: Heap 28H, BaseAddress 30H; Flags 70H, ForceFlags 74H)
		constexpr uint32_t heap_heap = EMU_OFF( heap, "Heap" ), heap_base_address = EMU_OFF( heap, "BaseAddress" ), heap_flags = EMU_OFF( heap, "Flags" ),
						   heap_force_flags = EMU_OFF( heap, "ForceFlags" );
		static_assert( heap_heap == 0x28 && heap_base_address == 0x30 && heap_flags == 0x70 && heap_force_flags == 0x74, "_HEAP" );
#undef EMU_OFF
		constexpr uint32_t heap_growable = 0x2;   // winnt.h HEAP_GROWABLE (Windows SDK 10.0.26100.0 um\winnt.h :23131)
		constexpr uint64_t heap_segment_reserve = 0x100000, heap_segment_commit = 0x2000;   // ReactOS mminit.c:367-368 (PEB defaults)
		constexpr uint32_t ldr_flag_image_dll = 1u << 2, ldr_flag_in_legacy_lists = 1u << 6;   // _LDR_DATA_TABLE_ENTRY bitfields ImageDll, InLegacyLists
		constexpr uint32_t load_reason_static_dependency = 0;                                   // _LDR_DLL_LOAD_REASON LoadReasonStaticDependency
		constexpr uint32_t rtl_user_process_parameters_normalized = 1;                         // ReactOS sdk/include/ndk/rtltypes.h:41

		struct writer
		{
			engine& e;
			std::string err;
			bool ok = true;
			void raw( uint64_t va, const void* p, uint64_t n )
			{
				if ( ok && !e.write( va, p, n, err ) ) ok = false;
			}
			void u16( uint64_t va, uint16_t v ) { raw( va, &v, 2 ); }
			void u32( uint64_t va, uint32_t v ) { raw( va, &v, 4 ); }
			void u64( uint64_t va, uint64_t v ) { raw( va, &v, 8 ); }
			// a UNICODE_STRING {Length, MaximumLength, Buffer} at VA whose characters go to *CURSOR (NUL-terminated, 8-aligned)
			void ustr( uint64_t va, const std::string& s, uint64_t& cursor )
			{
				std::vector<uint16_t> w;
				for ( char c : s ) w.push_back( uint16_t( uint8_t( c ) < 0x80 ? c : '?' ) );
				const uint16_t len = uint16_t( w.size() * 2 );
				w.push_back( 0 );
				u16( va, len );
				u16( va + 2, uint16_t( len + 2 ) );
				u64( va + 8, cursor );
				raw( cursor, w.data(), w.size() * 2 );
				cursor = ( cursor + w.size() * 2 + 7 ) & ~7ull;
			}
		};

		bool allocate_rw( nt::kernel& k, uint64_t reserve, uint64_t commit, uint64_t& base, std::string& error )
		{
			base = 0;
			uint64_t size = reserve;
			nt::reply r = k.allocate( base, size, nt::mem::reserve, nt::page::readwrite );
			if ( r.kind != nt::reply_kind::done || r.status )
			{
				error = "reserve: " + r.text;
				return false;
			}
			uint64_t cb = base, cs = commit;
			r = k.allocate( cb, cs, nt::mem::commit, nt::page::readwrite );
			if ( r.kind != nt::reply_kind::done || r.status )
			{
				error = "commit: " + r.text;
				return false;
			}
			return true;
		}
	}

	uint64_t system_image_slot( const std::string& n )
	{
		// fixed, chosen slots of 256 MiB each in the high user range above the emulator's HLE page (7FFA00000000H) (Windows: the
		// boot-time ASLR base of each system image, the same in every process)
		if ( n == "ntdll.dll" ) return 0x7FFB00000000ull;
		if ( n == "kernel32.dll" ) return 0x7FFB10000000ull;
		if ( n == "kernelbase.dll" ) return 0x7FFB20000000ull;
		// U2105 (P43, sample run 8): every other section of the environment's \KnownDlls directory (the 42 names measured by tm_nt kd,
		// nt_kernel_measured.cpp known_dll_sections, lower case) - slot = 7FFB30000000H + i * 10000000H, i = the position in this sorted
		// table; the last slot ends at 7FFDA0000000H (below the top-down allocations from the user maximum and the CSR views)
		static const char* const knowndll_others[] = {
			"advapi32.dll", "bcrypt.dll", "bcryptprimitives.dll", "cfgmgr32.dll", "clbcatq.dll", "combase.dll",
			"comctl32.dll", "comdlg32.dll", "coml2.dll", "crypt32.dll", "difxapi.dll", "gdi32.dll",
			"gdi32full.dll", "gdiplus.dll", "imagehlp.dll", "imm32.dll", "msctf.dll", "msvcp_win.dll",
			"msvcrt.dll", "normaliz.dll", "nsi.dll", "ole32.dll", "oleaut32.dll", "psapi.dll",
			"rpcrt4.dll", "sechost.dll", "setupapi.dll", "shcore.dll", "shell32.dll", "shlwapi.dll",
			"ucrtbase.dll", "user32.dll", "win32u.dll", "wintrust.dll", "wldap32.dll", "wow64.dll",
			"wow64cpu.dll", "wow64win.dll", "ws2_32.dll",
		};
		for ( size_t i = 0; i < sizeof( knowndll_others ) / sizeof( knowndll_others[ 0 ] ); i++ )
			if ( n == knowndll_others[ i ] ) return 0x7FFB30000000ull + uint64_t( i ) * system_image_slot_size;
		return 0;
	}

	bool map_one_image( engine& e, const std::string& name, const std::string& system_root, const std::vector<uint8_t>& file, uint64_t base, module_image& m,
						std::vector<std::string>& log, std::string& error )
	{
		m = module_image{};
		m.name = name;
		m.path = system_root + "\\System32\\" + name;
		m.is_dll = true;
		if ( !pe::parse( file, m.img, error ) )
		{
			error = name + ": " + error;
			return false;
		}
		if ( !( m.img.file_characteristics & 0x2000 ) )   // winnt.h IMAGE_FILE_DLL
		{
			error = name + " of the capture is not a DLL";
			return false;
		}
		pe::mapping mp;
		if ( !pe::map( e, file, m.img, base, name, mp, error ) )
		{
			error = name + ": " + error;
			return false;
		}
		for ( auto& l : mp.log ) log.push_back( "pe: " + l );
		m.base = mp.base;
		m.size = mp.size;
		m.entry = mp.entry;
		m.original_base = m.img.image_base;
		std::vector<pe::export_entry> ex;
		if ( !pe::exports( file, m.img, ex, error ) )
		{
			error = name + ": " + error;
			return false;
		}
		for ( auto& x : ex )
			if ( !x.forwarder && !x.name.empty() ) m.exports_by_rva.emplace( x.rva, x.name );
		std::memcpy( &m.timestamp, file.data() + m.img.e_lfanew + 8, 4 );   // IMAGE_FILE_HEADER TimeDateStamp: e_lfanew + 8 (winnt.h)
		log.push_back( "system image " + name + " mapped as data at " + hex( m.base ) + " (slot chosen; Windows: ASLR), SizeOfImage " + hex( m.size ) + ", entry " +
					   hex( m.entry ) + ", " + std::to_string( m.exports_by_rva.size() ) + " named exports" +
					   ( mp.relocated ? ", relocated (" + std::to_string( mp.relocations ) + " fixups)" : "" ) );
		return true;
	}

	bool map_system_images( engine& e, const environment_facts& f, const std::vector<std::string>& names, std::vector<module_image>& out, std::vector<std::string>& log,
							std::string& error )
	{
		out.clear();
		if ( !f.files ) return true;
		for ( const auto& name : names )
		{
			const auto it = f.files->find( name );
			if ( it == f.files->end() )
			{
				log.push_back( "system image " + name + ": not in the capture - not mapped" );
				continue;
			}
			module_image m;
			if ( !map_one_image( e, name, f.system_root, it->second, system_image_slot( name ), m, log, error ) ) return false;
			out.push_back( std::move( m ) );
		}
		return true;
	}

	process_identity make_identity( const environment_facts& f, const std::string& image_file_name )
	{
		process_identity id;
		const std::string drive = f.system_root.size() >= 2 && f.system_root[ 1 ] == ':' ? f.system_root.substr( 0, 2 ) : "C:";
		id.image_dir = drive + "\\Users\\" + id.user + "\\Desktop";
		id.image_path = id.image_dir + "\\" + image_file_name;
		return id;
	}

	bool build_process_parameters( engine& e, nt::kernel& k, const environment_facts& f, const process_identity& id, process_state& st,
								   std::vector<std::string>& lines, std::string& error )
	{
		uint64_t base = 0;
		if ( !allocate_rw( k, 0x10000, 0x10000, base, error ) )
		{
			error = "process parameters: " + error;
			return false;
		}
		writer w{ e };
		const std::string drive = id.image_dir.substr( 0, 2 );
		const std::string root = f.system_root, profile = drive + "\\Users\\" + id.user;
		// the environment block: placeholders only, sorted case-insensitively, "NAME=value" UTF-16 strings, a final empty string
		std::vector<std::pair<std::string, std::string>> env = {
			{ "ALLUSERSPROFILE", drive + "\\ProgramData" }, { "APPDATA", profile + "\\AppData\\Roaming" }, { "COMPUTERNAME", id.computer },
			{ "ComSpec", root + "\\system32\\cmd.exe" }, { "HOMEDRIVE", drive }, { "HOMEPATH", "\\Users\\" + id.user }, { "LOCALAPPDATA", profile + "\\AppData\\Local" },
			{ "NUMBER_OF_PROCESSORS", std::to_string( f.processors ) }, { "OS", "Windows_NT" }, { "Path", root + "\\system32;" + root + ";" + root + "\\System32\\Wbem" },
			{ "PATHEXT", ".COM;.EXE;.BAT;.CMD" }, { "PROCESSOR_ARCHITECTURE", f.processor_architecture == 9 ? "AMD64" : "UNKNOWN" }, { "ProgramData", drive + "\\ProgramData" },
			{ "ProgramFiles", drive + "\\Program Files" }, { "PUBLIC", drive + "\\Users\\Public" }, { "SystemDrive", drive }, { "SystemRoot", root },
			{ "TEMP", profile + "\\AppData\\Local\\Temp" }, { "TMP", profile + "\\AppData\\Local\\Temp" }, { "USERDOMAIN", id.computer }, { "USERNAME", id.user },
			{ "USERPROFILE", profile }, { "windir", root } };
		std::sort( env.begin(), env.end(), []( const auto& a, const auto& b ) { return lower( a.first ) < lower( b.first ); } );
		std::vector<uint16_t> block;
		for ( auto& [ n, v ] : env )
		{
			for ( char c : n + "=" + v ) block.push_back( uint16_t( uint8_t( c ) ) );
			block.push_back( 0 );
		}
		block.push_back( 0 );
		const uint64_t env_va = base + 0x8000;
		w.raw( env_va, block.data(), block.size() * 2 );
		uint64_t cursor = base + 0x440;   // the strings after the structure (sizeof _RTL_USER_PROCESS_PARAMETERS 440H)
		static_assert( fields::process_parameters_size == 0x440, "_RTL_USER_PROCESS_PARAMETERS size" );
		const std::string cmd = "\"" + id.image_path + "\"";
		w.ustr( base + pp_curdir, id.image_dir + "\\", cursor );   // CurrentDirectory.DosPath (Handle 0: no directory handle object)
		w.ustr( base + pp_dllpath, "", cursor );
		w.ustr( base + pp_image, id.image_path, cursor );
		w.ustr( base + pp_cmdline, cmd, cursor );
		w.ustr( base + pp_title, id.image_path, cursor );
		w.ustr( base + pp_desktop, "Winsta0\\Default", cursor );
		w.ustr( base + pp_shell, "", cursor );
		w.ustr( base + pp_runtime, "", cursor );
		const uint32_t used = uint32_t( cursor - base );
		w.u32( base + pp_max, used );
		w.u32( base + pp_len, used );
		w.u32( base + pp_flags, rtl_user_process_parameters_normalized );
		w.u64( base + pp_env, env_va );
		w.u64( base + pp_envsize, block.size() * 2 );
		// P41 (U2069, USER DECISION 2026-10-26 "started without a console"): the emulated process is the model of a process started
		// DETACHED - CreateProcessW( ..., DETACHED_PROCESS, STARTUPINFOW {cb only} ): ConsoleHandle = RTL_USER_PROC_DETACHED_PROCESS (-1,
		// phnt ntrtl.h:4433; the creator's rule: ReactOS 0.4.16 kernel32/client/proc.c:681-685, reference only), ConsoleFlags 0, no
		// standard handles (StandardInput / Output / Error 0: no STARTF_USESTDHANDLES), WindowFlags / ShowWindowFlags 0. Measured from
		// inside our own detached test PE (tm_nt con.pp: the same values after kernelbase's start). Without it (ConsoleHandle 0) the real
		// kernelbase allocates a NEW console for a console-subsystem image (\Device\ConDrv\Server + a console host: the P40 frontier).
		w.u64( base + pp_console, ~0ull );
		if ( !w.ok )
		{
			error = "process parameters: " + w.err;
			return false;
		}
		st.parameters = base;
		st.environment = env_va;
		st.environment_size = block.size() * 2;
		lines.push_back( "RTL_USER_PROCESS_PARAMETERS at " + hex( base ) + " (normalized, Flags 1; Length / MaximumLength " + hex( used ) + "): ImagePathName " + id.image_path +
						 ", CommandLine " + cmd + ", CurrentDirectory " + id.image_dir + "\\, WindowTitle = the image path, DesktopInfo Winsta0\\Default, ConsoleHandle -1 (started without a console: the model of a DETACHED_PROCESS start, P41), no standard handles - placeholders (user \"" +
						 id.user + "\", computer \"" + id.computer + "\"), no real identifier" );
		lines.push_back( "environment block at " + hex( env_va ) + ": " + std::to_string( env.size() ) + " variables, " + hex( block.size() * 2 ) +
						 " bytes (EnvironmentSize), placeholders derived from the capture's system root and the platform facts" );
		return true;
	}

	bool build_process_heap( engine& e, nt::kernel& k, process_state& st, std::vector<std::string>& lines, std::string& error )
	{
		uint64_t base = 0;
		if ( !allocate_rw( k, heap_segment_reserve, heap_segment_commit, base, error ) )
		{
			error = "process heap: " + error;
			return false;
		}
		writer w{ e };
		w.u64( base + heap_heap, base );
		w.u64( base + heap_base_address, base );
		w.u32( base + heap_flags, heap_growable );
		w.u32( base + heap_force_flags, 0 );
		if ( !w.ok )
		{
			error = "process heap: " + w.err;
			return false;
		}
		st.heap = base;
		lines.push_back( "process heap at " + hex( base ) + ": reserve " + hex( heap_segment_reserve ) + ", commit " + hex( heap_segment_commit ) + ", _HEAP Flags (70H) = 2 " +
						 "(HEAP_GROWABLE), ForceFlags (74H) = 0 - no heap-debugging flags (NtGlobalFlag 0, no debugger); no allocator behind it in this milestone" );
		return true;
	}

	bool build_loader_data( engine& e, nt::kernel& k, const std::vector<module_image>& mods, process_state& st, std::vector<std::string>& lines, std::string& error )
	{
		uint64_t base = 0;
		if ( !allocate_rw( k, 0x10000, 0x10000, base, error ) )
		{
			error = "loader data: " + error;
			return false;
		}
		writer w{ e };
		static_assert( fields::peb_ldr_data_size == 0x58 && fields::ldr_data_table_entry_size == 0x120, "_PEB_LDR_DATA / _LDR_DATA_TABLE_ENTRY sizes" );
		const uint64_t ldr = base;
		w.u32( ldr + ld_length, fields::peb_ldr_data_size );
		w.raw( ldr + ld_initialized, "\x01", 1 );
		std::vector<uint64_t> ent;
		for ( size_t i = 0; i < mods.size(); i++ ) ent.push_back( base + 0x100 + 0x180 * i );
		uint64_t cursor = base + 0x100 + 0x180 * mods.size();
		for ( size_t i = 0; i < mods.size(); i++ )
		{
			const auto& m = mods[ i ];
			const uint64_t a = ent[ i ];
			w.u64( a + le_base, m.base );
			w.u64( a + le_entry, m.entry );
			w.u32( a + le_size, uint32_t( m.size ) );
			w.ustr( a + le_full, m.path, cursor );
			w.ustr( a + le_basename, m.name, cursor );
			w.u32( a + le_flags, ( m.is_dll ? ldr_flag_image_dll : 0 ) | ldr_flag_in_legacy_lists );
			w.u64( a + le_hash, a + le_hash );       // HashLinks: an empty list (self-linked; the loader's hash table is ntdll's)
			w.u64( a + le_hash + 8, a + le_hash );
			w.u32( a + le_time, m.timestamp );
			w.u64( a + le_original, m.original_base );
			w.u32( a + le_reason, load_reason_static_dependency );
			w.u32( a + le_refs, 1 );
		}
		// the three circular lists: LIST_ENTRY {Flink, Blink}
		auto link = [ & ]( uint64_t head, uint32_t off, const std::vector<size_t>& order ) {
			std::vector<uint64_t> nodes{ head };
			for ( size_t i : order ) nodes.push_back( ent[ i ] + off );
			for ( size_t i = 0; i < nodes.size(); i++ )
			{
				w.u64( nodes[ i ], nodes[ ( i + 1 ) % nodes.size() ] );
				w.u64( nodes[ i ] + 8, nodes[ ( i + nodes.size() - 1 ) % nodes.size() ] );
			}
		};
		std::vector<size_t> load_order, init_order;
		for ( size_t i = 0; i < mods.size(); i++ ) load_order.push_back( i );
		// initialization order: the dependencies before their users - ntdll, kernelbase, kernel32 (kernel32 imports kernelbase); the
		// process image is not in this list
		for ( const char* n : { "ntdll.dll", "kernelbase.dll", "kernel32.dll" } )
			for ( size_t i = 0; i < mods.size(); i++ )
				if ( mods[ i ].is_dll && mods[ i ].name == n ) init_order.push_back( i );
		link( ldr + ld_load, le_load, load_order );
		link( ldr + ld_memory, le_memory, load_order );
		link( ldr + ld_init, le_init, init_order );
		if ( !w.ok )
		{
			error = "loader data: " + w.err;
			return false;
		}
		st.ldr = ldr;
		st.entries = ent;
		std::string names, inits;
		for ( size_t i : load_order ) names += ( names.empty() ? "" : ", " ) + mods[ i ].name + " " + hex( mods[ i ].base );
		for ( size_t i : init_order ) inits += ( inits.empty() ? "" : ", " ) + mods[ i ].name;
		lines.push_back( "PEB_LDR_DATA at " + hex( ldr ) + " (Length 58H, Initialized 1): InLoadOrder / InMemoryOrder " + names + "; InInitializationOrder " +
						 ( inits.empty() ? std::string( "(empty)" ) : inits ) );
		for ( size_t i = 0; i < mods.size(); i++ )
			lines.push_back( "LDR_DATA_TABLE_ENTRY " + mods[ i ].name + " at " + hex( ent[ i ] ) + ": DllBase " + hex( mods[ i ].base ) + ", EntryPoint " + hex( mods[ i ].entry ) +
							 ", SizeOfImage " + hex( mods[ i ].size ) + ", FullDllName " + mods[ i ].path + ", Flags " +
							 hex( ( mods[ i ].is_dll ? ldr_flag_image_dll : 0 ) | ldr_flag_in_legacy_lists ) + ", TimeDateStamp " + hex( mods[ i ].timestamp ) + ", OriginalBase " +
							 hex( mods[ i ].original_base ) );
		return true;
	}
}
