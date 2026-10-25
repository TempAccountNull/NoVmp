// emu::input_file (plan 3.M3, ledger U1972): the ONE file input of the Emulator lib. Every file the lib or its tools read
// (the captured CPUID profiles of decision 3.0.3, emu-snippet's --state / --batch files) goes through these two functions:
// read-only, binary, size-bounded, never executed, never mapped executable (AGENT_RULES "Host safety"). Nothing in the lib
// writes, creates, renames or removes a file. Checked by no_native_check (rule group "fileio": a file open anywhere else in
// the Emulator projects, or anything but a read-only open here, fails the suite).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace emu
{
	// the whole file PATH into OUT; false (with a reason naming the path) when it cannot be opened / read or is larger than
	// MAX_BYTES
	bool read_input_file( const std::string& path, uint64_t max_bytes, std::string& out, std::string& error );

	// the file line by line (lines end at LF; one trailing CR is removed): LINE(text, line_number) for each, false from the
	// callback stops the read (the function then returns true). A line longer than MAX_LINE bytes, more than MAX_TOTAL bytes
	// in all, or a NUL byte is an error with the line number.
	bool read_input_lines( const std::string& path, uint64_t max_line, uint64_t max_total,
						   const std::function<bool( std::string_view, uint64_t )>& line, std::string& error );
}
