/*
 * Copyright (c) 2023 Bryance Oyang
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef WS_CTUBE_WS_BASE_H
#define WS_CTUBE_WS_BASE_H

#include <stddef.h>
#include <time.h>

#define WS_CTUBE_FRAME_HDR_SIZE 2
#define WS_CTUBE_MAX_PAYLD_SIZE 125

/**
 * make a websocket frame
 *
 * @param frame pointer to buffer where frame shall be written; needs to have
 * size of at least WS_CTUBE_FRAME_HDR_SIZE + WS_CTUBE_MAX_PAYLD_SIZE bytes
 * @param msg pointer to data
 * @param msg_size bytes of message
 * @param first whether this is the first frame in a sequence
 *
 * @return number of bytes of msg contained in frame
 */
int ws_ctube_ws_mkframe(char *frame, const char *msg, size_t msg_size, int first);

int ws_ctube_ws_send(int conn, const char *msg, size_t msg_size);
int ws_ctube_ws_recv(int conn, char *msg, int *msg_size, size_t max_msg_size);
int ws_ctube_ws_is_ping(const char *msg, int msg_size);
int ws_ctube_ws_pong(int conn, const char *msg, int msg_size);
int ws_ctube_ws_handshake(int conn, const struct timeval *timeout);

#endif /* WS_CTUBE_WS_BASE_H */
