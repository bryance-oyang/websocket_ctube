#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "ws_base.h"
#include "ws_ctube.h"
#include "ws_ctube_struct.h"

#define WS_CTUBE_DEBUG 1
#define WS_CTUBE_BUFLEN 4096

typedef void (*cleanup_f)(void *);

static void cleanup_unlock_mutex(void *mutex)
{
	pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

static void *reader_main(void *arg)
{
	return NULL;
}

static void *writer_main(void *arg)
{
	return NULL;
}

static void *framer_main(void *arg)
{
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

out_nowriter:
	pthread_cleanup_pop(retval);
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

static void handler_process_queue(struct list *connq, struct list *conn_list)
{
	struct conn_qentry *qentry;
	struct list_node *node;
	struct conn_struct *conn;

	while ((node = list_lockpop_front(connq)) != NULL) {
		qentry = container_of(node, typeof(*qentry), lnode);
		conn = qentry->conn;

		if (qentry->act == WS_CONN_START) {
			conn_struct_start(conn);
			_conn_list_add(conn_list, conn);
		} else if (qentry->act == WS_CONN_STOP) {
			pthread_mutex_lock(&conn->stopping_mutex);
			if (!conn->stopping) {
				conn->stopping = 1;
				pthread_mutex_unlock(&conn->stopping_mutex);

				_conn_list_remove(conn_list, conn);
				conn_struct_stop(conn);
			} else {
				pthread_mutex_unlock(&conn->stopping_mutex);
			}
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
			conn_struct_stop(conn);
		}
		pthread_mutex_unlock(&conn->stopping_mutex);

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
		pthread_cleanup_push(cleanup_unlock_mutex, &ctube->connq_mutex);
		while (!ctube->connq_pred) {
			pthread_cond_wait(&ctube->connq_cond, &ctube->connq_mutex);
		}
		ctube->connq_pred = 0;
		pthread_mutex_unlock(&ctube->connq_mutex);
		pthread_cleanup_pop(0); /* cleanup_unlock_mutex */

		handler_process_queue(&ctube->connq, &conn_list);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

static int bind_server(int server_sock, int port) {
	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	return bind(server_sock, (struct sockaddr *)&sa, sizeof(sa));
}

static void serve_forever(struct ws_ctube *ctube)
{
	int oldstate;
	int server_sock = ctube->server_sock;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
		int conn_fd = accept(server_sock, NULL, NULL);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

		struct conn_struct *conn = malloc(sizeof(*conn));
		if (conn == NULL) {
			perror("serve_forever()");
			continue;
		}
		conn_struct_init(conn, conn_fd, ctube);

		/* push new connection onto queue for reader/writer to be created */
		struct conn_qentry *qentry = malloc(sizeof(*qentry));
		if (qentry == NULL) {
			perror("serve_forever()");
			continue;
		}
		conn_qentry_init(qentry, conn, WS_CONN_START);

		list_push_back(&ctube->connq, &qentry->lnode);
		pthread_mutex_lock(&ctube->connq_mutex);
		ctube->connq_pred = 1;
		pthread_cond_signal(&ctube->connq_cond);
		pthread_mutex_unlock(&ctube->connq_mutex);
	}
}

static void _server_init_fail(void *arg)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_mutex_lock(&ctube->server_init_mutex);
	ctube->server_inited = -1;
	pthread_cond_broadcast(&ctube->server_init_cond);
	pthread_mutex_unlock(&ctube->server_init_mutex);

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

	int yes = 1;
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &yes,
		   sizeof(yes)) < 0) {
		perror("server_main()");
		goto out_err;
	}

	/* set server socket address/port */
	if (bind_server(server_sock, ctube->port) < 0) {
		perror("server_main()");
		goto out_err;
	}

	/* set listening */
	if (listen(server_sock, 1) < 0) {
		perror("server_main()");
		goto out_err;
	}

	/* success */
	pthread_mutex_lock(&ctube->server_init_mutex);
	ctube->server_inited = 1;
	pthread_cond_broadcast(&ctube->server_init_cond);
	pthread_mutex_unlock(&ctube->server_init_mutex);
	serve_forever(ctube);

	/* code doesn't get here unless error */
out_err:
	pthread_cleanup_pop(1); /* _close_server_sock */
out_nosock:
	pthread_cleanup_pop(1); /* _server_init_fail */
	return NULL;
}

