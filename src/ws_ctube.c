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

#include "ws_ctube.h"
#include "ws_base.h"
#include "ws_ctube_struct.h"
#include "socket.h"

#define WS_CTUBE_DEBUG 0
#define WS_CTUBE_BUFLEN 4096

typedef void (*cleanup_f)(void *);

static void _cleanup_unlock_mutex(void *mutex)
{
	pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

static int _connq_push(struct ws_ctube *ctube, struct conn_struct *conn, enum qaction act)
{
	int retval = 0;
	struct conn_qentry *qentry = malloc(sizeof(*qentry));

	if (qentry == NULL) {
		retval = -1;
		goto out_noalloc;
	}
	pthread_cleanup_push(free, qentry);

	if (conn_qentry_init(qentry, conn, act) != 0) {
		retval = -1;
		goto out_noinit;
	}
	pthread_cleanup_push((cleanup_f)conn_qentry_destroy, qentry);

	list_push_back(&ctube->connq, &qentry->lnode);
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

static void *reader_main(void *arg)
{
	struct conn_struct *conn = (struct conn_struct *)arg;
	struct ws_ctube *ctube = conn->ctube;
	char buf[WS_CTUBE_BUFLEN];

	for (;;) {
		if (recv(conn->fd, buf, WS_CTUBE_BUFLEN, MSG_NOSIGNAL) < 1) {
			_connq_push(ctube, conn, WS_CONN_STOP);
			if (WS_CTUBE_DEBUG) {
				printf("reader_main(): disconnected client\n");
				fflush(stdout);
			}
			return NULL;
		}
	}

	return NULL;
}

static void _cleanup_release_ws_data(void *arg)
{
	struct ws_data *ws_data = (struct ws_data *)arg;
	ref_count_release(ws_data, refc, ws_data_free);
}

static void *writer_main(void *arg)
{
	struct conn_struct *conn = (struct conn_struct *)arg;
	struct ws_ctube *ctube = conn->ctube;
	struct ws_data *out_data = NULL;
	unsigned long out_data_id = 0;
	int send_retval;

	for (;;) {
		pthread_mutex_lock(&ctube->out_data_mutex);
		pthread_cleanup_push(_cleanup_unlock_mutex, &ctube->out_data_mutex);
		while (out_data_id == ctube->out_data_id) {
			pthread_cond_wait(&ctube->out_data_cond, &ctube->out_data_mutex);
		}

		ref_count_acquire(ctube->out_data, refc);
		out_data = ctube->out_data;
		out_data_id = ctube->out_data_id;

		pthread_cleanup_pop(0); /* _cleanup_unlock_mutex */
		pthread_mutex_unlock(&ctube->out_data_mutex);

		pthread_cleanup_push(_cleanup_release_ws_data, out_data);
		send_retval = ws_send(conn->fd, out_data->data, out_data->data_size);
		pthread_cleanup_pop(0); /* _cleanup_release_ws_data */
		ref_count_release(out_data, refc, ws_data_free);

		if (send_retval != 0) {
			continue;
		}
	}

	return NULL;
}

static void _cancel_reader(void *arg)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct conn_struct *conn = (struct conn_struct *)arg;
	pthread_cancel(conn->reader_tid);
	pthread_join(conn->reader_tid, NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
}

static void _cancel_writer(void *arg)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct conn_struct *conn = (struct conn_struct *)arg;
	pthread_cancel(conn->writer_tid);
	pthread_join(conn->writer_tid, NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
}

static int conn_struct_start(struct conn_struct *conn)
{
	int retval = 0;

	if (pthread_create(&conn->reader_tid, NULL, reader_main, (void *)conn) != 0) {
		fprintf(stderr, "conn_struct_start(): create reader failed\n");
		retval = -1;
		goto out_noreader;
	}
	pthread_cleanup_push(_cancel_reader, conn);

	if (pthread_create(&conn->writer_tid, NULL, writer_main, (void *)conn) != 0) {
		fprintf(stderr, "conn_struct_start(): create writer failed\n");
		retval = -1;
		goto out_nowriter;
	}
	pthread_cleanup_push(_cancel_writer, conn);

	pthread_cleanup_pop(retval); /* _cancel_writer */
out_nowriter:
	pthread_cleanup_pop(retval); /* _cancel_reader */
out_noreader:
	return retval;
}

static void conn_struct_stop(struct conn_struct *conn)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	pthread_cancel(conn->reader_tid);
	pthread_cancel(conn->writer_tid);

	pthread_join(conn->reader_tid, NULL);
	pthread_join(conn->writer_tid, NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
}

static void _conn_list_add(struct list *conn_list, struct conn_struct *conn)
{
	ref_count_acquire(conn, refc);
	list_push_back(conn_list, &conn->lnode);
}

static void _conn_list_remove(struct list *conn_list, struct conn_struct *conn)
{
	list_unlink(conn_list, &conn->lnode);
	ref_count_release(conn, refc, conn_struct_free);
}

static void handler_process_queue(struct list *connq, struct list *conn_list, int max_nclient)
{
	struct conn_qentry *qentry;
	struct list_node *node;
	struct conn_struct *conn;

	while ((node = list_lockpop_front(connq)) != NULL) {
		qentry = container_of(node, typeof(*qentry), lnode);
		conn = qentry->conn;

		switch (qentry->act) {
		case WS_CONN_START:
			pthread_mutex_lock(&conn_list->mutex);
			if (conn_list->len >= max_nclient) {
				pthread_mutex_unlock(&conn_list->mutex);
				fprintf(stderr, "handler_process_queue(): max_nclient reached\n");
				fflush(stderr);
				break;
			} else {
				pthread_mutex_unlock(&conn_list->mutex);
			}

			if (ws_handshake(conn->fd, &conn->ctube->timeout_val) == 0) {
				conn_struct_start(conn);
				_conn_list_add(conn_list, conn);
			}
			break;

		case WS_CONN_STOP:
			pthread_mutex_lock(&conn->stopping_mutex);
			if (!conn->stopping) {
				conn->stopping = 1;
				pthread_mutex_unlock(&conn->stopping_mutex);

				_conn_list_remove(conn_list, conn);
				conn_struct_stop(conn);
			} else {
				pthread_mutex_unlock(&conn->stopping_mutex);
			}
			break;
		}

		pthread_mutex_unlock(&node->mutex);
		conn_qentry_free(qentry);
	}
}

static void _cleanup_conn_list(void *arg)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct list *conn_list = (struct list *)arg;
	struct list_node *node;
	struct conn_struct *conn;

	while ((node = list_lockpop_front(conn_list)) != NULL) {
		conn = container_of(node, typeof(*conn), lnode);

		pthread_mutex_lock(&conn->stopping_mutex);
		if (!conn->stopping) {
			conn->stopping = 1;
			pthread_mutex_unlock(&conn->stopping_mutex);
			conn_struct_stop(conn);
		} else {
			pthread_mutex_unlock(&conn->stopping_mutex);
		}

		pthread_mutex_unlock(&node->mutex);
		ref_count_release(conn, refc, conn_struct_free);
	}

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
}

static void *handler_main(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;

	struct list conn_list;
	list_init(&conn_list);
	pthread_cleanup_push(_cleanup_conn_list, &conn_list);

	for (;;) {
		pthread_mutex_lock(&ctube->connq_mutex);
		pthread_cleanup_push(_cleanup_unlock_mutex, &ctube->connq_mutex);
		while (!ctube->connq_pred) {
			pthread_cond_wait(&ctube->connq_cond, &ctube->connq_mutex);
		}
		ctube->connq_pred = 0;
		pthread_mutex_unlock(&ctube->connq_mutex);
		pthread_cleanup_pop(0); /* _cleanup_unlock_mutex */

		handler_process_queue(&ctube->connq, &conn_list, ctube->max_nclient);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

static void _cleanup_close_client_conn(void *arg)
{
	int *fd = (int *)arg;
	if (*fd >= 0) {
		close(*fd);
	}
}

static int _serve_accept_new_conn(struct ws_ctube *ctube, const int server_sock)
{
	int retval = 0;

	int conn_fd = accept(server_sock, NULL, NULL);
	pthread_cleanup_push(_cleanup_close_client_conn, &conn_fd);

	struct conn_struct *conn = malloc(sizeof(*conn));
	if (conn == NULL) {
		retval = -1;
		goto out_noalloc;
	}
	pthread_cleanup_push(free, conn);

	if (conn_struct_init(conn, conn_fd, ctube) != 0) {
		retval = -1;
		goto out_noinit;
	}
	/* conn_struct_destroy closes conn_fd now; this prevents _cleanup_close_client_conn() from closing it */
	conn_fd = -1;
	pthread_cleanup_push((cleanup_f)conn_struct_destroy, conn);

	if (_connq_push(ctube, conn, WS_CONN_START) != 0) {
		retval = -1;
		goto out_nopush;
	}

out_nopush:
	pthread_cleanup_pop(retval); /* conn_struct_destroy */
out_noinit:
	pthread_cleanup_pop(retval); /* free */
out_noalloc:
	pthread_cleanup_pop(retval); /* _cleanup_close_client_conn */
	return retval;
}

static void serve_forever(struct ws_ctube *ctube)
{
	const int server_sock = ctube->server_sock;

	for (;;) {
		if (_serve_accept_new_conn(ctube, server_sock) != 0) {
			fprintf(stderr, "serve_forever(): error\n");
			fflush(stderr);
		}
	}
}

static void _server_init_fail(void *arg)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_mutex_lock(&ctube->server_init_mutex);
	ctube->server_inited = -1;
	pthread_mutex_unlock(&ctube->server_init_mutex);
	pthread_cond_signal(&ctube->server_init_cond);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
}

static void _close_server_sock(void *arg)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	close(ctube->server_sock);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
}

static void *server_main(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cleanup_push(_server_init_fail, ctube);

	/* create server socket */
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		perror("server_main()");
		goto out_nosock;
	}
	ctube->server_sock = server_sock;
	pthread_cleanup_push(_close_server_sock, ctube)

	/* allow reuse */
#ifdef __linux__
	int optname = SO_REUSEADDR | SO_REUSEPORT;
#else
	int optname = SO_REUSEADDR;
#endif
	int yes = 1;
	if (setsockopt(server_sock, SOL_SOCKET, optname, &yes, sizeof(yes)) < 0) {
		perror("server_main()");
		goto out_err;
	}

	/* set server socket address/port */
	if (bind_server(server_sock, ctube->port) < 0) {
		perror("server_main()");
		goto out_err;
	}

	/* set listening */
	if (listen(server_sock, ctube->max_nclient) < 0) {
		perror("server_main()");
		goto out_err;
	}

	/* success */
	pthread_mutex_lock(&ctube->server_init_mutex);
	ctube->server_inited = 1;
	pthread_mutex_unlock(&ctube->server_init_mutex);
	pthread_cond_signal(&ctube->server_init_cond);
	serve_forever(ctube);

	/* code doesn't get here unless error */
out_err:
	pthread_cleanup_pop(1); /* _close_server_sock */
out_nosock:
	pthread_cleanup_pop(1); /* _server_init_fail */
	return NULL;
}

