/**
 * Copyright (c) 2023 Bryance Oyang
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef WS_CTUBE_CRYPT_H
#define WS_CTUBE_CRYPT_H

#include <stddef.h>

void ws_ctube_b64_encode(unsigned char *out, const unsigned char *in, size_t in_bytes);
void ws_ctube_sha1sum(unsigned char *out, const unsigned char *in, size_t len);

#endif /* WS_CTUBE_CRYPT_H */