static void _cancel_framer(void *arg)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cancel(ctube->framer_tid);
	pthread_join(ctube->framer_tid, NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
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

static int ws_ctube_start(struct ws_ctube *ctube)
{
	int retval = 0;

	if (pthread_create(&ctube->framer_tid, NULL, framer_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_start(): create framer failed\n");
		retval = -1;
		goto out_noframer;
	}
	pthread_cleanup_push(_cancel_framer, ctube);

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

	pthread_mutex_lock(&ctube->server_init_mutex);
	pthread_cleanup_push(cleanup_unlock_mutex, &ctube->server_init_mutex);
	while (!ctube->server_inited) {
		pthread_cond_timedwait(&ctube->server_init_cond, &ctube->server_init_mutex, &ctube->timeout);
	}
	if (ctube->server_inited < 0) {
		fprintf(stderr, "ws_ctube_start(): server failed to init\n");
		retval = -1;
		goto out_noinit;
	}
	pthread_mutex_unlock(&ctube->server_init_mutex);

out_noinit:
	pthread_cleanup_pop(retval);
out_noserver:
	pthread_cleanup_pop(retval);
out_nohandler:
	pthread_cleanup_pop(retval);
out_noframer:
	return retval;
}

static void ws_ctube_stop(struct ws_ctube *ctube)
{
	int oldstate;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	pthread_cancel(ctube->framer_tid);
	pthread_cancel(ctube->handler_tid);
	pthread_cancel(ctube->server_tid);

	pthread_join(ctube->framer_tid, NULL);
	pthread_join(ctube->handler_tid, NULL);
	pthread_join(ctube->server_tid, NULL);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
}

struct ws_ctube *ws_ctube_open(int port, int conn_limit, int timeout_ms)
{
	int err = 0;
	struct ws_ctube *ctube;

	ctube = malloc(sizeof(*ctube));
	if (ctube == NULL) {
		err = -1;
		goto out_noalloc;
	}
	pthread_cleanup_push((cleanup_f)free, ctube);

	if (ws_ctube_init(ctube, port, conn_limit, timeout_ms) != 0) {
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

int ws_ctube_try_bcast(struct ws_ctube *ctube, void *data, size_t data_size)
{
	int retval = 0;

	if (pthread_mutex_trylock(&ctube->out_data_mutex) != 0) {
		retval = -1;
		goto out_nolock;
	}
	pthread_cleanup_push(cleanup_unlock_mutex, &ctube->out_data_mutex);

	if (ctube->out_data_list.len > 0) {
		retval = -1;
		goto out_listfull;
	}

	struct ws_data *out_data = malloc(sizeof(*out_data));
	if (out_data == NULL) {
		retval = -1;
		goto out_nodata;
	}
	pthread_cleanup_push(free, out_data);

	if (ws_data_init(out_data, data_size) != 0) {
		retval = -1;
		goto out_nodatainit;
	}
	if (ws_data_cp(out_data, data, data_size) != 0) {
		retval = -1;
		goto out_nodatacp;
	}

	list_push_back(&ctube->out_data_list, &out_data->lnode);

	pthread_mutex_unlock(&ctube->out_data_mutex);

out_nodatacp:
out_nodatainit:
	pthread_cleanup_pop(retval); /* free */
out_nodata:
out_listfull:
	pthread_cleanup_pop(retval); /* cleanup_unlock_mutex */
out_nolock:
	return retval;
}
