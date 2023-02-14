#ifndef WS_CTUBE_STRUCT_H
#define WS_CTUBE_STRUCT_H

#include <pthread.h>
#include <time.h>
#include "container_of.h"
#include "ref_count.h"
#include "list.h"

#pragma GCC visibility push(hidden)

struct ws_data {
	void *data;
	size_t data_size;

	pthread_mutex_t mutex;
	struct list_node lnode;
	struct ref_count refc;
};

static int ws_data_init(struct ws_data *ws_data, const void *data, size_t data_size)
{
	if (data_size > 0) {
		ws_data->data = malloc(data_size);
		if (ws_data->data == NULL) {
			goto out_nodata;
		}

		if (data != NULL) {
			memcpy(ws_data->data, data, data_size);
		}
	}

	ws_data->data_size = data_size;

	pthread_mutex_init(&ws_data->mutex, NULL);
	list_node_init(&ws_data->lnode);
	ref_count_init(&ws_data->refc);
	return 0;

out_nodata:
	return -1;
}

static void ws_data_destroy(struct ws_data *ws_data)
{
	if (ws_data->data != NULL) {
		free(ws_data->data);
		ws_data->data = NULL;
	}

	ws_data->data_size = 0;

	pthread_mutex_destroy(&ws_data->mutex);
	list_node_destroy(&ws_data->lnode);
	ref_count_destroy(&ws_data->refc);
}

static inline int ws_data_cp(struct ws_data *ws_data, const void *data, size_t data_size)
{
	int retval = 0;

	pthread_mutex_lock(&ws_data->mutex);
	if (ws_data->data_size < data_size) {
		if (ws_data->data != NULL) {
			free(ws_data->data);
		}

		ws_data->data = malloc(data_size);
		if (ws_data->data == NULL) {
			retval = -1;
			goto out;
		}

		ws_data->data_size = data_size;
	}

	memcpy(ws_data->data, data, data_size);

out:
	pthread_mutex_unlock(&ws_data->mutex);
	return retval;
}

static void ws_data_free(struct ws_data *ws_data)
{
	ws_data_destroy(ws_data);
	free(ws_data);
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
	close(conn->fd);
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
	return 0;
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

struct ws_ctube {
	int server_sock;
	int port;
	int max_nclient;

	struct timespec timeout_spec;
	struct timeval timeout_val;

	struct list in_data_list;
	int in_data_pred;
	pthread_mutex_t in_data_mutex;
	pthread_cond_t in_data_cond;

	struct ws_data *out_data;
	unsigned long out_data_id;
	pthread_mutex_t out_data_mutex;
	pthread_cond_t out_data_cond;

	double max_bcast_fps;
	struct timespec prev_bcast_time;

	struct list connq;
	int connq_pred;
	pthread_mutex_t connq_mutex;
	pthread_cond_t connq_cond;

	int server_inited;
	pthread_mutex_t server_init_mutex;
	pthread_cond_t server_init_cond;

	pthread_t handler_tid;
	pthread_t server_tid;
};

static int ws_ctube_init(
	struct ws_ctube *ctube,
	int port,
	int max_nclient,
	unsigned int timeout_ms,
	double max_broadcast_fps)
{
	ctube->server_sock = -1;
	ctube->port = port;
	ctube->max_nclient = max_nclient;

	ctube->timeout_spec.tv_sec = timeout_ms / 1000;
	ctube->timeout_spec.tv_nsec = (timeout_ms % 1000) * 1000000;
	ctube->timeout_val.tv_sec = timeout_ms / 1000;
	ctube->timeout_val.tv_usec = (timeout_ms % 1000) * 1000;

	list_init(&ctube->in_data_list);
	ctube->in_data_pred = 0;
	pthread_mutex_init(&ctube->in_data_mutex, NULL);
	pthread_cond_init(&ctube->in_data_cond, NULL);

	ctube->out_data = NULL;
	ctube->out_data_id = 0;
	pthread_mutex_init(&ctube->out_data_mutex, NULL);
	pthread_cond_init(&ctube->out_data_cond, NULL);

	ctube->max_bcast_fps = max_broadcast_fps;
	ctube->prev_bcast_time.tv_sec = 0;
	ctube->prev_bcast_time.tv_nsec = 0;

	list_init(&ctube->connq);
	ctube->connq_pred = 0;
	pthread_mutex_init(&ctube->connq_mutex, NULL);
	pthread_cond_init(&ctube->connq_cond, NULL);

	ctube->server_inited = 0;
	pthread_mutex_init(&ctube->server_init_mutex, NULL);
	pthread_cond_init(&ctube->server_init_cond, NULL);

	return 0;
}

static void _ws_data_list_clear(struct list *dlist)
{
	struct list_node *node;
	struct ws_data *data;

	while ((node = list_lockpop_front(dlist)) != NULL) {
		data = container_of(node, typeof(*data), lnode);
		pthread_mutex_unlock(&node->mutex);
		ws_data_free(data);
	}
}

static void _connq_clear(struct list *connq)
{
	struct list_node *node;
	struct conn_qentry *qentry;

	while ((node = list_lockpop_front(connq)) != NULL) {
		qentry = container_of(node, typeof(*qentry), lnode);
		pthread_mutex_unlock(&node->mutex);
		conn_qentry_free(qentry);
	}
}

static void ws_ctube_destroy(struct ws_ctube *ctube)
{
	ctube->server_sock = -1;
	ctube->port = -1;
	ctube->max_nclient = -1;

	ctube->timeout_spec.tv_sec = 0;
	ctube->timeout_spec.tv_nsec = 0;
	ctube->timeout_val.tv_sec = 0;
	ctube->timeout_val.tv_usec = 0;

	_ws_data_list_clear(&ctube->in_data_list);
	list_destroy(&ctube->in_data_list);
	ctube->in_data_pred = 0;
	pthread_mutex_destroy(&ctube->in_data_mutex);
	pthread_cond_destroy(&ctube->in_data_cond);

	if (ctube->out_data != NULL) {
		ref_count_release(ctube->out_data, refc, ws_data_free);
		ctube->out_data = NULL;
	}
	ctube->out_data_id = 0;
	pthread_mutex_destroy(&ctube->out_data_mutex);
	pthread_cond_destroy(&ctube->out_data_cond);

	ctube->max_bcast_fps = 0;
	ctube->prev_bcast_time.tv_sec = 0;
	ctube->prev_bcast_time.tv_nsec = 0;

	_connq_clear(&ctube->connq);
	list_destroy(&ctube->connq);
	ctube->connq_pred = 0;
	pthread_mutex_destroy(&ctube->connq_mutex);
	pthread_cond_destroy(&ctube->connq_cond);

	ctube->server_inited = 0;
	pthread_mutex_destroy(&ctube->server_init_mutex);
	pthread_cond_destroy(&ctube->server_init_cond);
}

#pragma GCC visibility pop

#endif /* WS_CTUBE_STRUCT_H */
