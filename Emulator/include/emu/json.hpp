// emu::json (plan 3.M3, ledger U1972): a small, strict JSON reader for emu-snippet's state / spec files ("state by flags or
// JSON", plan 2.7) and the batch files of the replay (one JSON document per line). Strict and bounded (AGENT_RULES "Host
// safety": every input is bounded and fails loudly): RFC 8259 syntax only (no comments, no trailing commas, no NaN), numbers
// are INTEGERS only (a fraction or an exponent is an error: register values never pass through a double), duplicate object
// keys are an error, the nesting depth, the number of items and the document size are limited, and every error names its
// byte offset. Values keep their order.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace emu::json
{
	enum class kind { null, boolean, number, string, array, object };

	struct value
	{
		kind k = kind::null;
		bool b = false;
		std::string text;                 // number: the literal ("-12", "4096"); string: the decoded UTF-8 text
		std::vector<value> items;         // array elements, or the object's values
		std::vector<std::string> keys;    // object keys (same order as items)

		bool is( kind x ) const { return k == x; }
		// the member KEY of an object (nullptr: not an object or no such key)
		const value* find( std::string_view key ) const;
	};

	struct limits
	{
		uint64_t max_bytes = 64ull << 20;   // one document
		size_t max_depth = 64;
		size_t max_items = 1u << 22;        // array elements + object members, whole document
		size_t max_string = 16u << 20;      // one string (decoded bytes)
	};

	// parse exactly one document (surrounding white space allowed); false with "offset N: reason" on the first problem
	bool parse( std::string_view text, value& out, std::string& error, const limits& lim = {} );

	// a JSON string literal (quotes, escapes; control characters as \u00XX) for output
	std::string quote( std::string_view s );

	// typed helpers (false with a reason): an unsigned 64-bit number given as a JSON integer or as a string "0x..." / decimal
	bool get_u64( const value& v, uint64_t& out, std::string& error );
	bool get_i64( const value& v, int64_t& out, std::string& error );
}
