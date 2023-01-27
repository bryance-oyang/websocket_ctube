#ifndef WEBSOCKET_H
#define WEBSOCKET_H

int ws_send(int conn, char *msg, int msg_size);
int ws_recv(int conn, char *msg, int msg_size);
int ws_handshake(int conn);

#endif /* WEBSOCKET_H */
