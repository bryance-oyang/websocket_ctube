#ifndef CRYPT_H
#define CRYPT_H

#include <stddef.h>

#pragma GCC visibility push(hidden)

void b64_encode(unsigned char *out, const unsigned char *in, size_t in_bytes);
void sha1sum(unsigned char *out, const unsigned char *in, size_t len);

#pragma GCC visibility pop

#endif /* CRYPT_H */
