#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include "ws_ctube.h"

struct serv_td {
	int port;
	struct ws_ctube *ctube;

	int thread_status;
	pthread_barrier_t thread_ready;
};

struct conn_td {
	int conn;
	struct ws_ctube *ctube;
	pthread_barrier_t thread_ready;
};

static void *ws_ctube_conn(void *arg)
{

}

static void ws_ctube_conn_init(struct ws_ctube *ctube, int conn)
{
	struct conn_td conn_td;

	conn_td.conn = conn;
	conn_td.ctube = ctube;
	pthread_barrier_init(&conn_td.thread_ready, NULL, 2);

	pthread_create(&ctube->_conn_tid, NULL,

	pthread_barrier_wait(&conn_td.thread_ready);
	pthread_barrier_destroy(&conn_td.thread_ready);
}

static void *ws_ctube_serv_init(void *arg)
{
	int port = ((struct serv_td *)arg)->port;
	struct ws_ctube *ctube = ((struct serv_td *)arg)->ctube;
	int *thread_status = &((struct serv_td *)arg)->thread_status;
	pthread_barrier_t *thread_ready = &((struct serv_td *)arg)->thread_ready;

	int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sock == -1) {
		perror(NULL);
		goto out_nosock;
	}
	int true = 1;
	setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &true,
		   sizeof(int));

	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	if (bind(serv_sock, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		perror(NULL);
		goto out_err;
	}

	if (listen(serv_sock, 1) == -1) {
		perror(NULL);
		goto out_err;
	}

success:
	*thread_status = 0;
	pthread_barrier_wait(thread_ready);
	for (;;) {
		int conn = accept(serv_sock, NULL, NULL);
		ws_ctube_conn_init(ws_ctube, conn);
	}
	close(serv_sock);
	return NULL;

out_err:
	close(serv_sock);
out_nosock:
	*thread_status = -1;
	pthread_barrier_wait(thread_ready);
	return NULL;
}

static void ws_ctube_syncprim_init(struct ws_ctube *ctube)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);

	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&ctube->_mutex, &attr);
	pthread_cond_init(&ctube->_data_ready_cond, NULL);

	pthread_mutexattr_destroy(&attr);
}

int ws_ctube_init(struct ws_ctube *ctube, int port)
{
	ws_ctube_syncprim_init(ctube);

	struct serv_td serv_td;
	serv_td.port = port;
	serv_td.ctube = ctube;
	serv_td.thread_status = 0;
	pthread_barrier_init(&serv_td.thread_ready, NULL, 2);

	pthread_create(&ctube->_serv_tid, NULL, ws_ctube_serv_init, (void *)&serv_td);

	pthread_barrier_wait(&serv_td.thread_ready);
	pthread_barrier_destroy(&serv_td.thread_ready);
	return serv_td.thread_status;
}

void ws_ctube_lock(struct ws_ctube *ctube)
{
	pthread_mutex_lock(&ctube->_mutex);
}

void ws_ctube_unlock(struct ws_ctube *ctube)
{
	pthread_mutex_unlock(&ctube->_mutex);
}

void ws_ctube_signal(struct ws_ctube *ctube)
{
	ws_ctube_lock(ctube);
	ctube->_data_ready = 1;
	pthread_cond_broadcast(&ctube->_data_ready_cond);
	ws_ctube_unlock(ctube);
}

void ws_ctube_destroy(struct ws_ctube *ctube)
{
	pthread_cancel(ctube->_serv_tid);
	pthread_cancel(ctube->_conn_tid);
	pthread_join(ctube->_serv_tid, NULL);
	pthread_join(ctube->_conn_tid, NULL);

	pthread_mutex_destroy(&ctube->_mutex);
	pthread_cond_destroy(&ctube->_conn_tid);
}
