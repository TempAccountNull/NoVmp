// pg_randomize.hpp - the Randomizer of ProfileGenerator (plan 1.J.49, ledger U1513-U1514): the ONE place that produces
// randomness. Every synthetic value of a NEW identity comes from RDRAND (pg_rand.asm) - never from a seed or a software
// CSPRNG - and leaves here as a finished value in its final Windows text form (const char*): "DESKTOP-XXXXXXX", a GUID,
// "XX:XX:XX:XX:XX:XX", "S-1-5-21-...", a 63-digit installation ID, hex text for binary fields (an SMBIOS UUID). The
// format-preserving rules take the captured value as the pattern (const char*) and return a new value of the same format
// that differs from it. No other function of the generator draws random numbers.
// Design: the user's rid demo (randomize.hpp / .cpp: a singleton with format primitives, reached as
// Randomize->gen_<what>(), thread-local result buffers, '?'-pattern fill with rejection sampling, the installation-ID
// check digit) - re-implemented in the emulator's style (exceptions instead of null pointers).
//
//     if ( !Randomize->cpu_supports_rand ) ...                     CPUID.1:ECX[30], read once
//     const char* v = Randomize->gen_like( "AB12cd-345_Z" );       "QK07xw-918_D": classes per position kept
//     const char* g = Randomize->gen_guid_like( "{0F0F...}", nullptr );
//
// Threading (plan 1.J.49.a, user decision 2026-10-17 "Per-build context"): the Randomizer is fully thread-safe and
// lock-free - every member may be called from any thread at any time. get_instance() is a magic static (C++11 thread-safe
// initialization; cpu_supports_rand is written once there and only read afterwards); RDRAND itself is per-thread safe (each
// hardware thread draws from the DRNG independently); the byte pool of gen_char() is a thread_local named struct
// (BytePool in pg_randomize.cpp), so no state of the instance is ever written after construction.
// Every const char* result is a thread-local buffer of that function: valid until the SAME function is called again on
// the SAME thread (another thread never overwrites it) - copy it to keep it (ProfileGenerator interns every value in its
// process-lifetime value pool). No fallback: without RDRAND, or when RDRAND fails 10 times in a row, every gen_ throws
// std::runtime_error and the generation stops cleanly. APPLYING a saved identity map needs no randomness at all.
#pragma once
#include <cstdint>
#include <string>

namespace emu::platform
{
	class Randomizer
	{
	public:
		static Randomizer& get_instance();
		Randomizer( const Randomizer& ) = delete;
		Randomizer& operator=( const Randomizer& ) = delete;

		const bool cpu_supports_rand;	// CPUID.1:ECX[30], checked once when the instance is created

		// ── primitives (rejection sampling throughout: no modulo bias) ─────────────────────────────────────────────
		uint64_t gen_u64();								// 64 bits, never 0 or ~0 (pg_rdrand64)
		uint64_t gen_below( uint64_t n );				// uniform in [0, n), n >= 1
		int64_t gen_in_range( int64_t lo, int64_t hi );	// uniform in [lo, hi], lo <= hi
		char gen_char( const char* set );				// one character of set (1..256 characters)
		const char* gen_fill( const char* pattern, const char* set );	// every '?' -> a character of set (the demo's fill)
		const char* gen_decimal_in_range( int64_t lo, int64_t hi );		// a number in [lo, hi] as decimal text (an offset in seconds)

		// ── format-preserving values: the result has the captured value's format and differs from it ─────────────
		// rule-2 classes: digit -> digit, A-Z -> A-Z, a-z -> a-z per position, every other character kept
		const char* gen_like( const char* real );
		// hex digits in the value's case, other characters kept; keep_digit_class: digits stay digits, letters stay A-F / a-f
		const char* gen_hex_like( const char* real, bool keep_digit_class );
		// a USB / HID serial: hex-encoded printable ASCII stays hex-encoded ASCII (the text gen_like), other all-hex
		// serials gen_hex_like( digit classes kept ), anything else gen_like
		const char* gen_usb_serial_like( const char* real );
		// "DESKTOP-" + 7 of [A-Z0-9], not avoid
		const char* gen_computer_name( const char* avoid );
		// "XX:XX:XX:XX:XX:XX": the OUI (bytes 0-2) kept, bytes 3-5 random with room for max_delta above them
		const char* gen_mac_like( const char* real, unsigned max_delta );
		// a GUID text (braces, hyphens, case kept): the version digit and the variant bits kept; an MBR-style volume GUID
		// {SSSSSSSS-0000-0000-0000-OOOOOOOOOOOO}: only the disk signature; a version-1 GUID: node = node (12 hex digits),
		// or Windows' no-NIC node 806E6F6E6963 kept when node is null; any other GUID: every remaining digit random.
		// (Since U1516 ProfileGenerator::gen_guid no longer takes RFC 4122 version-1 GUIDs from here: their timestamp is the
		// real one + the install-history offset, only the clock sequence comes from gen_clock_seq_like.)
		const char* gen_guid_like( const char* real, const char* node );
		// the clock sequence of a version-1 GUID (its 4th group: 4 hex digits, the real one's case - upper case when it has no
		// letter; ProfileGenerator writes it in the GUID's case anyway): the variant bits of the
		// first digit kept (0xxx / 10xx / 110x / 111x), every other bit random, differs from real (U1516: the 60-bit
		// timestamp of a version-1 GUID is no random value - ProfileGenerator derives it from the install-history offset)
		const char* gen_clock_seq_like( const char* real );
		// SMBIOS type 1 UUID as 32 hex digits (raw SMBIOS byte order): vmware = bytes 0-1 kept (VMware's 56 4D), else the
		// RFC 4122 version nibbles (bytes 6, 7) and the variant bits (byte 8) kept
		const char* gen_system_uuid_like( const char* real_hex, bool vmware );
		// a decimal number of the same digit count (no leading zero), at most max
		const char* gen_decimal_like( const char* real, uint64_t max );
		// the EDID serial field (bytes 12-15) as its decimal value: four printable letters / digits get gen_like on the
		// text, any other value gen_decimal_like( 4294967295 )
		const char* gen_edid_serial_like( const char* real_decimal );
		// "S-1-5-21-a-b-c": each sub-authority a decimal of the same digit count (no leading zero, <= 4294967295)
		const char* gen_sid_like( const char* prefix );
		// the Windows installation ID: 63 digits = 9 groups of six random digits + their check digit (the demo's rule
		// (d0 + 2 d1 + d2 + 2 d3 + d4 + 2 d5) mod 7), separators of real kept
		const char* gen_installation_id_like( const char* real );
		static char check_digit( const char* six_digits );	// '0'..'6'

	private:
		Randomizer();
		uint8_t byte();				// from this thread's pool of 8 bytes per RDRAND draw (thread_local BytePool, pg_randomize.cpp)
	};

	// the global handle (the demo's spelling): Randomize->gen_like( ... ), Randomize.get_instance().gen_like( ... )
	struct RandomizeHandle
	{
		static Randomizer& get_instance() { return Randomizer::get_instance(); }
		Randomizer* operator->() const { return &Randomizer::get_instance(); }
	};
	inline constexpr RandomizeHandle Randomize {};
}
