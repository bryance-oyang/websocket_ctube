/**
 * Copyright (c) 2023 Bryance Oyang
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef WS_CTUBE_STRUCT_H
#define WS_CTUBE_STRUCT_H

#include <pthread.h>
#include <time.h>
#include "container_of.h"
#include "ref_count.h"
#include "list.h"

/** holds data to be sent/received over the network */
struct ws_ctube_data {
	void *data;
	size_t data_size;

	pthread_mutex_t mutex;
	struct ws_ctube_list_node lnode;
	struct ws_ctube_ref_count refc;
};

static int ws_ctube_data_init(struct ws_ctube_data *ws_ctube_data, const void *data, size_t data_size)
{
	if (data_size > 0) {
		ws_ctube_data->data = (typeof(ws_ctube_data->data))malloc(data_size);
		if (ws_ctube_data->data == NULL) {
			goto out_nodata;
		}

		if (data != NULL) {
			memcpy(ws_ctube_data->data, data, data_size);
		}
	}

	ws_ctube_data->data_size = data_size;

	pthread_mutex_init(&ws_ctube_data->mutex, NULL);
	ws_ctube_list_node_init(&ws_ctube_data->lnode);
	ws_ctube_ref_count_init(&ws_ctube_data->refc);
	return 0;

out_nodata:
	return -1;
}

static void ws_ctube_data_destroy(struct ws_ctube_data *ws_ctube_data)
{
	if (ws_ctube_data->data != NULL) {
		free(ws_ctube_data->data);
		ws_ctube_data->data = NULL;
	}

	ws_ctube_data->data_size = 0;

	pthread_mutex_destroy(&ws_ctube_data->mutex);
	ws_ctube_list_node_destroy(&ws_ctube_data->lnode);
	ws_ctube_ref_count_destroy(&ws_ctube_data->refc);
}

/** copy data into ws_ctube_data */
static inline int ws_ctube_data_cp(struct ws_ctube_data *ws_ctube_data, const void *data, size_t data_size)
{
	int retval = 0;

	pthread_mutex_lock(&ws_ctube_data->mutex);
	if (ws_ctube_data->data_size < data_size) {
		if (ws_ctube_data->data != NULL) {
			free(ws_ctube_data->data);
		}

		ws_ctube_data->data = (typeof(ws_ctube_data->data))malloc(data_size);
		if (ws_ctube_data->data == NULL) {
			retval = -1;
			goto out;
		}

		ws_ctube_data->data_size = data_size;
	}

	memcpy(ws_ctube_data->data, data, data_size);

out:
	pthread_mutex_unlock(&ws_ctube_data->mutex);
	return retval;
}

static void ws_ctube_data_free(struct ws_ctube_data *ws_ctube_data)
{
	ws_ctube_data_destroy(ws_ctube_data);
	free(ws_ctube_data);
}

/** represents a client connection and owns their associated reader/writer threads */
struct ws_ctube_conn_struct {
	int fd;
	struct ws_ctube *ctube;

	/* to prevent double shutdown */
	int stopping;
	pthread_mutex_t stopping_mutex;

	/** reader thread */
	pthread_t reader_tid;
	/** writer thread */
	pthread_t writer_tid;

	struct ws_ctube_ref_count refc;
	struct ws_ctube_list_node lnode;
};

static int ws_ctube_conn_struct_init(struct ws_ctube_conn_struct *conn, int fd, struct ws_ctube *ctube)
{
	conn->fd = fd;
	conn->ctube = ctube;

	conn->stopping = 0;
	pthread_mutex_init(&conn->stopping_mutex, NULL);

	ws_ctube_ref_count_init(&conn->refc);
	ws_ctube_list_node_init(&conn->lnode);
	return 0;
}

static void ws_ctube_conn_struct_destroy(struct ws_ctube_conn_struct *conn)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	close(conn->fd);
	pthread_setcancelstate(oldstate, &statevar);

	conn->fd = -1;
	conn->ctube = NULL;

	conn->stopping = 0;
	pthread_mutex_destroy(&conn->stopping_mutex);

	ws_ctube_ref_count_destroy(&conn->refc);
	ws_ctube_list_node_destroy(&conn->lnode);
}

static void ws_ctube_conn_struct_free(struct ws_ctube_conn_struct *conn)
{
	ws_ctube_conn_struct_destroy(conn);
	free(conn);
}

enum ws_ctube_qaction {
	WS_CTUBE_CONN_START,
	WS_CTUBE_CONN_STOP
};

/** a work item in FIFO queue: start or stop a connection to client */
struct ws_ctube_conn_qentry {
	struct ws_ctube_conn_struct *conn;
	enum ws_ctube_qaction act;
	struct ws_ctube_list_node lnode;
};

