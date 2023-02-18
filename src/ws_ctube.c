/**
 * Copyright (c) 2023 Bryance Oyang
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file
 * @brief crux
 */

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>

#include "ws_ctube_include.h"
#include "ws_base.h"
#include "ws_ctube_struct.h"
#include "socket.h"

#define WS_CTUBE_DEBUG 0
#define WS_CTUBE_BUFLEN 4096

typedef void (*cleanup_func)(void *);

static void _ws_ctube_cleanup_unlock_mutex(void *mutex)
{
	pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

/** push a work item (start/stop connection) onto the FIFO connq */
static int ws_ctube_connq_push(struct ws_ctube *ctube, struct ws_ctube_conn_struct *conn, enum ws_ctube_qaction act)
{
	int retval = 0;
	struct ws_ctube_conn_qentry *qentry = (typeof(qentry))malloc(sizeof(*qentry));

	if (qentry == NULL) {
		retval = -1;
		goto out_noalloc;
	}
	pthread_cleanup_push(free, qentry);

	if (ws_ctube_conn_qentry_init(qentry, conn, act) != 0) {
		retval = -1;
		goto out_noinit;
	}
	pthread_cleanup_push((cleanup_func)ws_ctube_conn_qentry_destroy, qentry);

	ws_ctube_list_push_back(&ctube->connq, &qentry->lnode);
	pthread_mutex_lock(&ctube->connq_mutex);
	ctube->connq_pred = 1;
	pthread_mutex_unlock(&ctube->connq_mutex);
	pthread_cond_signal(&ctube->connq_cond);

	pthread_cleanup_pop(retval); /* conn_qentry_destory */
out_noinit:
	pthread_cleanup_pop(retval); /* free */
out_noalloc:
	return retval;
}

/** handles incoming data from client */
static void *ws_ctube_reader_main(void *arg)
{
	struct ws_ctube_conn_struct *conn = (struct ws_ctube_conn_struct *)arg;
	struct ws_ctube *ctube = conn->ctube;
	char buf[WS_CTUBE_BUFLEN];

	for (;;) {
		/* TODO: handle ping/pong */
		if (recv(conn->fd, buf, WS_CTUBE_BUFLEN, MSG_NOSIGNAL) < 1) {
			ws_ctube_connq_push(ctube, conn, WS_CTUBE_CONN_STOP);
			if (WS_CTUBE_DEBUG) {
				printf("ws_ctube_reader_main(): disconnected client\n");
				fflush(stdout);
			}
			return NULL;
		}
	}

	return NULL;
}

/** ensures data is released even if writer is cancelled */
static void _ws_ctube_cleanup_release_ws_ctube_data(void *arg)
{
	struct ws_ctube_data *ws_ctube_data = (struct ws_ctube_data *)arg;
	ws_ctube_ref_count_release(ws_ctube_data, refc, ws_ctube_data_free);
}

/** sends broadcast data to client */
static void *ws_ctube_writer_main(void *arg)
{
	struct ws_ctube_conn_struct *conn = (struct ws_ctube_conn_struct *)arg;
	struct ws_ctube *ctube = conn->ctube;
	struct ws_ctube_data *out_data = NULL;
	unsigned long out_data_id = 0;
	int send_retval;

	for (;;) {
		/* wait until new data is needed to be broadcast by checking data id */
		pthread_mutex_lock(&ctube->out_data_mutex);
		pthread_cleanup_push(_ws_ctube_cleanup_unlock_mutex, &ctube->out_data_mutex);
		while (out_data_id == ctube->out_data_id) {
			pthread_cond_wait(&ctube->out_data_cond, &ctube->out_data_mutex);
		}

		ws_ctube_ref_count_acquire(ctube->out_data, refc);
		out_data = ctube->out_data;
		out_data_id = ctube->out_data_id;

		pthread_cleanup_pop(0); /* _ws_ctube_cleanup_unlock_mutex */
		pthread_mutex_unlock(&ctube->out_data_mutex);

		/* broadcast data in a cancellable way */
		pthread_cleanup_push(_ws_ctube_cleanup_release_ws_ctube_data, out_data);
		send_retval = ws_ctube_ws_send(conn->fd, (char *)out_data->data, out_data->data_size);
		pthread_cleanup_pop(0); /* _ws_ctube_cleanup_release_ws_ctube_data */
		ws_ctube_ref_count_release(out_data, refc, ws_ctube_data_free);

		/* TODO: error handling of failed broadcast */
		if (send_retval != 0) {
			continue;
		}
	}

	return NULL;
}

static void _ws_ctube_cancel_reader(void *arg)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube_conn_struct *conn = (struct ws_ctube_conn_struct *)arg;
	pthread_cancel(conn->reader_tid);
	pthread_join(conn->reader_tid, NULL);

	pthread_setcancelstate(oldstate, &statevar);
}

