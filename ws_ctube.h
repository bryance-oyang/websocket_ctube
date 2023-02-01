#ifndef WS_CTUBE_H
#define WS_CTUBE_H

#include <pthread.h>
#include <time.h>

struct ws_dframes {
	char *frames;
	size_t frames_size;

	int refs;
	pthread_mutex_t refs_mutex;
};

struct ws_ctube {
	int server_sock;
	int port;
	int conn_limit;
	struct timespec timeout;

	void *data;
	size_t data_size;
	pthread_mutex_t data_mutex;
	pthread_cond_t data_cond;

	struct ws_dframes *dframes;
	pthread_mutex_t dframes_mutex;
	pthread_cond_t dframes_cond;

	pthread_t framer_tid;
	pthread_t handler_tid;
	pthread_t server_tid;

	int server_inited;
	pthread_mutex_t server_init_mutex;
	pthread_cond_t server_init_cond;
};

int ws_ctube_init(struct ws_ctube *ctube, int port, int conn_limit, int timeout_ms);
void ws_ctube_destroy(struct ws_ctube *ctube);
int ws_ctube_broadcast(struct ws_ctube *ctube, void *data, size_t data_size);

#endif /* WS_CTUBE_H */
