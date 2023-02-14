#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stddef.h>
#include <time.h>

#pragma GCC visibility push(hidden)

#define WS_FRAME_HDR_SIZE 2
#define WS_MAX_PAYLD_SIZE 125

/**
 * make a websocket frame
 *
 * @param frame pointer to buffer where frame shall be written; needs to have
 * size of at least WS_FRAME_HDR_SIZE + WS_MAX_PAYLD_SIZE bytes
 * @param msg pointer to data
 * @param msg_size bytes of message
 * @param first whether this is the first frame in a sequence
 *
 * @return number of bytes of msg contained in frame
 */
int ws_mkframe(char *frame, const char *msg, size_t msg_size, int first);

int ws_send(int conn, const char *msg, size_t msg_size);
int ws_recv(int conn, char *msg, int *msg_size, size_t max_msg_size);
int ws_is_ping(const char *msg, int msg_size);
int ws_pong(int conn, const char *msg, int msg_size);
int ws_handshake(int conn, const struct timeval *timeout);

#pragma GCC visibility pop

#endif /* WEBSOCKET_H */
