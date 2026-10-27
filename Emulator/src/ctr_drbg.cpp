// emu::ctr_drbg (ledger U2093): see emu/ctr_drbg.hpp. AES-128 per FIPS 197 (5.1 Cipher: SubBytes / ShiftRows / MixColumns /
// AddRoundKey, Nr = 10; 5.2 KeyExpansion with Rcon; the S-box computed from its definition in 5.1.1 - the multiplicative inverse in
// GF(2^8) followed by the affine transformation - instead of a typed table); CTR_DRBG per NIST SP 800-90A Rev. 1 10.2.1.
#include <emu/ctr_drbg.hpp>

#include <cstring>

namespace emu
{
	namespace
	{
		uint8_t xtime( uint8_t b ) { return uint8_t( ( b << 1 ) ^ ( ( b & 0x80 ) ? 0x1B : 0 ) ); }   // FIPS 197 4.2.1
		uint8_t gmul( uint8_t a, uint8_t b )
		{
			uint8_t r = 0;
			for ( ; b; b >>= 1, a = xtime( a ) )
				if ( b & 1 ) r ^= a;
			return r;
		}
		// FIPS 197 5.1.1: S(b) = affine( b^-1 ), 0 -> 0
		struct sbox_table
		{
			uint8_t s[ 256 ];
			sbox_table()
			{
				for ( int x = 0; x < 256; x++ )
				{
					uint8_t inv = 0;
					for ( int y = 1; y < 256 && x; y++ )
						if ( gmul( uint8_t( x ), uint8_t( y ) ) == 1 )
						{
							inv = uint8_t( y );
							break;
						}
					uint8_t b = inv, r = 0x63;
					for ( int i = 0; i < 5; i++ ) r ^= uint8_t( ( b << i ) | ( b >> ( 8 - i ) ) );   // b ^ rotl1 ^ rotl2 ^ rotl3 ^ rotl4 ^ 63H
					s[ x ] = r;
				}
			}
		};
		const sbox_table& sbox()
		{
			static const sbox_table t;
			return t;
		}
	}

	void aes128_encrypt( const uint8_t key[ 16 ], const uint8_t in[ 16 ], uint8_t out[ 16 ] )
	{
		const uint8_t* S = sbox().s;
		uint8_t w[ 176 ];   // FIPS 197 5.2: Nb (Nr + 1) = 44 words
		std::memcpy( w, key, 16 );
		uint8_t rcon = 1;
		for ( int i = 4; i < 44; i++ )
		{
			uint8_t t[ 4 ] = { w[ 4 * i - 4 ], w[ 4 * i - 3 ], w[ 4 * i - 2 ], w[ 4 * i - 1 ] };
			if ( i % 4 == 0 )
			{
				const uint8_t t0 = t[ 0 ];   // RotWord, SubWord, Rcon
				t[ 0 ] = uint8_t( S[ t[ 1 ] ] ^ rcon );
				t[ 1 ] = S[ t[ 2 ] ];
				t[ 2 ] = S[ t[ 3 ] ];
				t[ 3 ] = S[ t0 ];
				rcon = xtime( rcon );
			}
			for ( int j = 0; j < 4; j++ ) w[ 4 * i + j ] = uint8_t( w[ 4 * i - 16 + j ] ^ t[ j ] );
		}
		uint8_t s[ 16 ];   // the state, column-major as FIPS 197 3.4 (s[r + 4c])
		for ( int i = 0; i < 16; i++ ) s[ i ] = uint8_t( in[ i ] ^ w[ i ] );
		for ( int round = 1; round <= 10; round++ )
		{
			uint8_t t[ 16 ];
			for ( int c = 0; c < 4; c++ )
				for ( int r = 0; r < 4; r++ ) t[ r + 4 * c ] = S[ s[ r + 4 * ( ( c + r ) % 4 ) ] ];   // SubBytes + ShiftRows
			if ( round < 10 )
				for ( int c = 0; c < 4; c++ )   // MixColumns (5.1.3)
				{
					const uint8_t a0 = t[ 4 * c ], a1 = t[ 4 * c + 1 ], a2 = t[ 4 * c + 2 ], a3 = t[ 4 * c + 3 ];
					t[ 4 * c ] = uint8_t( xtime( a0 ) ^ ( xtime( a1 ) ^ a1 ) ^ a2 ^ a3 );
					t[ 4 * c + 1 ] = uint8_t( a0 ^ xtime( a1 ) ^ ( xtime( a2 ) ^ a2 ) ^ a3 );
					t[ 4 * c + 2 ] = uint8_t( a0 ^ a1 ^ xtime( a2 ) ^ ( xtime( a3 ) ^ a3 ) );
					t[ 4 * c + 3 ] = uint8_t( ( xtime( a0 ) ^ a0 ) ^ a1 ^ a2 ^ xtime( a3 ) );
				}
			for ( int i = 0; i < 16; i++ ) s[ i ] = uint8_t( t[ i ] ^ w[ 16 * round + i ] );   // AddRoundKey
		}
		std::memcpy( out, s, 16 );
	}

	namespace
	{
		void inc128( std::array<uint8_t, 16>& v )   // V = (V + 1) mod 2^128, big-endian
		{
			for ( int j = 15; j >= 0; j-- )
				if ( ++v[ size_t( j ) ] ) break;
		}
	}

	// SP 800-90A 10.2.1.2 CTR_DRBG_Update (seedlen 256)
	void ctr_drbg::update( const uint8_t* provided )
	{
		uint8_t temp[ 32 ];
		for ( int i = 0; i < 32; i += 16 )
		{
			inc128( v_ );
			aes128_encrypt( key_.data(), v_.data(), temp + i );
		}
		for ( int i = 0; i < 32; i++ ) temp[ i ] ^= provided ? provided[ i ] : 0;
		std::memcpy( key_.data(), temp, 16 );
		std::memcpy( v_.data(), temp + 16, 16 );
	}

	// SP 800-90A 10.2.1.3.1 Instantiate without a derivation function: seed_material = entropy_input XOR personalization (padded),
	// Key = 0^128, V = 0^128, Update( seed_material )
	ctr_drbg::ctr_drbg( uint64_t seed, const std::string& personalization )
	{
		uint8_t m[ 32 ] = {};
		for ( int i = 0; i < 8; i++ ) m[ i ] = uint8_t( seed >> ( 8 * i ) );
		for ( size_t i = 0; i < personalization.size() && i < 32; i++ ) m[ i ] ^= uint8_t( personalization[ i ] );
		update( m );
	}

	// SP 800-90A 10.2.1.5.1 Generate without a derivation function, no additional input, 64 bits
	uint64_t ctr_drbg::next()
	{
		uint8_t out[ 16 ];
		inc128( v_ );
		aes128_encrypt( key_.data(), v_.data(), out );
		update( nullptr );
		n_++;
		uint64_t r = 0;
		for ( int i = 0; i < 8; i++ ) r |= uint64_t( out[ i ] ) << ( 8 * i );
		return r;
	}
}
