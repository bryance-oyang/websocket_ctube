#ifndef WS_CTUBE_H
#define WS_CTUBE_H

#include <pthread.h>

struct ws_ctube {
	void *data;
	size_t data_size;

	pthread_mutex_t _mutex;
	int _data_ready_pipefd[2];

	pthread_t _serv_tid;
	pthread_t _conn_tid;
};

int ws_ctube_init(struct ws_ctube *ctube, int port);
void ws_ctube_lock(struct ws_ctube *ctube);
void ws_ctube_unlock(struct ws_ctube *ctube);
void ws_ctube_broadcast(struct ws_ctube *ctube);
void ws_ctube_destroy(struct ws_ctube *ctube);


#endif /* WS_CTUBE_H */
