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
	return (w << amount) | (w >> (32 - amount));
}

static void sha1_wordgen(uint32_t *const word)
{
	for (int i = 16; i < 80; i++) {
		word[i] = left_rotate(word[i-3] ^ word[i-8] ^ word[i-14] ^ word[i-16], 1);
	}
}

static void sha1_total_len_cpy(uint8_t *const word, uint64_t total_len)
{
	uint64_t mask = 0xFF;
	for (int i = 0; i < 8; i++) {
		word[i] = (total_len >> (56 - 8*i)) & mask;
	}
}

static void sha1_mkwords(uint8_t *const word, const uint8_t *const in, size_t len, const int mode, const uint64_t total_len)
{
	memset(word, 0, 64);

	switch (mode) {
	case 0:
		if (len < 56) {
			memcpy(word, in, len);
			word[len] = 0x80;
			sha1_total_len_cpy(&word[56], total_len);
		} else if (len < 64) {
			memcpy(word, in, len);
			word[len] = 0x80;
		} else {
			memcpy(word, in, 64);
		}
		break;

	case 1:
		/* needs total_len appended only */
		sha1_total_len_cpy(&word[56], total_len);
		break;

	case 2:
		/* needs 1 and total_len appended */
		word[0] = 0x80;
		sha1_total_len_cpy(&word[56], total_len);
		break;
	}

	sha1_wordgen((uint32_t *)word);
}

void sha1sum(unsigned char *out, const unsigned char *in, size_t len)
{
	//SHA1(in, len, out);
	//return;

	const uint8_t *in_byte = (uint8_t *)in;
	uint8_t *const out_byte = (uint8_t *)out;
	const uint64_t total_len = len;

	uint32_t h[5];
	h[0] = 0x67452301;
	h[1] = 0xEFCDAB89;
	h[2] = 0x98BADCFE;
	h[3] = 0x10325476;
	h[4] = 0xC3D2E1F0;
	uint32_t word[80];

	int mode = 0;
	while(1) {
		sha1_mkwords((uint8_t *)word, in_byte, len, mode, total_len);

		uint32_t a = h[0];
		uint32_t b = h[1];
		uint32_t c = h[2];
		uint32_t d = h[3];
		uint32_t e = h[4];

		uint32_t f, k;
		for (int i = 0; i < 80; i++) {
			if (i < 20) {
				f = (b & c) | ((~b) & d);
				k = 0x5A827999;
			} else if (20 <= i && i < 40) {
				f = b ^ c ^ d;
				k = 0x6ED9EBA1;
			} else if (40 <= i && i < 60) {
				f = (b & c) | (b & d) | (c & d);
				k = 0x8F1BBCDC;
			} else if (60 <= i && i < 80) {
				f = b ^ c ^ d;
				k = 0xCA62C1D6;
			}

			uint32_t tmp = left_rotate(a, 5) + f + e + k + word[i];
			e = d;
			d = c;
			c = left_rotate(b, 30);
			b = a;
			a = tmp;
		}

		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;

		if (mode != 0) {
			break;
		}

		if (len < 56) {
			break;
		} else if (len < 64) {
			mode = 1;
		} else if (len == 64) {
			mode = 2;
		} else {
			len -= 64;
			in_byte += 64;
		}
	}

	uint8_t mask = 0xFF;
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < 4; j++) {
			out_byte[4*i + j] = (h[i] >> 8*(3 - j)) & mask;
		}
	}
}
