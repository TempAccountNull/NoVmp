// emu::ctr_drbg (plan 3.M6 / 1.K.11 P14, ledger U2093): the Emulator lib's own CTR_DRBG - NIST SP 800-90A Rev. 1 section 10.2.1
// with AES-128 and no derivation function (seedlen 256 = keylen 128 + blocklen 128, Table 3; 10.2.1.2 CTR_DRBG_Update,
// 10.2.1.3.1 Instantiate, 10.2.1.5.1 Generate), on its own AES-128 encryption (FIPS 197: 5.1 Cipher, 5.2 KeyExpansion; known
// answer Appendix C.1). Written independently of the fork's model (int_helper.c, U2091, QEMU crypto/aes.c) so the two cross-check
// each other (emu-selftest engine_rng: equal sequences for the same seed).
//
// The model's definition (the same as the fork's seeded RDRAND source, notes/eread/P42_phase3_m6.md): entropy_input = the 64-bit
// seed little-endian || 24 zero bytes, no nonce, personalization_string = PERS (empty for the CPU source; the environment's own
// random values use "NoVmp environment"), no reseed; next() = one Generate of 64 bits without additional input, the 8 bytes read
// little-endian.
// Uses: the emulated kernel's random values (the process cookie, nt_kernel_sys.cpp) - "any emulated random source" of --seeded
// (decision D8; --HostSeed changes only the CPU's RDRAND / RDSEED, which the fork executes on the host DRNG: the environment
// never reads host randomness, AGENT_RULES "Host safety").
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace emu
{
	// FIPS 197 AES-128 encryption of one block (the key expanded per call: the DRBG changes its key on every Update)
	void aes128_encrypt( const uint8_t key[ 16 ], const uint8_t in[ 16 ], uint8_t out[ 16 ] );

	class ctr_drbg
	{
	public:
		explicit ctr_drbg( uint64_t seed, const std::string& personalization = "" );
		uint64_t next();                      // one CTR_DRBG_Generate of 64 bits (little-endian)
		uint64_t draws() const { return n_; }  // values generated since the instantiation

	private:
		void update( const uint8_t* provided );   // nullptr = 0^seedlen
		std::array<uint8_t, 16> key_{}, v_{};
		uint64_t n_ = 0;
	};

	// the personalization string of the environment's own random values (P42)
	inline constexpr const char* environment_personalization = "NoVmp environment";
}