static void _ws_ctube_cancel_writer(void *arg)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube_conn_struct *conn = (struct ws_ctube_conn_struct *)arg;
	pthread_cancel(conn->writer_tid);
	pthread_join(conn->writer_tid, NULL);

	pthread_setcancelstate(oldstate, &statevar);
}

/** start reader/writer threads for a client */
static int ws_ctube_conn_struct_start(struct ws_ctube_conn_struct *conn)
{
	int retval = 0;

	if (pthread_create(&conn->reader_tid, NULL, ws_ctube_reader_main, (void *)conn) != 0) {
		fprintf(stderr, "ws_ctube_conn_struct_start(): create reader failed\n");
		retval = -1;
		goto out_noreader;
	}
	pthread_cleanup_push(_ws_ctube_cancel_reader, conn);

	if (pthread_create(&conn->writer_tid, NULL, ws_ctube_writer_main, (void *)conn) != 0) {
		fprintf(stderr, "ws_ctube_conn_struct_start(): create writer failed\n");
		retval = -1;
		goto out_nowriter;
	}
	pthread_cleanup_push(_ws_ctube_cancel_writer, conn);

	pthread_cleanup_pop(retval); /* _ws_ctube_cancel_writer */
out_nowriter:
	pthread_cleanup_pop(retval); /* _ws_ctube_cancel_reader */
out_noreader:
	return retval;
}

/** cancels reader/writer threads for a client */
static void ws_ctube_conn_struct_stop(struct ws_ctube_conn_struct *conn)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	pthread_cancel(conn->reader_tid);
	pthread_cancel(conn->writer_tid);

	pthread_join(conn->reader_tid, NULL);
	pthread_join(conn->writer_tid, NULL);

	pthread_setcancelstate(oldstate, &statevar);
}

static void _ws_ctube_conn_list_add(struct ws_ctube_list *conn_list, struct ws_ctube_conn_struct *conn)
{
	ws_ctube_ref_count_acquire(conn, refc);
	ws_ctube_list_push_back(conn_list, &conn->lnode);
}

static void _ws_ctube_conn_list_remove(struct ws_ctube_list *conn_list, struct ws_ctube_conn_struct *conn)
{
	ws_ctube_list_unlink(conn_list, &conn->lnode);
	ws_ctube_ref_count_release(conn, refc, ws_ctube_conn_struct_free);
}

/** process work item from FIFO connq (start/stop connection) */
static void ws_ctube_handler_process_queue(struct ws_ctube_list *connq, struct ws_ctube_list *conn_list, int max_nclient)
{
	struct ws_ctube_conn_qentry *qentry;
	struct ws_ctube_list_node *node;
	struct ws_ctube_conn_struct *conn;

	while ((node = ws_ctube_list_pop_front(connq)) != NULL) {
		qentry = ws_ctube_container_of(node, typeof(*qentry), lnode);
		conn = qentry->conn;

		switch (qentry->act) {
		case WS_CTUBE_CONN_START:
			pthread_mutex_lock(&conn_list->mutex);

			/* refuse new connections if limit exceeded*/
			if (conn_list->len >= max_nclient) {
				pthread_mutex_unlock(&conn_list->mutex);
				fprintf(stderr, "ws_ctube_handler_process_queue(): max_nclient reached\n");
				fflush(stderr);
				break;
			} else {
				pthread_mutex_unlock(&conn_list->mutex);
			}

			/* do websocket handshake */
			if (ws_ctube_ws_handshake(conn->fd, &conn->ctube->timeout_val) == 0) {
				ws_ctube_conn_struct_start(conn);
				_ws_ctube_conn_list_add(conn_list, conn);
			}
			break;

		case WS_CTUBE_CONN_STOP:
			pthread_mutex_lock(&conn->stopping_mutex);
			/* prevent double stop */
			if (!conn->stopping) {
				conn->stopping = 1;
				pthread_mutex_unlock(&conn->stopping_mutex);

				_ws_ctube_conn_list_remove(conn_list, conn);
				ws_ctube_conn_struct_stop(conn);
			} else {
				pthread_mutex_unlock(&conn->stopping_mutex);
			}
			break;
		}

		ws_ctube_conn_qentry_free(qentry);
	}
}

