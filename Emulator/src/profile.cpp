// emu::profile (plan 3.M2, ledger U1951): see include/emu/profile.hpp. Reads the captured CPUID profile files (read-only,
// bounded) - since U1972 (plan 3.M3) through emu::read_input_file, the one audited file input of the Emulator lib (the M2
// std::ifstream here was allow-listed until the M1-only "opens" rule group was retired; no_native_check rule group "fileio").
#include <emu/input_file.hpp>
#include <emu/profile.hpp>

#include <windows.h>

#include <filesystem>
#include <set>
#include <utility>

namespace emu
{
	namespace
	{
		bool is_hex( char c ) { return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' ); }

		// 1..8 hex digits, no prefix, no sign
		bool parse_hex32( std::string_view t, uint32_t& out )
		{
			if ( t.empty() || t.size() > 8 ) return false;
			uint32_t v = 0;
			for ( char c : t )
			{
				if ( !is_hex( c ) ) return false;
				v = ( v << 4 ) | uint32_t( c <= '9' ? c - '0' : ( c | 0x20 ) - 'a' + 10 );
			}
			out = v;
			return true;
		}

		std::vector<std::string_view> split_ws( std::string_view s )
		{
			std::vector<std::string_view> out;
			size_t i = 0;
			while ( i < s.size() )
			{
				while ( i < s.size() && ( s[ i ] == ' ' || s[ i ] == '\t' ) ) i++;
				size_t j = i;
				while ( j < s.size() && s[ j ] != ' ' && s[ j ] != '\t' ) j++;
				if ( j > i ) out.push_back( s.substr( i, j - i ) );
				i = j;
			}
			return out;
		}
	}

	bool parse_cpuid_profile( std::string_view text, cpuid_profile& out, std::string& error )
	{
		out.leaves.clear();
		out.has_microcode = false;
		out.microcode_rev = 0;
		out.microcode_platform_id = 0;
		if ( text.size() > max_profile_bytes )
		{
			error = "profile larger than " + std::to_string( max_profile_bytes ) + " bytes";
			return false;
		}
		std::set<std::pair<uint32_t, uint32_t>> seen;
		size_t pos = 0, line_no = 0;
		while ( pos <= text.size() )
		{
			size_t nl = text.find( '\n', pos );
			if ( nl == std::string_view::npos ) nl = text.size();
			std::string_view line = text.substr( pos, nl - pos );
			pos = nl + 1;
			line_no++;
			if ( !line.empty() && line.back() == '\r' ) line.remove_suffix( 1 );
			const std::string where = "line " + std::to_string( line_no ) + ": ";
			if ( line.find( '\0' ) != std::string_view::npos )
			{
				error = where + "NUL byte";
				return false;
			}
			const auto tok = split_ws( line );
			if ( tok.empty() ) continue;
			if ( line.rfind( "#@ microcode ", 0 ) == 0 )
			{
				// "#@ microcode REV platform-id N" (U1473)
				uint32_t rev = 0;
				if ( tok.size() != 5 || tok[ 1 ] != "microcode" || !parse_hex32( tok[ 2 ], rev ) || tok[ 3 ] != "platform-id" || tok[ 4 ].size() != 1 ||
					 tok[ 4 ][ 0 ] < '0' || tok[ 4 ][ 0 ] > '7' )
				{
					error = where + "malformed \"#@ microcode REV platform-id N\" line (REV hex, N 0..7)";
					return false;
				}
				if ( out.has_microcode )
				{
					error = where + "second \"#@ microcode\" line";
					return false;
				}
				out.has_microcode = true;
				out.microcode_rev = rev;
				out.microcode_platform_id = tok[ 4 ][ 0 ] - '0';
				continue;
			}
			if ( line.front() == '#' || tok[ 0 ].front() == '#' ) continue;   // comment
			if ( tok.size() != 6 )
			{
				error = where + "want 6 hex fields LEAF SUBLEAF EAX EBX ECX EDX, got " + std::to_string( tok.size() );
				return false;
			}
			uint32_t v[ 6 ] = {};
			for ( size_t k = 0; k < 6; k++ )
				if ( !parse_hex32( tok[ k ], v[ k ] ) )
				{
					error = where + "field " + std::to_string( k + 1 ) + " is not 1..8 hex digits";
					return false;
				}
			if ( !seen.insert( { v[ 0 ], v[ 1 ] } ).second )
			{
				error = where + "repeated (leaf, sub-leaf)";
				return false;
			}
			if ( out.leaves.size() >= max_profile_rows )
			{
				error = where + "more than " + std::to_string( max_profile_rows ) + " rows";
				return false;
			}
			uc_x86_cpuid e{};
			e.leaf = v[ 0 ];
			e.subleaf = v[ 1 ];
			e.eax = v[ 2 ];
			e.ebx = v[ 3 ];
			e.ecx = v[ 4 ];
			e.edx = v[ 5 ];
			out.leaves.push_back( e );
		}
		if ( out.leaves.empty() )
		{
			error = "no CPUID row";
			return false;
		}
		return true;
	}

	bool load_cpuid_profile_file( const std::string& path, cpuid_profile& out, std::string& error )
	{
		std::string text;
		if ( !read_input_file( path, max_profile_bytes, text, error ) ) return false;
		std::string why;
		if ( !parse_cpuid_profile( text, out, why ) )
		{
			error = path + ": " + why;
			return false;
		}
		out.path = path;
		out.file_bytes = uint64_t( text.size() );
		return true;
	}

	const char* profile_file_name( cpu_profile_choice choice )
	{
		switch ( choice )
		{
		case cpu_profile_choice::host: return "cpuid_i5-13600k_bare_host.txt";
		case cpu_profile_choice::guest: return "cpuid_i5-13600k.txt";
		default: return "";
		}
	}

	const char* profile_description( cpu_profile_choice choice )
	{
		switch ( choice )
		{
		case cpu_profile_choice::host: return "the captured bare-host profile (physical i5-13600K, P-core; default, decision 3.0.3)";
		case cpu_profile_choice::guest: return "the captured VMware-guest profile (decision 3.0.3)";
		case cpu_profile_choice::max: return "Unicorn's built-in UC_CPU_X86_MAX model without a CPUID profile (coverage, decision 3.0.3)";
		default: return "the running machine's CPUID (explicit option only, decision 3.0.3; not implemented in M2)";
		}
	}

	bool default_data_dir( std::string& out, std::string& error )
	{
		std::vector<char> buf( 32768 );
		const DWORD n = GetModuleFileNameA( nullptr, buf.data(), DWORD( buf.size() ) );
		if ( n == 0 || n >= buf.size() )
		{
			error = "GetModuleFileNameA failed";
			return false;
		}
		const std::filesystem::path exe( std::string( buf.data(), n ) );
		const auto data = ( exe.parent_path() / ".." / ".." / ".." / ".." / "Emulator" / "data" ).lexically_normal();
		std::error_code ec;
		if ( !std::filesystem::is_directory( data, ec ) )
		{
			error = "no Emulator\\data folder at " + data.string() + " (executables live in build\\x64\\<config>\\tests)";
			return false;
		}
		out = data.string();
		return true;
	}
}
