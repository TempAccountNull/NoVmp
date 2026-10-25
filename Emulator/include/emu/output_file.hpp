// emu::output_file (plan 3.M5 part 2, ledger U2020): the audited file OUTPUT of the Emulator lib - the one place a file is
// created for writing (no_native_check rule group "fileio": the only std::ofstream of the Emulator projects, in
// src/output_file.cpp). Used for emu-oep --trace FILE: text lines the emulator produces (never guest bytes executed, never a
// file the guest names - the path comes from the command line), bounded by max_bytes (the disk rule), then marked truncated.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace emu
{
	class output_file
	{
	public:
		output_file();
		~output_file();
		output_file( const output_file& ) = delete;
		output_file& operator=( const output_file& ) = delete;

		// create / truncate PATH; false with the reason
		bool create( const std::string& path, uint64_t max_bytes, std::string& error );
		// append TEXT; past max_bytes nothing more is written (one "TRUNCATED" line is) and false is returned
		bool write( const std::string& text );
		void close();
		bool is_open() const;
		bool truncated() const { return truncated_; }
		uint64_t bytes() const { return bytes_; }

	private:
		struct impl;
		std::unique_ptr<impl> p_;
		uint64_t max_ = 0, bytes_ = 0;
		bool truncated_ = false;
	};
}