/** stop all client connections and cleanup */
static void _ws_ctube_cleanup_conn_list(void *arg)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube_list *conn_list = (struct ws_ctube_list *)arg;
	struct ws_ctube_list_node *node;
	struct ws_ctube_conn_struct *conn;

	while ((node = ws_ctube_list_pop_front(conn_list)) != NULL) {
		conn = ws_ctube_container_of(node, typeof(*conn), lnode);

		pthread_mutex_lock(&conn->stopping_mutex);
		/* prevent double stop */
		if (!conn->stopping) {
			conn->stopping = 1;
			pthread_mutex_unlock(&conn->stopping_mutex);
			ws_ctube_conn_struct_stop(conn);
		} else {
			pthread_mutex_unlock(&conn->stopping_mutex);
		}

		ws_ctube_ref_count_release(conn, refc, ws_ctube_conn_struct_free);
	}

	pthread_setcancelstate(oldstate, &statevar);
}

/** connection handler thread: handles new clients via handshake or cleans up disconnected clients */
static void *ws_ctube_handler_main(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;

	struct ws_ctube_list conn_list;
	ws_ctube_list_init(&conn_list);
	pthread_cleanup_push(_ws_ctube_cleanup_conn_list, &conn_list);

	for (;;) {
		/* wait for work items in FIFO connq */
		pthread_mutex_lock(&ctube->connq_mutex);
		pthread_cleanup_push(_ws_ctube_cleanup_unlock_mutex, &ctube->connq_mutex);
		while (!ctube->connq_pred) {
			pthread_cond_wait(&ctube->connq_cond, &ctube->connq_mutex);
		}
		ctube->connq_pred = 0;
		pthread_mutex_unlock(&ctube->connq_mutex);
		pthread_cleanup_pop(0); /* _ws_ctube_cleanup_unlock_mutex */

		ws_ctube_handler_process_queue(&ctube->connq, &conn_list, ctube->max_nclient);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

/* closes client socket on error */
static void _ws_ctube_cleanup_close_client_conn(void *arg)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	int *fd = (int *)arg;
	if (*fd >= 0) {
		close(*fd);
	}

	pthread_setcancelstate(oldstate, &statevar);
}

static int ws_ctube_server_accept_new_conn(struct ws_ctube *ctube, const int server_sock)
{
	int retval = 0;

	int conn_fd = accept(server_sock, NULL, NULL);
	pthread_cleanup_push(_ws_ctube_cleanup_close_client_conn, &conn_fd);

	/* create new conn_struct for client */
	struct ws_ctube_conn_struct *conn = (typeof(conn))malloc(sizeof(*conn));
	if (conn == NULL) {
		retval = -1;
		goto out_noalloc;
	}
	pthread_cleanup_push(free, conn);

	if (ws_ctube_conn_struct_init(conn, conn_fd, ctube) != 0) {
		retval = -1;
		goto out_noinit;
	}
	/* ws_ctube_conn_struct_destroy() closes conn_fd now; this prevents _ws_ctube_cleanup_close_client_conn() from closing it */
	conn_fd = -1;
	pthread_cleanup_push((cleanup_func)ws_ctube_conn_struct_destroy, conn);

	/* push work item to connection handler to do handshake and start reader/writer */
	if (ws_ctube_connq_push(ctube, conn, WS_CTUBE_CONN_START) != 0) {
		retval = -1;
		goto out_nopush;
	}

out_nopush:
	pthread_cleanup_pop(retval); /* ws_ctube_conn_struct_destroy */
out_noinit:
	pthread_cleanup_pop(retval); /* free */
out_noalloc:
	pthread_cleanup_pop(retval); /* _ws_ctube_cleanup_close_client_conn */
	return retval;
}

static void ws_ctube_serve_forever(struct ws_ctube *ctube)
{
	const int server_sock = ctube->server_sock;

	for (;;) {
		if (ws_ctube_server_accept_new_conn(ctube, server_sock) != 0) {
			fprintf(stderr, "ws_ctube_serve_forever(): error\n");
			fflush(stderr);
		}
	}
}

/** alert main thread if the server fails to init by setting flag */
static void _ws_ctube_server_init_fail(void *arg)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_mutex_lock(&ctube->server_init_mutex);
	ctube->server_inited = -1;
	pthread_mutex_unlock(&ctube->server_init_mutex);
	pthread_cond_signal(&ctube->server_init_cond);

	pthread_setcancelstate(oldstate, &statevar);
}

