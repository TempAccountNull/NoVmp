// emu::output_file (plan 3.M5 part 2, ledger U2020): see include/emu/output_file.hpp. The audited file output of the Emulator
// lib: the only std::ofstream of the Emulator projects (no_native_check: write-only creation here, nothing elsewhere).
#include <emu/output_file.hpp>

#include <fstream>

namespace emu
{
	struct output_file::impl
	{
		std::ofstream out;
	};

	output_file::output_file() : p_( std::make_unique<impl>() ) {}
	output_file::~output_file() { close(); }

	bool output_file::create( const std::string& path, uint64_t max_bytes, std::string& error )
	{
		close();
		p_->out.open( path, std::ios::binary | std::ios::trunc );
		if ( !p_->out )
		{
			error = "cannot create " + path;
			return false;
		}
		max_ = max_bytes;
		bytes_ = 0;
		truncated_ = false;
		return true;
	}

	bool output_file::write( const std::string& text )
	{
		if ( !p_->out.is_open() || truncated_ ) return false;
		if ( bytes_ + text.size() > max_ )
		{
			static const char cut[] = "TRUNCATED: the output file reached its size limit\n";
			p_->out.write( cut, sizeof( cut ) - 1 );
			truncated_ = true;
			return false;
		}
		p_->out.write( text.data(), std::streamsize( text.size() ) );
		bytes_ += text.size();
		return bool( p_->out );
	}

	void output_file::close()
	{
		if ( p_ && p_->out.is_open() )
		{
			p_->out.flush();
			p_->out.close();
		}
	}

	bool output_file::is_open() const { return p_ && p_->out.is_open(); }
}