static void _cancel_handler(void *arg)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cancel(ctube->handler_tid);
	pthread_join(ctube->handler_tid, NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
}

static void _cancel_server(void *arg)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cancel(ctube->server_tid);
	pthread_join(ctube->server_tid, NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
}

static int ws_ctube_start(struct ws_ctube *ctube)
{
	int retval = 0;

	if (pthread_create(&ctube->handler_tid, NULL, handler_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_start(): create handler failed\n");
		retval = -1;
		goto out_nohandler;
	}
	pthread_cleanup_push(_cancel_handler, ctube);

	if (pthread_create(&ctube->server_tid, NULL, server_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_start(): create server failed\n");
		retval = -1;
		goto out_noserver;
	}
	pthread_cleanup_push(_cancel_server, ctube);

	pthread_mutex_lock(&ctube->server_init_mutex);
	pthread_cleanup_push(_cleanup_unlock_mutex, &ctube->server_init_mutex);
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
	pthread_cleanup_pop(retval); /* _cleanup_unlock_mutex */
	pthread_cleanup_pop(retval); /* _cancel_server */
out_noserver:
	pthread_cleanup_pop(retval); /* _cancel_handler */
out_nohandler:
	return retval;
}

static void ws_ctube_stop(struct ws_ctube *ctube)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	pthread_cancel(ctube->handler_tid);
	pthread_cancel(ctube->server_tid);

	pthread_join(ctube->handler_tid, NULL);
	pthread_join(ctube->server_tid, NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
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

	ctube = malloc(sizeof(*ctube));
	if (ctube == NULL) {
		err = -1;
		goto out_noalloc;
	}
	pthread_cleanup_push((cleanup_f)free, ctube);

	if (ws_ctube_init(ctube, port, max_nclient, timeout_ms, max_broadcast_fps) != 0) {
		err = -1;
		goto out_noinit;
	}
	pthread_cleanup_push((cleanup_f)ws_ctube_destroy, ctube);

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
	pthread_cleanup_push(_cleanup_unlock_mutex, &ctube->out_data_mutex);

	/* rate limit broadcasting if set */
	struct timespec cur_time;
	const double max_bcast_fps = ctube->max_bcast_fps;
	if (max_bcast_fps > 0) {
		clock_gettime(CLOCK_MONOTONIC, &cur_time);
		double dt = (cur_time.tv_sec - ctube->prev_bcast_time.tv_sec) +
			1e-9 * (cur_time.tv_nsec - ctube->prev_bcast_time.tv_nsec);

		if (dt < 1.0 / ctube->max_bcast_fps) {
			retval = -1;
			goto out_ratelim;
		}
	}

	/* alloc new out_data */
	if (ctube->out_data != NULL) {
		ref_count_release(ctube->out_data, refc, ws_data_free);
	}
	ctube->out_data = malloc(sizeof(*ctube->out_data));
	if (ctube->out_data == NULL) {
		retval = -1;
		goto out_nodata;
	}
	pthread_cleanup_push(free, ctube->out_data);

	/* init and memcpy into out_data */
	if (ws_data_init(ctube->out_data, data, data_size) != 0) {
		retval = -1;
		goto out_noinit;
	}
	ref_count_acquire(ctube->out_data, refc);
	ctube->out_data_id++;

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
	pthread_cleanup_pop(retval); /* _cleanup_unlock_mutex */
out_nolock:
	return retval;
}