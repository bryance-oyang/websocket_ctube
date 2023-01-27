#ifndef WS_CTUBE_H
#define WS_CTUBE_H

#include <pthread.h>

struct ws_ctube {
	void *data;
	size_t data_size;

	pthread_mutex_t _mutex;
	char _data_ready;
	int _data_ready_pipefd[2];

	pthread_t _serv_tid;
	pthread_t _conn_tid;
};


#endif /* WS_CTUBE_H */
