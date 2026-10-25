// emu::json (plan 3.M3, ledger U1972): see include/emu/json.hpp. Pure string handling.
#include <emu/json.hpp>

#include <cstdio>

namespace emu::json
{
	namespace
	{
		struct parser
		{
			std::string_view s;
			size_t i = 0;
			const limits& lim;
			size_t items = 0;
			std::string err;

			bool fail( const std::string& why )
			{
				if ( err.empty() ) err = "offset " + std::to_string( i ) + ": " + why;
				return false;
			}
			void ws()
			{
				while ( i < s.size() && ( s[ i ] == ' ' || s[ i ] == '\t' || s[ i ] == '\n' || s[ i ] == '\r' ) ) i++;
			}
			bool lit( std::string_view w )
			{
				if ( s.substr( i, w.size() ) != w ) return false;
				i += w.size();
				return true;
			}
			static int hexv( char c )
			{
				if ( c >= '0' && c <= '9' ) return c - '0';
				if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
				if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
				return -1;
			}
			static void put_utf8( std::string& o, uint32_t cp )
			{
				if ( cp < 0x80 ) o += char( cp );
				else if ( cp < 0x800 ) { o += char( 0xC0 | ( cp >> 6 ) ); o += char( 0x80 | ( cp & 0x3F ) ); }
				else if ( cp < 0x10000 ) { o += char( 0xE0 | ( cp >> 12 ) ); o += char( 0x80 | ( ( cp >> 6 ) & 0x3F ) ); o += char( 0x80 | ( cp & 0x3F ) ); }
				else
				{
					o += char( 0xF0 | ( cp >> 18 ) );
					o += char( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
					o += char( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
					o += char( 0x80 | ( cp & 0x3F ) );
				}
			}
			bool hex4( uint32_t& v )
			{
				if ( i + 4 > s.size() ) return fail( "truncated \\u escape" );
				v = 0;
				for ( int k = 0; k < 4; k++ )
				{
					const int d = hexv( s[ i + k ] );
					if ( d < 0 ) return fail( "bad \\u escape" );
					v = ( v << 4 ) | uint32_t( d );
				}
				i += 4;
				return true;
			}
			bool str( std::string& out )
			{
				// at '"'
				i++;
				out.clear();
				for ( ;; )
				{
					if ( i >= s.size() ) return fail( "unterminated string" );
					const unsigned char c = (unsigned char)s[ i ];
					if ( c == '"' ) { i++; return true; }
					if ( c < 0x20 ) return fail( "control character in a string" );
					if ( out.size() >= lim.max_string ) return fail( "string longer than " + std::to_string( lim.max_string ) + " bytes" );
					if ( c != '\\' ) { out += char( c ); i++; continue; }
					i++;
					if ( i >= s.size() ) return fail( "unterminated escape" );
					const char e = s[ i++ ];
					switch ( e )
					{
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'u':
					{
						uint32_t cp = 0;
						if ( !hex4( cp ) ) return false;
						if ( cp >= 0xD800 && cp < 0xDC00 )
						{
							uint32_t lo = 0;
							if ( !lit( "\\u" ) || !hex4( lo ) || lo < 0xDC00 || lo >= 0xE000 ) return fail( "unpaired surrogate" );
							cp = 0x10000 + ( ( cp - 0xD800 ) << 10 ) + ( lo - 0xDC00 );
						}
						else if ( cp >= 0xDC00 && cp < 0xE000 ) return fail( "unpaired surrogate" );
						put_utf8( out, cp );
						break;
					}
					default: i--; return fail( "bad escape" );
					}
				}
			}
			bool num( value& v )
			{
				const size_t b = i;
				if ( s[ i ] == '-' ) i++;
				if ( i >= s.size() || s[ i ] < '0' || s[ i ] > '9' ) return fail( "bad number" );
				if ( s[ i ] == '0' && i + 1 < s.size() && s[ i + 1 ] >= '0' && s[ i + 1 ] <= '9' ) return fail( "leading zero in a number" );
				while ( i < s.size() && s[ i ] >= '0' && s[ i ] <= '9' ) i++;
				if ( i < s.size() && ( s[ i ] == '.' || s[ i ] == 'e' || s[ i ] == 'E' ) )
					return fail( "only integers are accepted (no fraction / exponent: write large values as \"0x...\" strings)" );
				if ( i - b > 24 ) return fail( "number literal too long" );
				v.k = kind::number;
				v.text = std::string( s.substr( b, i - b ) );
				return true;
			}
			bool val( value& v, size_t depth )
			{
				ws();
				if ( i >= s.size() ) return fail( "value expected" );
				if ( depth > lim.max_depth ) return fail( "nesting deeper than " + std::to_string( lim.max_depth ) );
				if ( ++items > lim.max_items ) return fail( "more than " + std::to_string( lim.max_items ) + " values" );
				const char c = s[ i ];
				if ( c == '{' )
				{
					v.k = kind::object;
					i++;
					ws();
					if ( i < s.size() && s[ i ] == '}' ) { i++; return true; }
					for ( ;; )
					{
						ws();
						if ( i >= s.size() || s[ i ] != '"' ) return fail( "object key (a string) expected" );
						std::string key;
						if ( !str( key ) ) return false;
						for ( auto& k : v.keys )
							if ( k == key ) return fail( "duplicate key \"" + key + "\"" );
						ws();
						if ( i >= s.size() || s[ i ] != ':' ) return fail( "':' expected" );
						i++;
						value m;
						if ( !val( m, depth + 1 ) ) return false;
						v.keys.push_back( std::move( key ) );
						v.items.push_back( std::move( m ) );
						ws();
						if ( i < s.size() && s[ i ] == ',' ) { i++; continue; }
						if ( i < s.size() && s[ i ] == '}' ) { i++; return true; }
						return fail( "',' or '}' expected" );
					}
				}
				if ( c == '[' )
				{
					v.k = kind::array;
					i++;
					ws();
					if ( i < s.size() && s[ i ] == ']' ) { i++; return true; }
					for ( ;; )
					{
						value m;
						if ( !val( m, depth + 1 ) ) return false;
						v.items.push_back( std::move( m ) );
						ws();
						if ( i < s.size() && s[ i ] == ',' ) { i++; continue; }
						if ( i < s.size() && s[ i ] == ']' ) { i++; return true; }
						return fail( "',' or ']' expected" );
					}
				}
				if ( c == '"' )
				{
					v.k = kind::string;
					return str( v.text );
				}
				if ( lit( "true" ) ) { v.k = kind::boolean; v.b = true; return true; }
				if ( lit( "false" ) ) { v.k = kind::boolean; v.b = false; return true; }
				if ( lit( "null" ) ) { v.k = kind::null; return true; }
				if ( c == '-' || ( c >= '0' && c <= '9' ) ) return num( v );
				return fail( "unexpected character" );
			}
		};

		bool parse_u64_text( std::string_view t, uint64_t& out )
		{
			if ( t.empty() ) return false;
			uint64_t v = 0;
			if ( t.size() > 2 && t[ 0 ] == '0' && ( t[ 1 ] == 'x' || t[ 1 ] == 'X' ) )
			{
				t.remove_prefix( 2 );
				if ( t.empty() || t.size() > 16 ) return false;
				for ( char c : t )
				{
					const int d = parser::hexv( c );
					if ( d < 0 ) return false;
					v = ( v << 4 ) | uint64_t( d );
				}
			}
			else
			{
				for ( char c : t )
				{
					if ( c < '0' || c > '9' ) return false;
					const uint64_t d = uint64_t( c - '0' );
					if ( v > ( UINT64_MAX - d ) / 10 ) return false;
					v = v * 10 + d;
				}
			}
			out = v;
			return true;
		}
	}

