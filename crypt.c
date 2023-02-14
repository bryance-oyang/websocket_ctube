/**
 * @file
 * @brief crypto functions
 */

#include <stdint.h>

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
	return (w << amount) | (w >> (32 - amount));
}

static inline void sha1_wordgen(uint32_t *const word)
{
	for (int i = 16; i < 80; i++) {
		word[i] = left_rotate(word[i-3] ^ word[i-8] ^ word[i-14] ^ word[i-16], 1);
	}
}

static inline void sha1_total_len_cpy(uint32_t *words, const uint64_t total_bits)
{
	const uint64_t mask = 0xFF;

	words[14] = 0;
	words[15] = 0;
	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 4; j++) {
			uint32_t byte = (total_bits >> 8*(8 - (4*i+j) - 1)) & mask;
			words[14 + i] |= byte << 8*(4 - j - 1);
		}
	}
}

static inline void sha1_cp_to_words(uint32_t *const words, const uint8_t *const in, const size_t len, int pad)
{
	size_t i, w_ind, byte_ind;

	for (i = 0, w_ind = 0, byte_ind = 0; i < len; i++, w_ind = i / 4, byte_ind = i % 4) {
		uint32_t tmp = in[i];
		words[w_ind] |= tmp << 8*(4 - byte_ind - 1);
	}
	if (pad) {
		uint32_t one = 0x80;
		words[w_ind] |= one << 8*(4 - byte_ind - 1);
	}
}

static inline void sha1_mkwords(uint32_t *const words, const uint8_t *const in, const size_t len, const int mode, const uint64_t total_bits)
{
	for (int i = 0; i < 16; i++) {
		words[i] = 0;
	}

	switch (mode) {
	case 0:
		if (len < 56) {
			sha1_cp_to_words(words, in, len, 1);
			sha1_total_len_cpy(words, total_bits);
		} else if (len < 64) {
			sha1_cp_to_words(words, in, len, 1);
		} else {
			sha1_cp_to_words(words, in, 64, 0);
		}
		break;

	case 1:
		/* needs total_len appended only */
		sha1_total_len_cpy(words, total_bits);
		break;
	}

	sha1_wordgen(words);
}

void sha1sum(unsigned char *out, const unsigned char *in, size_t len_bytes)
{
	const uint8_t *in_byte = (uint8_t *)in;
	uint8_t *const out_byte = (uint8_t *)out;
	const uint64_t total_bits = ((uint64_t)8 * (uint64_t)len_bytes);

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

	uint32_t words[80];
	int mode = 0;
	while(1) {
		sha1_mkwords(words, in_byte, len_bytes, mode, total_bits);

		uint32_t A = h[0];
		uint32_t B = h[1];
		uint32_t C = h[2];
		uint32_t D = h[3];
		uint32_t E = h[4];

		for (int i = 0; i < 20; i++) {
			uint32_t temp = left_rotate(A, 5) + ((B & C) | ((~B) & D)) + E + words[i] + K[0];
			E = D;
			D = C;
			C = left_rotate(B, 30);
			B = A;
			A = temp;
		}
		for (int i = 20; i < 40; i++) {
			uint32_t temp = left_rotate(A, 5) + (B ^ C ^ D) + E + words[i] + K[1];
			E = D;
			D = C;
			C = left_rotate(B, 30);
			B = A;
			A = temp;
		}
		for (int i = 40; i < 60; i++) {
			uint32_t temp = left_rotate(A, 5) + ((B & C) | (B & D) | (C & D)) + E + words[i] + K[2];
			E = D;
			D = C;
			C = left_rotate(B, 30);
			B = A;
			A = temp;
		}
		for (int i = 60; i < 80; i++) {
			uint32_t temp = left_rotate(A, 5) + (B ^ C ^ D) + E + words[i] + K[3];
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
		if (len_bytes < 56) {
			break;
		} else if (len_bytes < 64) {
			mode = 1;
		} else {
			len_bytes -= 64;
			in_byte += 64;
		}
	}

	uint32_t mask = 0xFF;
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < 4; j++) {
			out_byte[4*i + j] = (h[i] >> 8*(4 - j - 1)) & mask;
		}
	}
}
