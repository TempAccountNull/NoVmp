// emu::input_file (plan 3.M3, ledger U1972): see include/emu/input_file.hpp. The audited file input of the Emulator lib: the
// only std::ifstream of the Emulator projects (no_native_check rule group "fileio": read-only opens here, nothing elsewhere).
#include <emu/input_file.hpp>

#include <fstream>
#include <vector>

namespace emu
{
	bool read_input_file( const std::string& path, uint64_t max_bytes, std::string& out, std::string& error )
	{
		out.clear();
		std::ifstream in( path, std::ios::binary );
		if ( !in )
		{
			error = "cannot open " + path;
			return false;
		}
		in.seekg( 0, std::ios::end );
		const std::streamoff size = in.tellg();
		if ( size < 0 || uint64_t( size ) > max_bytes )
		{
			error = path + ": size " + std::to_string( (long long)size ) + " outside 0.." + std::to_string( max_bytes ) + " bytes";
			return false;
		}
		in.seekg( 0, std::ios::beg );
		out.assign( size_t( size ), '\0' );
		if ( size > 0 && !in.read( out.data(), size ) )
		{
			out.clear();
			error = "cannot read " + path;
			return false;
		}
		return true;
	}

	bool read_input_lines( const std::string& path, uint64_t max_line, uint64_t max_total,
						   const std::function<bool( std::string_view, uint64_t )>& line, std::string& error )
	{
		std::ifstream in( path, std::ios::binary );
		if ( !in )
		{
			error = "cannot open " + path;
			return false;
		}
		std::vector<char> buf( 1u << 16 );
		std::string cur;
		uint64_t total = 0, line_no = 1;
		for ( ;; )
		{
			in.read( buf.data(), std::streamsize( buf.size() ) );
			const std::streamsize got = in.gcount();
			if ( got <= 0 ) break;
			total += uint64_t( got );
			if ( total > max_total )
			{
				error = path + ": more than " + std::to_string( max_total ) + " bytes";
				return false;
			}
			for ( std::streamsize k = 0; k < got; k++ )
			{
				const char c = buf[ size_t( k ) ];
				if ( c == '\0' )
				{
					error = path + ": line " + std::to_string( line_no ) + ": NUL byte";
					return false;
				}
				if ( c != '\n' )
				{
					if ( cur.size() >= max_line )
					{
						error = path + ": line " + std::to_string( line_no ) + " longer than " + std::to_string( max_line ) + " bytes";
						return false;
					}
					cur += c;
					continue;
				}
				if ( !cur.empty() && cur.back() == '\r' ) cur.pop_back();
				if ( !line( cur, line_no ) ) return true;
				cur.clear();
				line_no++;
			}
		}
		if ( !in.eof() )
		{
			error = "cannot read " + path;
			return false;
		}
		if ( !cur.empty() )
		{
			if ( cur.back() == '\r' ) cur.pop_back();
			line( cur, line_no );
		}
		return true;
	}
}