	const value* value::find( std::string_view key ) const
	{
		if ( k != kind::object ) return nullptr;
		for ( size_t n = 0; n < keys.size(); n++ )
			if ( keys[ n ] == key ) return &items[ n ];
		return nullptr;
	}

	bool parse( std::string_view text, value& out, std::string& error, const limits& lim )
	{
		out = value{};
		if ( text.size() > lim.max_bytes )
		{
			error = "document larger than " + std::to_string( lim.max_bytes ) + " bytes";
			return false;
		}
		parser p{ text, 0, lim };
		if ( !p.val( out, 0 ) )
		{
			error = p.err;
			return false;
		}
		p.ws();
		if ( p.i != text.size() )
		{
			error = "offset " + std::to_string( p.i ) + ": trailing characters after the document";
			return false;
		}
		return true;
	}

	std::string quote( std::string_view s )
	{
		std::string o = "\"";
		for ( unsigned char c : s )
		{
			if ( c == '"' ) o += "\\\"";
			else if ( c == '\\' ) o += "\\\\";
			else if ( c < 0x20 )
			{
				char b[ 8 ];
				std::snprintf( b, sizeof( b ), "\\u%04x", c );
				o += b;
			}
			else o += char( c );
		}
		return o + "\"";
	}

	bool get_u64( const value& v, uint64_t& out, std::string& error )
	{
		if ( v.k == kind::number )
		{
			if ( !v.text.empty() && v.text[ 0 ] == '-' ) { error = "negative number " + v.text + " where an unsigned value is wanted"; return false; }
			if ( !parse_u64_text( v.text, out ) ) { error = "number " + v.text + " outside 0..2^64-1"; return false; }
			return true;
		}
		if ( v.k == kind::string )
		{
			if ( !parse_u64_text( v.text, out ) ) { error = "\"" + v.text + "\" is not an unsigned 64-bit number (decimal or 0x hex, at most 16 hex digits)"; return false; }
			return true;
		}
		error = "a number or a \"0x...\" string expected";
		return false;
	}

	bool get_i64( const value& v, int64_t& out, std::string& error )
	{
		if ( v.k == kind::number && !v.text.empty() && v.text[ 0 ] == '-' )
		{
			uint64_t m = 0;
			if ( !parse_u64_text( std::string_view( v.text ).substr( 1 ), m ) || m > uint64_t( INT64_MAX ) + 1 ) { error = "number " + v.text + " out of range"; return false; }
			out = m == uint64_t( INT64_MAX ) + 1 ? INT64_MIN : -int64_t( m );
			return true;
		}
		uint64_t u = 0;
		if ( !get_u64( v, u, error ) ) return false;
		if ( u > uint64_t( INT64_MAX ) ) { error = "value above 2^63-1"; return false; }
		out = int64_t( u );
		return true;
	}
}
