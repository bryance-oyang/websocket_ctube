#ifndef WS_CTUBE_STRUCT_H
#define WS_CTUBE_STRUCT_H

#include <pthread.h>
#include <time.h>
#include "container_of.h"
#include "ref_count.h"
#include "list.h"
#include "ws_ctube.h"

struct ws_data {
	void *data;
	size_t data_size;
	struct ref_count refc;
};

static int ws_data_alloc(struct ws_data *data, void *data, size_t data_size)
{
	data->data = malloc(data_size);
	if (data->data == NULL) {
		goto out_nodata;
	}

	data->data_size = data_size;
	ref_count_init(&data->refc);
}

static void ws_data_free(struct ws_data *data)
{
	if (data->data != NULL) {
		free(data->data);
		data->data = NULL;
	}
	data->data_size = 0;
	ref_count_destroy(&data->refc);
}

static void ws_data_free(struct ws_data *data)
{
	ws_data_destroy(data);
	free(data);
}

struct dframes {
	char *frames;
	size_t frames_size;
	struct ref_count refc;
};

static int dframes_init(struct dframes *df)
{
	df->frames = NULL;
	df->frames_size = 0;
	ref_count_init(&df->refc);
	return 0;
}

static void dframes_destroy(struct dframes *df)
{
	if (df->frames != NULL) {
		free(df->frames);
		df->frames = NULL;
	}
	df->frames_size = 0;
	ref_count_destroy(&df->refc);
}

static void dframes_free(struct dframes *df)
{
	dframes_destroy(df);
	free(df);
}

struct conn_struct {
	int fd;
	struct ws_ctube *ctube;

	int stopping;
	pthread_mutex_t stopping_mutex;

	pthread_t reader_tid;
	pthread_t writer_tid;

	struct ref_count refc;
	struct list_node lnode;
};

static int conn_struct_init(struct conn_struct *conn, int fd, struct ws_ctube *ctube)
{
	conn->fd = fd;
	conn->ctube = ctube;

	conn->stopping = 0;
	pthread_mutex_init(&conn->stopping_mutex, NULL);

	ref_count_init(&conn->refc);
	list_node_init(&conn->lnode);
	return 0;
}

static void conn_struct_destroy(struct conn_struct *conn)
{
	conn->fd = -1;
	conn->ctube = NULL;

	conn->stopping = 0;
	pthread_mutex_destroy(&conn->stopping_mutex);

	ref_count_destroy(&conn->refc);
	list_node_destroy(&conn->lnode);
}

static void conn_struct_free(struct conn_struct *conn)
{
	conn_struct_destroy(conn);
	free(conn);
}

enum qaction {
	WS_CONN_START,
	WS_CONN_STOP
};

struct conn_qentry {
	struct conn_struct *conn;
	enum qaction act;
	struct list_node lnode;
};

static int conn_qentry_init(struct conn_qentry *qentry, struct conn_struct *conn, enum qaction act)
{
	ref_count_acquire(conn, refc);
	qentry->conn = conn;
	qentry->act = act;
	list_node_init(&qentry->lnode);

	return qentry;
}

static void conn_qentry_destroy(struct conn_qentry *qentry)
{
	ref_count_release(qentry->conn, refc, conn_struct_free);
	qentry->conn = NULL;
	list_node_destroy(&qentry->lnode);
}

static void conn_qentry_free(struct conn_qentry *qentry)
{
	conn_qentry_destroy(qentry);
	free(qentry);
}

static void connq_clear(struct list *connq)
{
	struct list_node *node;
	struct conn_qentry *qentry;

	while ((node = list_pop_front(connq)) != NULL) {
		qentry = container_of(node, typeof(*qentry), lnode);
		conn_qentry_free(qentry);
	}
}

struct ws_ctube {
	int server_sock;
	int port;
	int conn_limit;
	struct timespec timeout;

	struct ws_data *data;
	int new_data;
	pthread_mutex_t data_mutex;
	pthread_cond_t data_cond;

	struct dframes *dframes;
	pthread_mutex_t dframes_mutex;
	pthread_cond_t dframes_cond;

	struct list connq;
	int connq_pred;
	pthread_mutex_t connq_mutex;
	pthread_cond_t connq_cond;

	int server_inited;
	pthread_mutex_t server_init_mutex;
	pthread_cond_t server_init_cond;

	pthread_t framer_tid;
	pthread_t handler_tid;
	pthread_t server_tid;
};

static int ws_ctube_init(struct ws_ctube *ctube, int port, int conn_limit, int timeout_ms)
{
	ctube->server_sock = -1;
	ctube->port = port;
	ctube->conn_limit = conn_limit;
	ctube->timeout.tv_sec = 0;
	ctube->timeout.tv_nsec = timeout_ms * 1000000;

	ctube->data = NULL;
	ctube->new_data = 0;
	pthread_mutex_init(&ctube->data_mutex, NULL);
	pthread_cond_init(&ctube->data_cond, NULL);

	ctube->dframes = NULL;
	pthread_mutex_init(&ctube->dframes_mutex, NULL);
	pthread_cond_init(&ctube->dframes_cond, NULL);

	list_init(&ctube->connq);
	ctube->connq_pred = 0;
	pthread_mutex_init(&ctube->connq_mutex, NULL);
	pthread_cond_init(&ctube->connq_cond, NULL);

	ctube->server_inited = 0;
	pthread_mutex_init(&ctube->server_init_mutex, NULL);
	pthread_cond_init(&ctube->server_init_cond, NULL);

	return 0;
}

static void ws_ctube_destroy(struct ws_ctube *ctube)
{
}

#endif /* WS_CTUBE_STRUCT_H */
