#ifndef WS_CTUBE_STRUCT_H
#define WS_CTUBE_STRUCT_H

#include <pthread.h>
#include <time.h>
#include "container_of.h"
#include "ref_count.h"
#include "list.h"

struct ws_data {
	void *data;
	size_t data_size;

	struct list_node lnode;
};

static int ws_data_init(struct ws_data *ws_data, size_t data_size)
{
	ws_data->data = malloc(data_size);
	if (ws_data->data == NULL) {
		goto out_nodata;
	}

	ws_data->data_size = data_size;

	list_node_init(&ws_data->lnode);
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

	list_node_destroy(&ws_data->lnode);
}

static int ws_data_cp(struct ws_data *ws_data, void *data, size_t data_size)
{
	int retval = 0;

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
	return retval;
}

static void ws_data_free(struct ws_data *ws_data)
{
	ws_data_destroy(ws_data);
	free(ws_data);
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

static int dframes_resize(struct dframes *df, size_t frames_size)
{
	if (frames_size == 0) {
		if (df->frames != NULL) {
			free(df->frames);
			df->frames = NULL;
		}
		df->frames_size = 0;
		return 0;
	} else {
		df->frames = realloc(df->frames, frames_size);
		if (df->frames == NULL) {
			return -1;
		}
		df->frames_size = frames_size;
		return 0;
	}
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
	int conn_limit;

	struct timespec timeout_spec;
	struct timeval timeout_val;

	struct list in_data_list;
	int in_data_pred;
	pthread_mutex_t in_data_mutex;
	pthread_cond_t in_data_cond;

	struct ws_data out_data;
	int out_data_pred;
	pthread_mutex_t out_data_mutex;
	pthread_cond_t out_data_cond;

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

	struct timespec prev_bcast_time;
	double max_bcast_fps;
};

static int ws_ctube_init(
	struct ws_ctube *ctube,
	int port,
	int conn_limit,
	unsigned int timeout_ms,
	double max_broadcast_fps)
{
	ctube->server_sock = -1;
	ctube->port = port;
	ctube->conn_limit = conn_limit;

	ctube->timeout_spec.tv_sec = timeout_ms / 1000;
	ctube->timeout_spec.tv_nsec = (timeout_ms % 1000) * 1000000;
	ctube->timeout_val.tv_sec = timeout_ms / 1000;
	ctube->timeout_val.tv_usec = (timeout_ms % 1000) * 1000;

	list_init(&ctube->in_data_list);
	ctube->in_data_pred = 0;
	pthread_mutex_init(&ctube->in_data_mutex, NULL);
	pthread_cond_init(&ctube->in_data_cond, NULL);

	ws_data_init(&ctube->out_data, 0);
	ctube->out_data_pred = 0;
	pthread_mutex_init(&ctube->out_data_mutex, NULL);
	pthread_cond_init(&ctube->out_data_cond, NULL);

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

	ctube->prev_bcast_time.tv_sec = 0;
	ctube->prev_bcast_time.tv_nsec = 0;
	ctube->max_bcast_fps = max_broadcast_fps;

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
	ctube->conn_limit = -1;

	ctube->timeout_spec.tv_sec = 0;
	ctube->timeout_spec.tv_nsec = 0;
	ctube->timeout_val.tv_sec = 0;
	ctube->timeout_val.tv_usec = 0;

	_ws_data_list_clear(&ctube->in_data_list);
	list_destroy(&ctube->in_data_list);
	ctube->in_data_pred = 0;
	pthread_mutex_destroy(&ctube->in_data_mutex);
	pthread_cond_destroy(&ctube->in_data_cond);

	ws_data_destroy(&ctube->out_data);
	ctube->out_data_pred = 0;
	pthread_mutex_destroy(&ctube->out_data_mutex);
	pthread_cond_destroy(&ctube->out_data_cond);

	if (ctube->dframes != NULL) {
		ref_count_release(ctube->dframes, refc, dframes_free);
		ctube->dframes = NULL;
	}
	pthread_mutex_destroy(&ctube->dframes_mutex);
	pthread_cond_destroy(&ctube->dframes_cond);

	_connq_clear(&ctube->connq);
	list_destroy(&ctube->connq);
	ctube->connq_pred = 0;
	pthread_mutex_destroy(&ctube->connq_mutex);
	pthread_cond_destroy(&ctube->connq_cond);

	ctube->server_inited = 0;
	pthread_mutex_destroy(&ctube->server_init_mutex);
	pthread_cond_destroy(&ctube->server_init_cond);

	ctube->prev_bcast_time.tv_sec = 0;
	ctube->prev_bcast_time.tv_nsec = 0;
	ctube->max_bcast_fps = 0;
}

#endif /* WS_CTUBE_STRUCT_H */
