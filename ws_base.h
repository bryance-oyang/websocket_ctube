#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#define WS_FRAME_HDR_SIZE 2
#define WS_MAX_PAYLD_SIZE 125

int ws_send(int conn, const char *msg, size_t msg_size);
int ws_recv(int conn, char *msg, int *msg_size, size_t max_msg_size);
int ws_is_ping(const char *msg, int msg_size);
int ws_pong(int conn, const char *msg, int msg_size);
int ws_handshake(int conn);
int ws_mkframe(char *frame, const char *msg, size_t msg_size, int first);

#endif /* WEBSOCKET_H */