static void _ws_ctube_close_server_sock(void *arg)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	close(ctube->server_sock);

	pthread_setcancelstate(oldstate, &statevar);
}

static void *ws_ctube_server_main(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cleanup_push(_ws_ctube_server_init_fail, ctube);

	/* create server socket */
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		perror("ws_ctube_server_main()");
		goto out_nosock;
	}
	ctube->server_sock = server_sock;
	pthread_cleanup_push(_ws_ctube_close_server_sock, ctube);

	/* allow reuse */
#ifdef __linux__
	int optname = SO_REUSEADDR | SO_REUSEPORT;
#else
	int optname = SO_REUSEADDR;
#endif
	int yes = 1;
	if (setsockopt(server_sock, SOL_SOCKET, optname, &yes, sizeof(yes)) < 0) {
		perror("ws_ctube_server_main()");
		goto out_err;
	}

	/* set server socket address/port */
	if (ws_ctube_bind_server(server_sock, ctube->port) < 0) {
		perror("ws_ctube_server_main()");
		goto out_err;
	}

	/* set listening */
	if (listen(server_sock, ctube->max_nclient) < 0) {
		perror("ws_ctube_server_main()");
		goto out_err;
	}

	/* success: alert main thread by setting flag */
	pthread_mutex_lock(&ctube->server_init_mutex);
	ctube->server_inited = 1;
	pthread_mutex_unlock(&ctube->server_init_mutex);
	pthread_cond_signal(&ctube->server_init_cond);
	ws_ctube_serve_forever(ctube);

	/* code doesn't get here unless error */
out_err:
	pthread_cleanup_pop(1); /* _ws_ctube_close_server_sock */
out_nosock:
	pthread_cleanup_pop(1); /* _ws_ctube_server_init_fail */
	return NULL;
}

static void _ws_ctube_cancel_handler(void *arg)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cancel(ctube->handler_tid);
	pthread_join(ctube->handler_tid, NULL);

	pthread_setcancelstate(oldstate, &statevar);
}

static void _ws_ctube_cancel_server(void *arg)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cancel(ctube->server_tid);
	pthread_join(ctube->server_tid, NULL);

	pthread_setcancelstate(oldstate, &statevar);
}

