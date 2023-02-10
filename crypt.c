/** @file
 * @brief crypto functions
 */

#include <stdint.h>
#include <string.h>
//#include <openssl/sha.h>

#include "crypt.h"

static volatile int b64_encode_inited = 0;
static volatile unsigned char b64_encode_table[64];
static const uint32_t b64_mask = 63;

static void init_b64_encode_table()
{
	if (__atomic_exchange_n(&b64_encode_inited, (int)1, __ATOMIC_SEQ_CST))
		return;

	for (int i = 0; i < 26; i++) {
		b64_encode_table[i] = 'A' + i;
		b64_encode_table[26 + i] = 'a' + i;
	}
	for (int i = 0; i < 10; i++) {
		b64_encode_table[52 + i] = '0' + i;
	}
	b64_encode_table[62] = '+';
	b64_encode_table[63] = '/';
}

static void b64_encode_triplet(unsigned char *out, unsigned char in0, unsigned char in1, unsigned char in2)
{
	uint32_t triplet = ((uint32_t)in0 << 16) + ((uint32_t)in1 << 8) + (uint32_t)in2;
	for (int i = 0; i < 4; i++) {
		uint32_t b64_val = (triplet >> (6*(3 - i))) & b64_mask;
		out[i] = b64_encode_table[b64_val];
	}
}

/* out must be large enough to hold output + 1 for null terminator */
void b64_encode(unsigned char *out, const unsigned char *in, size_t in_bytes)
{
	init_b64_encode_table();

	while (in_bytes >= 3) {
		b64_encode_triplet(out, in[0], in[1], in[2]);
		in_bytes -= 3;
		in += 3;
		out += 4;
	}

	if (in_bytes == 0) {
		out[0] = '\0';
	} else if (in_bytes == 1) {
		b64_encode_triplet(out, in[0], 0, 0);
		out[2] = '=';
		out[3] = '=';
		out[4] = '\0';
	} else if (in_bytes == 2) {
		b64_encode_triplet(out, in[0], in[1], 0);
		out[3] = '=';
		out[4] = '\0';
	}
}

static inline uint32_t left_rotate(uint32_t w, uint32_t amount)
{
	return (w << amount) | (w >> (32U - amount));
}

static void sha1_wordgen(uint32_t *const word)
{
	for (int i = 16; i < 80; i++) {
		word[i] = left_rotate(word[i-3] ^ word[i-8] ^ word[i-14] ^ word[i-16], 1);
	}
}

static void sha1_total_len_cpy(uint8_t *const byte, const uint64_t total_len)
{
	uint64_t mask = 0xFF;
	for (int i = 0; i < 8; i++) {
		byte[56 + i] = (uint8_t)((total_len >> 8U*(8U - i - 1U)) & mask);
	}
}

static void sha1_mkwords(uint8_t *const byte, const uint8_t *const in, const size_t len, const int mode, const uint64_t total_len)
{
	switch (mode) {
	case 0:
		if (len < 56) {
			for (size_t i = 0; i < len; i++) {
				byte[i] = in[i];
			}
			byte[len] = 0x80;
			for (size_t i = len + 1; i < 56; i++) {
				byte[i] = 0;
			}
			sha1_total_len_cpy(byte, total_len);
		} else if (len < 64) {
			for (size_t i = 0; i < len; i++) {
				byte[i] = in[i];
			}
			byte[len] = 0x80;
			for (size_t i = len + 1; i < 64; i++) {
				byte[i] = 0;
			}
		} else {
			for (size_t i = 0; i < 64; i++) {
				byte[i] = in[i];
			}
		}
		break;

	case 1:
		/* needs total_len appended only */
		sha1_total_len_cpy(byte, total_len);
		break;
	}

	sha1_wordgen((uint32_t *)byte);
}

void sha1sum(unsigned char *out, const unsigned char *in, size_t len)
{
	//SHA1(in, len, out);
	//return;

	const uint8_t *in_byte = (uint8_t *)in;
	uint8_t *const out_byte = (uint8_t *)out;
	const uint64_t total_len = (uint64_t)len;

	const uint32_t K[4] = {
		0x5A827999,
		0x6ED9EBA1,
		0x8F1BBCDC,
		0xCA62C1D6
	};

	uint32_t h[5] = {
		0x67452301,
		0xEFCDAB89,
		0x98BADCFE,
		0x10325476,
		0xC3D2E1F0
	};

	uint32_t word[80];
	int mode = 0;
	while(1) {
		sha1_mkwords((uint8_t *)word, in_byte, len, mode, total_len);

		uint32_t A = h[0];
		uint32_t B = h[1];
		uint32_t C = h[2];
		uint32_t D = h[3];
		uint32_t E = h[4];

		for (int i = 0; i < 20; i++) {
			uint32_t temp = left_rotate(A, 5) + ((B & C) | ((~B) & D)) + E + word[i] + K[0];
			E = D;
			D = C;
			C = left_rotate(B, 30);
			B = A;
			A = temp;
		}
		for (int i = 20; i < 40; i++) {
			uint32_t temp = left_rotate(A, 5) + (B ^ C ^ D) + E + word[i] + K[1];
			E = D;
			D = C;
			C = left_rotate(B, 30);
			B = A;
			A = temp;
		}
		for (int i = 40; i < 60; i++) {
			uint32_t temp = left_rotate(A, 5) + ((B & C) | (B & D) | (C & D)) + E + word[i] + K[2];
			E = D;
			D = C;
			C = left_rotate(B, 30);
			B = A;
			A = temp;
		}
		for (int i = 60; i < 80; i++) {
			uint32_t temp = left_rotate(A, 5) + (B ^ C ^ D) + E + word[i] + K[3];
			E = D;
			D = C;
			C = left_rotate(B, 30);
			B = A;
			A = temp;
		}

		h[0] += A;
		h[1] += B;
		h[2] += C;
		h[3] += D;
		h[4] += E;

		if (mode != 0) {
			break;
		}

		if (len < 56) {
			break;
		} else if (len < 64) {
			mode = 1;
		} else {
			len -= 64;
			in_byte += 64;
		}
	}

	uint32_t mask = 0xFF;
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < 4; j++) {
			out_byte[4*i + j] = (uint8_t)((h[i] >> 8U*(4U - j - 1U)) & mask);
		}
	}
}
