#ifndef WS_CTUBE_H
#define WS_CTUBE_H

#include <pthread.h>

struct ws_ctube {
	void *data;
	size_t data_size;

	pthread_mutex_t _mutex;
	int _data_ready;
	pthread_cond_t _data_ready_cond;

	pthread_t _serv_tid;
	pthread_t _conn_tid;
};


#endif /* WS_CTUBE_H */
