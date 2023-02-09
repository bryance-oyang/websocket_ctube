/** @file
 * @brief crypto functions
 */

#include <stdint.h>
#include <openssl/sha.h>

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

void sha1sum(unsigned char *out, const unsigned char *in, size_t len)
{
	SHA1(in, len, out);
}