/* start connection handler and server threads */
static int ws_ctube_start(struct ws_ctube *ctube)
{
	int retval = 0;

	if (pthread_create(&ctube->handler_tid, NULL, ws_ctube_handler_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_start(): create handler failed\n");
		retval = -1;
		goto out_nohandler;
	}
	pthread_cleanup_push(_ws_ctube_cancel_handler, ctube);

	if (pthread_create(&ctube->server_tid, NULL, ws_ctube_server_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_start(): create server failed\n");
		retval = -1;
		goto out_noserver;
	}
	pthread_cleanup_push(_ws_ctube_cancel_server, ctube);

	/* wait for server thread to report success/failure to start */
	pthread_mutex_lock(&ctube->server_init_mutex);
	pthread_cleanup_push(_ws_ctube_cleanup_unlock_mutex, &ctube->server_init_mutex);
	if (ctube->timeout_spec.tv_nsec > 0 || ctube->timeout_spec.tv_sec > 0) {
		while (!ctube->server_inited) {
			pthread_cond_timedwait(&ctube->server_init_cond, &ctube->server_init_mutex, &ctube->timeout_spec);
		}
	} else {
		while (!ctube->server_inited) {
			pthread_cond_wait(&ctube->server_init_cond, &ctube->server_init_mutex);
		}
	}
	if (ctube->server_inited <= 0) {
		fprintf(stderr, "ws_ctube_start(): server failed to init\n");
		retval = -1;
		goto out_noinit;
	}
	pthread_mutex_unlock(&ctube->server_init_mutex);

out_noinit:
	pthread_cleanup_pop(retval); /* _ws_ctube_cleanup_unlock_mutex */
	pthread_cleanup_pop(retval); /* _ws_ctube_cancel_server */
out_noserver:
	pthread_cleanup_pop(retval); /* _ws_ctube_cancel_handler */
out_nohandler:
	return retval;
}

/** stop connection handler and server threads */
static void ws_ctube_stop(struct ws_ctube *ctube)
{
	int oldstate, statevar;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	pthread_cancel(ctube->handler_tid);
	pthread_cancel(ctube->server_tid);

	pthread_join(ctube->handler_tid, NULL);
	pthread_join(ctube->server_tid, NULL);

	pthread_setcancelstate(oldstate, &statevar);
}

struct ws_ctube* ws_ctube_open(
	int port,
	int max_nclient,
	int timeout_ms,
	double max_broadcast_fps)
{
	int err = 0;
	struct ws_ctube *ctube;

	/* input sanity checks */
	if (port < 1) {
		fprintf(stderr, "ws_ctube_open(): invalid port\n");
		fflush(stderr);
		err = -1;
		goto out_noalloc;
	}
	if (max_nclient < 1) {
		fprintf(stderr, "ws_ctube_open(): invalid max_nclient\n");
		fflush(stderr);
		err = -1;
		goto out_noalloc;
	}
	if (timeout_ms < 0) {
		fprintf(stderr, "ws_ctube_open(): invalid timeout_ms\n");
		fflush(stderr);
		err = -1;
		goto out_noalloc;
	}
	if (max_broadcast_fps < 0) {
		fprintf(stderr, "ws_ctube_open(): invalid max_broadcast_fps\n");
		fflush(stderr);
		err = -1;
		goto out_noalloc;
	}

	ctube = (typeof(ctube))malloc(sizeof(*ctube));
	if (ctube == NULL) {
		err = -1;
		goto out_noalloc;
	}
	pthread_cleanup_push((cleanup_func)free, ctube);

	if (ws_ctube_init(ctube, port, max_nclient, timeout_ms, max_broadcast_fps) != 0) {
		err = -1;
		goto out_noinit;
	}
	pthread_cleanup_push((cleanup_func)ws_ctube_destroy, ctube);

	if (ws_ctube_start(ctube) != 0) {
		err = -1;
		goto out_nostart;
	}

out_nostart:
	pthread_cleanup_pop(err);
out_noinit:
	pthread_cleanup_pop(err);
out_noalloc:
	if (err) {
		return NULL;
	} else {
		return ctube;
	}
}

void ws_ctube_close(struct ws_ctube *ctube)
{
	ws_ctube_stop(ctube);
	ws_ctube_destroy(ctube);
	free(ctube);
}

int ws_ctube_broadcast(struct ws_ctube *ctube, const void *data, size_t data_size)
{
	if (data_size == 0) {
		return 0;
	}

	int retval = 0;
	if (pthread_mutex_trylock(&ctube->out_data_mutex) != 0) {
		retval = -1;
		goto out_nolock;
	}
	pthread_cleanup_push(_ws_ctube_cleanup_unlock_mutex, &ctube->out_data_mutex);

	/* rate limit broadcasting if set */
	struct timespec cur_time;
	const double max_bcast_fps = ctube->max_bcast_fps;
	if (max_bcast_fps > 0) {
		clock_gettime(CLOCK_REALTIME, &cur_time);
		double dt = (cur_time.tv_sec - ctube->prev_bcast_time.tv_sec) +
			1e-9 * (cur_time.tv_nsec - ctube->prev_bcast_time.tv_nsec);

		if (dt < 1.0 / ctube->max_bcast_fps) {
			retval = -1;
			goto out_ratelim;
		}
	}

	/* release old out_data if held */
	if (ctube->out_data != NULL) {
		ws_ctube_ref_count_release(ctube->out_data, refc, ws_ctube_data_free);
	}

	/* alloc new out_data */
	ctube->out_data = (typeof(ctube->out_data))malloc(sizeof(*ctube->out_data));
	if (ctube->out_data == NULL) {
		retval = -1;
		goto out_nodata;
	}
	pthread_cleanup_push(free, ctube->out_data);

	/* init and memcpy into out_data */
	if (ws_ctube_data_init(ctube->out_data, data, data_size) != 0) {
		retval = -1;
		goto out_noinit;
	}
	ws_ctube_ref_count_acquire(ctube->out_data, refc);
	ctube->out_data_id++; /* unique id for out_data */

	/* record broadcast time for rate-limiting next time */
	if (max_bcast_fps > 0) {
		ctube->prev_bcast_time = cur_time;
	}

	pthread_mutex_unlock(&ctube->out_data_mutex);
	pthread_cond_broadcast(&ctube->out_data_cond);

out_noinit:
	pthread_cleanup_pop(retval); /* free */
out_nodata:
out_ratelim:
	pthread_cleanup_pop(retval); /* _ws_ctube_cleanup_unlock_mutex */
out_nolock:
	return retval;
}
