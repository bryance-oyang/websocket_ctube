#ifndef WS_CTUBE_CRYPT_H
#define WS_CTUBE_CRYPT_H

#include <stddef.h>

void ws_ctube_b64_encode(unsigned char *out, const unsigned char *in, size_t in_bytes);
void ws_ctube_sha1sum(unsigned char *out, const unsigned char *in, size_t len);

#endif /* WS_CTUBE_CRYPT_H */
