#ifndef WS_CTUBE_H
#define WS_CTUBE_H

#include <pthread.h>

struct ws_ctube;

struct ws_ctube *ws_ctube_open(int port, int conn_limit, int timeout_ms);
void ws_ctube_close(struct ws_ctube *ctube);
int ws_ctube_broadcast(struct ws_ctube *ctube, void *data, size_t data_size);

#endif /* WS_CTUBE_H */
