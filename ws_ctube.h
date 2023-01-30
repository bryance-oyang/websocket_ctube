#ifndef WS_CTUBE_H
#define WS_CTUBE_H

#include <pthread.h>

struct ws_ctube {
	void *data;
	size_t data_size;

	pthread_mutex_t mutex;
	int data_ready;
	pthread_cond_t data_ready_cond;

	pthread_t serv_tid;
	pthread_barrier_t server_ready;
};

int ws_ctube_init(struct ws_ctube *ctube, int port, int conn_limit);
void ws_ctube_destroy(struct ws_ctube *ctube);
void ws_ctube_broadcast(struct ws_ctube *ctube, void *data, size_t nbytes);


#endif /* WS_CTUBE_H */
