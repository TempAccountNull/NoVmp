// emu::win sysdll (plan 3.M4, ledger U1981): see include/emu/sysdll.hpp. Reads only (emu::read_input_file); no host query.
#include <emu/sysdll.hpp>
#include <emu/input_file.hpp>
#include <emu/pe_image.hpp>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <tuple>

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

		bool dec_u64( const std::string& s, uint64_t& v )
		{
			if ( s.empty() || s.size() > 20 ) return false;
			v = 0;
			for ( char c : s )
			{
				if ( c < '0' || c > '9' ) return false;
				const uint64_t d = uint64_t( c - '0' );
				if ( v > ( UINT64_MAX - d ) / 10 ) return false;
				v = v * 10 + d;
			}
			return true;
		}

		// the decimal / hexadecimal number right after KEY in LINE ("KEY 123" / "KEY 0x7B" / "KEY 00007FFFFFFEFFFF" with hex=true)
		bool number_after( const std::string& line, const std::string& key, bool hex_digits, uint64_t& v )
		{
			const size_t p = line.find( key );
			if ( p == std::string::npos ) return false;
			size_t i = p + key.size();
			std::string t;
			if ( !hex_digits && line.compare( i, 2, "0x" ) == 0 ) { i += 2; hex_digits = true; }
			while ( i < line.size() && std::isxdigit( uint8_t( line[ i ] ) ) && t.size() < 20 ) t += line[ i++ ];
			if ( t.empty() ) return false;
			if ( !hex_digits ) return dec_u64( t, v );
			if ( t.size() > 16 ) return false;
			v = std::stoull( t, nullptr, 16 );
			return true;
		}
	}

	const char* name_of( environment e )
	{
		switch ( e )
		{
			case environment::vmware_guest: return "vmware_guest";
			case environment::bare_host: return "bare_host";
			default: return "none";
		}
	}

	environment environment_of( cpu_profile_choice p )
	{
		if ( p == cpu_profile_choice::guest ) return environment::vmware_guest;
		if ( p == cpu_profile_choice::host ) return environment::bare_host;
		return environment::none;
	}

	bool locate_capture( environment env, const std::string& explicit_dir, const std::string& data_dir, std::string& dir, std::string& error )
	{
		// U2042: the capture root named by NOVMP_CAPTURE_ROOT (the host path of the local capture, as capture_sysdlls.py /
		// win_env_t1.py / emu-profilegen read it); bounded, never written into guest memory
		std::string root;
		char* v = nullptr;
		size_t n = 0;
		if ( _dupenv_s( &v, &n, "NOVMP_CAPTURE_ROOT" ) == 0 && v )
		{
			if ( n <= 32768 ) root = v;
			free( v );
		}
		return locate_capture( env, explicit_dir, data_dir, root, dir, error );
	}

	bool locate_capture( environment env, const std::string& explicit_dir, const std::string& data_dir, const std::string& capture_root,
						 std::string& dir, std::string& error )
	{
		namespace fs = std::filesystem;
		if ( env == environment::none )
		{
			error = "no Windows environment for this CPU profile (--max / --cpuid-runtime): a PE run needs --guest (the VMware-guest capture) "
					"or --host (the bare-host capture, decision 3.0.8)";
			return false;
		}
		std::error_code ec;
		if ( !explicit_dir.empty() )
		{
			if ( !fs::is_regular_file( fs::path( explicit_dir ) / "manifest.txt", ec ) )
			{
				error = "--sysdlls " + explicit_dir + ": no manifest.txt there (make the capture with Emulator\\tools\\capture_sysdlls.py --env " + name_of( env ) + ")";
				return false;
			}
			dir = explicit_dir;
			return true;
		}
		if ( !capture_root.empty() )	// U2042: <capture root>\sysdlls\<env> before the ancestors (capture_sysdlls.py / win_env_t1.py order)
		{
			const fs::path c = fs::path( capture_root ) / "sysdlls" / name_of( env );
			if ( fs::is_regular_file( c / "manifest.txt", ec ) )
			{
				dir = c.string();
				return true;
			}
		}
		fs::path p = fs::absolute( fs::path( data_dir ), ec );
		for ( int depth = 0; depth < 16 && !p.empty(); depth++ )
		{
			const fs::path c = p / "capture" / "sysdlls" / name_of( env );
			if ( fs::is_regular_file( c / "manifest.txt", ec ) )
			{
				dir = c.string();
				return true;
			}
			if ( p == p.parent_path() ) break;
			p = p.parent_path();
		}
		error = std::string( "no system-DLL capture for the environment " ) + name_of( env ) +
				" (searched " + ( capture_root.empty() ? std::string( "" ) : "the capture root NOVMP_CAPTURE_ROOT " + capture_root + " for sysdlls\\" + name_of( env ) + "\\manifest.txt and " ) +
				"the ancestors of " + data_dir + " for capture\\sysdlls\\" + name_of( env ) + "\\manifest.txt; decision 3.0.8: " +
				( env == environment::bare_host ? "the bare-host DLL capture comes with a host package - not made yet; use --guest"
												: "run python -I Emulator\\tools\\capture_sysdlls.py --env vmware_guest in the VMware guest" ) +
				"; or name the folder with --sysdlls DIR, or set NOVMP_CAPTURE_ROOT to the capture root)";
		return false;
	}

	bool load_manifest( const std::string& dir, environment env, capture_manifest& m, std::string& error )
	{
		m = capture_manifest{};
		const std::string path = ( std::filesystem::path( dir ) / "manifest.txt" ).string();
		std::map<std::string, std::string> kv;
		bool ok = read_input_lines( path, 4096, max_manifest_bytes, [ & ]( std::string_view l, uint64_t ) {
			if ( l.empty() || l[ 0 ] == '#' ) return true;
			const size_t sp = l.find( ' ' );
			if ( sp == std::string_view::npos ) return true;
			kv[ std::string( l.substr( 0, sp ) ) ] = std::string( l.substr( sp + 1 ) );
			return true;
		}, error );
		if ( !ok ) return false;
		auto need = [ & ]( const char* k, std::string& out ) {
			auto it = kv.find( k );
			if ( it == kv.end() || it->second.empty() )
			{
				error = path + ": no \"" + k + "\" line";
				return false;
			}
			out = it->second;
			return true;
		};
		auto need_u = [ & ]( const char* k, uint64_t limit, uint64_t& out ) {
			std::string s;
			if ( !need( k, s ) ) return false;
			if ( !dec_u64( s, out ) || out > limit )
			{
				error = path + ": \"" + k + "\" is not a decimal number up to " + std::to_string( limit );
				return false;
			}
			return true;
		};
		std::string fmt;
		uint64_t a = 0, b = 0, c = 0, pt = 0;
		if ( !need( "format", fmt ) || !need( "environment", m.environment ) || !need( "captured", m.captured ) || !need_u( "os_major", 0xFFFF, a ) ||
			 !need_u( "os_minor", 0xFFFF, b ) || !need_u( "os_build", 0xFFFF, c ) || !need_u( "product_type", 3, pt ) || !need( "system_root", m.system_root ) ||
			 !need_u( "ntdll_size", max_ntdll_bytes, m.ntdll_size ) || !need( "ntdll_sha256", m.ntdll_sha256 ) )
			return false;
		if ( fmt != "1" )
		{
			error = path + ": format " + fmt + " (this lib reads format 1)";
			return false;
		}
		if ( m.environment != name_of( env ) )
		{
			error = path + ": the capture is of the environment " + m.environment + ", the run asks for " + name_of( env );
			return false;
		}
		if ( m.system_root.size() > 128 )
		{
			error = path + ": system_root longer than 128 characters";
			return false;
		}
		m.os_major = uint32_t( a );
		m.os_minor = uint32_t( b );
		m.os_build = uint32_t( c );
		m.product_type = uint32_t( pt );
		// U2024: the further files and the NLS roles (optional lines; a capture made before M5 part 2 has none)
		auto plain = []( const std::string& n ) {
			if ( n.empty() || n.size() > 64 ) return false;
			for ( char ch : n )
				if ( !( ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) || ch == '_' || ch == '.' || ch == '-' ) ) return false;
			return n != "." && n != "..";
		};
		for ( const auto& [ k, v ] : kv )
		{
			if ( k.rfind( "file_", 0 ) != 0 ) continue;
			const std::string name = k.substr( 5 );
			const size_t sp = v.find( ' ' );
			uint64_t size = 0;
			if ( !plain( name ) || sp == std::string::npos || !dec_u64( v.substr( 0, sp ), size ) || !size || size > max_capture_file_bytes || v.size() - sp - 1 != 64 )
			{
				error = path + ": malformed line \"" + k + "\" (want file_<lower-case name> <size 1.." + std::to_string( max_capture_file_bytes ) + "> <sha256>)";
				return false;
			}
			m.files[ name ] = { size, v.substr( sp + 1 ) };
		}
		for ( auto [ key, cp, file ] : { std::tuple<const char*, uint32_t*, std::string*>{ "nls_acp", &m.nls_acp, &m.nls_acp_file }, { "nls_oemcp", &m.nls_oemcp, &m.nls_oemcp_file },
										 { "nls_case", nullptr, &m.nls_case_file } } )
		{
			const auto it = kv.find( key );
			if ( it == kv.end() ) continue;
			std::string v = it->second;
			if ( cp )
			{
				const size_t sp = v.find( ' ' );
				uint64_t n = 0;
				if ( sp == std::string::npos || !dec_u64( v.substr( 0, sp ), n ) || n > 0xFFFF )
				{
					error = path + ": malformed line \"" + key + "\" (want <code page> <file>)";
					return false;
				}
				*cp = uint32_t( n );
				v = v.substr( sp + 1 );
			}
			if ( !m.files.count( v ) )
			{
				error = path + ": \"" + key + "\" names " + v + ", which has no file_ line";
				return false;
			}
			*file = v;
		}
		return true;
	}

	bool load_registry( const std::string& dir, std::map<std::string, nt::registry_key>& out, std::string& error )
	{
		out.clear();
		const std::string path = ( std::filesystem::path( dir ) / "registry.txt" ).string();
		std::error_code ec;
		if ( !std::filesystem::is_regular_file( path, ec ) ) return true;
		auto split = []( std::string_view l ) {
			std::vector<std::string> f;
			size_t p = 0;
			while ( true )
			{
				const size_t t = l.find( '\t', p );
				f.emplace_back( l.substr( p, t == std::string_view::npos ? std::string_view::npos : t - p ) );
				if ( t == std::string_view::npos ) break;
				p = t + 1;
			}
			return f;
		};
		bool bad = false;
		std::string why;
		const bool ok = read_input_lines( path, 16384, max_registry_bytes, [ & ]( std::string_view l, uint64_t no ) {
			if ( l.empty() || l[ 0 ] == '#' ) return true;
			const auto f = split( l );
			if ( f.size() == 3 && f[ 0 ] == "key" && ( f[ 1 ] == "present" || f[ 1 ] == "absent" ) )
			{
				out[ f[ 2 ] ].present = f[ 1 ] == "present";
				return true;
			}
			if ( f[ 0 ] == "value" && ( f.size() == 4 || f.size() == 5 ) )
			{
				nt::registry_value v;
				if ( f.size() == 4 && f[ 3 ] == "absent" )
					v.type = ~0u;
				else if ( f.size() == 5 && f[ 4 ].size() % 2 == 0 && f[ 4 ].size() <= 8192 )
				{
					uint64_t t = 0;
					if ( !dec_u64( f[ 3 ], t ) || t > 11 ) { bad = true; why = "line " + std::to_string( no ) + ": bad type"; return false; }
					v.type = uint32_t( t );
					for ( size_t i = 0; i < f[ 4 ].size(); i += 2 )
					{
						const int hi = std::isxdigit( uint8_t( f[ 4 ][ i ] ) ) ? std::stoi( f[ 4 ].substr( i, 1 ), nullptr, 16 ) : -1,
								  lo = std::isxdigit( uint8_t( f[ 4 ][ i + 1 ] ) ) ? std::stoi( f[ 4 ].substr( i + 1, 1 ), nullptr, 16 ) : -1;
						if ( hi < 0 || lo < 0 ) { bad = true; why = "line " + std::to_string( no ) + ": bad hex"; return false; }
						v.data.push_back( uint8_t( hi * 16 + lo ) );
					}
				}
				else { bad = true; why = "line " + std::to_string( no ) + ": malformed value line"; return false; }
				out[ f[ 1 ] ].values[ f[ 2 ] ] = v;
				return true;
			}
			bad = true;
			why = "line " + std::to_string( no ) + ": unknown line";
			return false;
		}, error );
		if ( !ok ) return false;
		if ( bad )
		{
			error = path + ": " + why;
			return false;
		}
		return true;
	}

	bool load_capture_file( const std::string& dir, const capture_manifest& m, const std::string& name, std::vector<uint8_t>& out, std::string& error )
	{
		out.clear();
		const auto it = m.files.find( name );
		if ( it == m.files.end() )
		{
			error = "the capture " + dir + " has no file " + name + " (capture_sysdlls.py --dll / --nls, U2024)";
			return false;
		}
		const std::string p = ( std::filesystem::path( dir ) / name ).string();
		std::string bytes;
		if ( !read_input_file( p, max_capture_file_bytes, bytes, error ) ) return false;
		if ( bytes.size() != it->second.first )
		{
			error = p + " is " + std::to_string( bytes.size() ) + " bytes, the capture manifest says " + std::to_string( it->second.first ) + " - recapture";
			return false;
		}
		out.assign( bytes.begin(), bytes.end() );
		return true;
	}

	bool parse_syscall_table( const std::vector<uint8_t>& f, syscall_table& t, std::string& error )
	{
		t = syscall_table{};
		pe::image img;
		if ( !pe::parse( f, img, error ) ) return false;
		std::vector<pe::export_entry> ex;
		if ( !pe::exports( f, img, ex, error ) ) return false;
		for ( const auto& e : ex )
		{
			if ( e.name.size() < 3 || e.name[ 0 ] != 'N' || e.name[ 1 ] != 't' || e.forwarder ) continue;
			t.nt_exports++;
			uint64_t off = 0;
			uint8_t b[ 8 ] = {};
			if ( !pe::rva_to_offset( img, e.rva, 8, off ) ) continue;
			std::memcpy( b, f.data() + off, 8 );
			if ( !( b[ 0 ] == 0x4C && b[ 1 ] == 0x8B && b[ 2 ] == 0xD1 && b[ 3 ] == 0xB8 ) ) continue;
			uint32_t n = 0;
			std::memcpy( &n, b + 4, 4 );
			if ( n > 0xFFF )
			{
				error = "ntdll stub " + e.name + ": service number " + hex( n ) + " outside the NT table (bits 15:12 must be 0)";
				return false;
			}
			auto it = t.by_number.find( n );
			if ( it != t.by_number.end() && it->second != e.name )
			{
				error = "ntdll stubs " + it->second + " and " + e.name + " both load service number " + hex( n );
				return false;
			}
			t.by_number[ n ] = e.name;
			t.by_name[ e.name ] = n;
			t.stubs++;
		}
		if ( !t.stubs )
		{
			error = "no system-service stub (4C 8B D1 B8 imm32) among " + std::to_string( t.nt_exports ) + " Nt* exports";
			return false;
		}
		return true;
	}

	bool load_platform_facts( environment env, const std::string& data_dir, platform_facts& p, std::string& error )
	{
		p = platform_facts{};
		if ( env == environment::none )
		{
			error = "no platform profile for this CPU profile";
			return false;
		}
		p.source = ( std::filesystem::path( data_dir ) / "platform" / name_of( env ) / "views" / "environment.txt" ).string();
		bool in_native = false, got_cpus = false, got_phys = false, got_arch = false, got_min = false, got_max = false, got_page = false, got_gran = false,
			 got_mask = false;
		bool ok = read_input_lines( p.source, 4096, max_platform_view_bytes, [ & ]( std::string_view sv, uint64_t ) {
			const std::string l( sv );
			if ( l.rfind( "## ", 0 ) == 0 ) in_native = l.find( "GetNativeSystemInfo" ) != std::string::npos;
			uint64_t v = 0;
			if ( !got_phys && number_after( l, "TotalPhys ", false, v ) ) { p.total_phys = v; got_phys = true; }
			if ( !in_native ) return true;
			if ( number_after( l, "dwNumberOfProcessors ", false, v ) && v && v <= 4096 ) { p.processors = uint32_t( v ); got_cpus = true; }
			if ( number_after( l, "wProcessorArchitecture ", false, v ) && v <= 0xFFFF ) { p.processor_architecture = uint32_t( v ); got_arch = true; }
			if ( number_after( l, "lpMinimumApplicationAddress ", true, v ) ) { p.min_app_address = v; got_min = true; }
			if ( number_after( l, "lpMaximumApplicationAddress ", true, v ) ) { p.max_app_address = v; got_max = true; }
			if ( number_after( l, "dwPageSize ", false, v ) && v <= 0xFFFFFFFFull ) { p.page_size = uint32_t( v ); got_page = true; }
			if ( number_after( l, "dwAllocationGranularity ", false, v ) && v <= 0xFFFFFFFFull ) { p.allocation_granularity = uint32_t( v ); got_gran = true; }
			if ( number_after( l, "dwActiveProcessorMask ", false, v ) ) { p.active_processor_mask = v; got_mask = true; }
			return true;
		}, error );
		if ( !ok ) return false;
		if ( !got_cpus || !got_phys || !got_arch || !got_min || !got_max || p.min_app_address >= p.max_app_address )
		{
			error = p.source + ": the GetNativeSystemInfo / GlobalMemoryStatusEx lines (dwNumberOfProcessors, wProcessorArchitecture, lpMinimum / "
								"lpMaximumApplicationAddress, TotalPhys) are incomplete";
			return false;
		}
		// U2011: the page size must be the 4 KiB the engine maps; the granularity a power of two of at least one page
		const auto pow2 = []( uint64_t x ) { return x && !( x & ( x - 1 ) ); };
		if ( !got_page || !got_gran || !got_mask || p.page_size != 0x1000 || !pow2( p.allocation_granularity ) || p.allocation_granularity < p.page_size ||
			 p.allocation_granularity > 0x1000000 || !p.active_processor_mask )
		{
			error = p.source + ": dwPageSize (4096) / dwAllocationGranularity (a power of two >= the page) / dwActiveProcessorMask (non-zero) missing or unusable";
			return false;
		}
		return true;
	}
}