static int ws_ctube_conn_qentry_init(struct ws_ctube_conn_qentry *qentry, struct ws_ctube_conn_struct *conn, enum ws_ctube_qaction act)
{
	ws_ctube_ref_count_acquire(conn, refc);
	qentry->conn = conn;
	qentry->act = act;
	ws_ctube_list_node_init(&qentry->lnode);
	return 0;
}

static void ws_ctube_conn_qentry_destroy(struct ws_ctube_conn_qentry *qentry)
{
	ws_ctube_ref_count_release(qentry->conn, refc, ws_ctube_conn_struct_free);
	qentry->conn = NULL;
	ws_ctube_list_node_destroy(&qentry->lnode);
}

static void ws_ctube_conn_qentry_free(struct ws_ctube_conn_qentry *qentry)
{
	ws_ctube_conn_qentry_destroy(qentry);
	free(qentry);
}

/** main struct for ws_ctube */
struct ws_ctube {
	int server_sock;
	int port;
	int max_nclient;

	/* to have timeout on operations */
	struct timespec timeout_spec;
	struct timeval timeout_val;

	/* currently unused (for incoming data) */
	struct ws_ctube_list in_data_list;
	int in_data_pred;
	pthread_mutex_t in_data_mutex;
	pthread_cond_t in_data_cond;

	/* current ws_ctube_data representing data to be sent */
	struct ws_ctube_data *out_data;
	unsigned long out_data_id;
	pthread_mutex_t out_data_mutex;
	pthread_cond_t out_data_cond;

	/* rate-limit broadcasting */
	double max_bcast_fps;
	struct timespec prev_bcast_time;

	/* the FIFO work queue: connection handler starts/stops client
	 * connections based on queued actions */
	struct ws_ctube_list connq;
	int connq_pred;
	pthread_mutex_t connq_mutex;
	pthread_cond_t connq_cond;

	/* allows ws_ctube_open() to know if server successfully started or not */
	int server_inited;
	pthread_mutex_t server_init_mutex;
	pthread_cond_t server_init_cond;

	/** connection handler thread */
	pthread_t handler_tid;
	/** server thread */
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

	ws_ctube_list_init(&ctube->in_data_list);
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

	ws_ctube_list_init(&ctube->connq);
	ctube->connq_pred = 0;
	pthread_mutex_init(&ctube->connq_mutex, NULL);
	pthread_cond_init(&ctube->connq_cond, NULL);

	ctube->server_inited = 0;
	pthread_mutex_init(&ctube->server_init_mutex, NULL);
	pthread_cond_init(&ctube->server_init_cond, NULL);

	return 0;
}

static void _ws_ctube_data_list_clear(struct ws_ctube_list *dlist)
{
	struct ws_ctube_list_node *node;
	struct ws_ctube_data *data;

	while ((node = ws_ctube_list_pop_front(dlist)) != NULL) {
		data = ws_ctube_container_of(node, typeof(*data), lnode);
		ws_ctube_data_free(data);
	}
}

static void _ws_ctube_connq_clear(struct ws_ctube_list *connq)
{
	struct ws_ctube_list_node *node;
	struct ws_ctube_conn_qentry *qentry;

	while ((node = ws_ctube_list_pop_front(connq)) != NULL) {
		qentry = ws_ctube_container_of(node, typeof(*qentry), lnode);
		ws_ctube_conn_qentry_free(qentry);
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

	_ws_ctube_data_list_clear(&ctube->in_data_list);
	ws_ctube_list_destroy(&ctube->in_data_list);
	ctube->in_data_pred = 0;
	pthread_mutex_destroy(&ctube->in_data_mutex);
	pthread_cond_destroy(&ctube->in_data_cond);

	if (ctube->out_data != NULL) {
		ws_ctube_ref_count_release(ctube->out_data, refc, ws_ctube_data_free);
		ctube->out_data = NULL;
	}
	ctube->out_data_id = 0;
	pthread_mutex_destroy(&ctube->out_data_mutex);
	pthread_cond_destroy(&ctube->out_data_cond);

	ctube->max_bcast_fps = 0;
	ctube->prev_bcast_time.tv_sec = 0;
	ctube->prev_bcast_time.tv_nsec = 0;

	_ws_ctube_connq_clear(&ctube->connq);
	ws_ctube_list_destroy(&ctube->connq);
	ctube->connq_pred = 0;
	pthread_mutex_destroy(&ctube->connq_mutex);
	pthread_cond_destroy(&ctube->connq_cond);

	ctube->server_inited = 0;
	pthread_mutex_destroy(&ctube->server_init_mutex);
	pthread_cond_destroy(&ctube->server_init_cond);
}

#endif /* WS_CTUBE_STRUCT_H */
