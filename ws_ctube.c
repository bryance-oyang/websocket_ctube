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
	struct conn_struct *conn = (struct conn_struct *)arg;
	pthread_cancel(conn->reader_tid);
	pthread_join(conn->reader_tid, NULL);
}

static int conn_struct_start(struct conn_struct *conn)
{
	if (pthread_create(&conn->reader_tid, NULL, reader_main, (void *)conn) != 0) {
		fprintf(stderr, "conn_struct_start(): create reader failed\n");
		goto err_noreader;
	}
	pthread_cleanup_push(_cancel_reader, conn);
	if (pthread_create(&conn->writer_tid, NULL, writer_main, (void *)conn) != 0) {
		fprintf(stderr, "conn_struct_start(): create writer failed\n");
		goto err_nowriter;
	}

err_nowriter:
	pthread_cleanup_pop(1);
err_noreader:
	return -1;
}

static void conn_struct_stop(struct conn_struct *conn)
{
	pthread_cancel(conn->reader_tid);
	pthread_cancel(conn->writer_tid);

	pthread_join(conn->reader_tid, NULL);
	pthread_join(conn->writer_tid, NULL);
}

static void _conn_list_add(struct list *conn_list, struct conn_struct *conn)
{
	ref_count_acquire(conn, refc);
	list_push_back(conn_list, &conn->lnode);
}

static void _conn_list_remove(struct list *conn_list, struct conn_struct *conn)
{
	(void)conn_list;
	list_node_unlink(&conn->lnode);
	ref_count_release(conn, refc, conn_struct_free);
}

static void handler_process_queue(struct list *connq, struct list *conn_list)
{
	struct list_node *node;
	struct conn_qentry *qentry;
	struct conn_struct *conn;

	while ((node = list_pop_front(connq)) != NULL) {
		qentry = container_of(node, typeof(*qentry), lnode);
		conn = qentry->conn;

		if (qentry->act == WS_CONN_CREATE) {
			conn_struct_start(conn);
			_conn_list_add(conn_list, conn);
		} else if (qentry->act == WS_CONN_DESTROY) {
			_conn_list_remove(conn_list, conn);
			conn_struct_stop(conn);
		}

		conn_qentry_free(qentry);
	}
}

static void _cleanup_conn_list(void *arg)
{
	struct list *conn_list = (struct list *)arg;
	struct list_node *node;
	struct conn_struct *conn;

	list_for_each_entry(conn_list, node, conn, lnode) {
		conn_struct_stop(conn);
		ref_count_release(conn, refc, conn_struct_free);
	}
}

static void *handler_main(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;

	struct list conn_list;
	list_init(&conn_list);
	pthread_cleanup_push(_cleanup_conn_list, &conn_list);

	pthread_mutex_lock(&ctube->connq_mutex);
	pthread_cleanup_push(cleanup_unlock_mutex, &ctube->connq_mutex) {
		for (;;) {
			while (!ctube->connq_pred) {
				pthread_cond_wait(&ctube->connq_cond, &ctube->connq_mutex);
			}
			ctube->connq_pred = 0;
			handler_process_queue(ctube->connq, &conn_list);
		}
	} pthread_cleanup_pop(0);
	pthread_mutex_unlock(&ctube->connq_mutex);

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
	int server_sock = ctube->server_sock;
	for (;;) {
		int conn_fd = accept(server_sock, NULL, NULL);

		struct conn_struct *conn = malloc(sizeof(*conn));
		if (conn == NULL) {
			perror("serve_forever()");
			continue;
		}
		conn_struct_init(conn, conn_fd, ctube);

		/* push new connection onto queue for reader/writer to be created */
		struct conn_qentry *qentry = conn_qentry_alloc(conn, WS_CONN_CREATE);
		if (qentry == NULL) {
			perror("serve_forever()");
			continue;
		}
		list_push_back(ctube->connq, &qentry->lnode);
		pthread_mutex_lock(&ctube->connq_mutex);
		ctube->connq_pred = 1;
		pthread_cond_signal(&ctube->connq_cond);
		pthread_mutex_unlock(&ctube->connq_mutex);
	}
}

static void _server_init_fail(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_mutex_lock(&ctube->server_init_mutex);
	ctube->server_inited = -1;
	pthread_cond_broadcast(&ctube->server_init_cond);
	pthread_mutex_unlock(&ctube->server_init_mutex);
}

static void _close_server_sock(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	close(ctube->server_sock);
}

static void *server_main(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cleanup_push(_server_init_fail, ctube);

	/* create server socket */
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		perror("server_main()");
		goto err_nosock;
	}
	ctube->server_sock = server_sock;
	pthread_cleanup_push(_close_server_sock, ctube)

	int yes = 1;
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &yes,
		   sizeof(yes)) < 0) {
		perror("server_main()");
		goto err;
	}

	/* set server socket address/port */
	if (bind_server(server_sock, ctube->port) < 0) {
		perror("server_main()");
		goto err;
	}

	/* set listening */
	if (listen(server_sock, 1) < 0) {
		perror("server_main()");
		goto err;
	}

	/* success */
	pthread_mutex_lock(&ctube->server_init_mutex);
	ctube->server_inited = 1;
	pthread_cond_broadcast(&ctube->server_init_cond);
	pthread_mutex_unlock(&ctube->server_init_mutex);
	serve_forever(ctube);

	/* code doesn't get here unless error */
err:
	pthread_cleanup_pop(1);
err_nosock:
	pthread_cleanup_pop(1);
	return NULL;
}

static void _cancel_framer(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cancel(ctube->framer_tid);
	pthread_join(ctube->framer_tid, NULL);
}

static void _cancel_handler(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;
	pthread_cancel(ctube->handler_tid);
	pthread_join(ctube->handler_tid, NULL);
}

static int ws_ctube_start(struct ws_ctube *ctube)
{
	if (pthread_create(&ctube->framer_tid, NULL, framer_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_start(): create framer failed\n");
		goto err_noframer;
	}
	pthread_cleanup_push(_cancel_framer, ctube);
	if (pthread_create(&ctube->handler_tid, NULL, handler_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_start(): create handler failed\n");
		goto err_nohandler;
	}
	pthread_cleanup_push(_cancel_handler, ctube);
	if (pthread_create(&ctube->server_tid, NULL, server_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_start(): create server failed\n");
		goto err_noserver;
	}

	pthread_mutex_lock(&ctube->server_init_mutex);
	while (!ctube->server_inited) {
		pthread_cond_timedwait(&ctube->server_init_cond, &ctube->server_init_mutex, &ctube->timeout);
	}
	if (ctube->server_inited < 0) {
		fprintf(stderr, "ws_ctube_start(): server failed to init\n");
		pthread_mutex_unlock(&ctube->server_init_mutex);
		goto err_noserver;
	}
	pthread_mutex_unlock(&ctube->server_init_mutex);

	return 0;

err_noserver:
	pthread_cleanup_pop(1);
err_nohandler:
	pthread_cleanup_pop(1);
err_noframer:
	return -1;
}

static void ws_ctube_stop(struct ws_ctube *ctube)
{
	pthread_cancel(ctube->framer_tid);
	pthread_cancel(ctube->handler_tid);
	pthread_cancel(ctube->server_tid);

	pthread_join(ctube->framer_tid, NULL);
	pthread_join(ctube->handler_tid, NULL);
	pthread_join(ctube->server_tid, NULL);
}

void _ws_ctube_clear_connq(struct list *connq)
{
	struct list_node *node;
	struct conn_qentry *qentry;

	while ((node = list_pop_front(connq)) != NULL) {
		qentry = container_of(node, typeof(*qentry), lnode);
		conn_qentry_free(qentry);
	}
}

void _ws_ctube_destroy_nostop(struct ws_ctube *ctube)
{
	ctube->server_sock = -1;
	ctube->port = -1;
	ctube->conn_limit = -1;
	ctube->timeout.tv_sec = 0;
	ctube->timeout.tv_nsec = 0;

	if (ctube->data != NULL) {
		free(ctube->data);
		ctube->data = NULL;
	}
	ctube->data_size = 0;
	pthread_mutex_destroy(&ctube->data_mutex);
	pthread_cond_destroy(&ctube->data_cond);

	ctube->dframes = NULL;
	pthread_mutex_destroy(&ctube->dframes_mutex);
	pthread_cond_destroy(&ctube->dframes_cond);

	_ws_ctube_clear_connq(ctube->connq);
	list_destroy(ctube->connq);
	free(ctube->connq);
	ctube->connq_pred = 0;
	pthread_mutex_destroy(&ctube->connq_mutex);
	pthread_cond_destroy(&ctube->connq_cond);

	ctube->server_inited = 0;
	pthread_mutex_destroy(&ctube->server_init_mutex);
	pthread_cond_destroy(&ctube->server_init_cond);
}

int ws_ctube_init(struct ws_ctube *ctube, int port, int conn_limit, int timeout_ms)
{
	ctube->connq = malloc(sizeof(*ctube->connq));
	if (ctube->connq == NULL) {
		perror("ws_ctube_init()");
		return -1;
	}
	list_init(ctube->connq);
	ctube->connq_pred = 0;
	pthread_mutex_init(&ctube->connq_mutex, NULL);
	pthread_cond_init(&ctube->connq_cond, NULL);

	ctube->server_sock = -1;
	ctube->port = port;
	ctube->conn_limit = conn_limit;
	ctube->timeout.tv_sec = 0;
	ctube->timeout.tv_nsec = timeout_ms * 1000000;

	ctube->data = NULL;
	ctube->data_size = 0;
	pthread_mutex_init(&ctube->data_mutex, NULL);
	pthread_cond_init(&ctube->data_cond, NULL);

	ctube->dframes = NULL;
	pthread_mutex_init(&ctube->dframes_mutex, NULL);
	pthread_cond_init(&ctube->dframes_cond, NULL);

	ctube->server_inited = 0;
	pthread_mutex_init(&ctube->server_init_mutex, NULL);
	pthread_cond_init(&ctube->server_init_cond, NULL);

	if (ws_ctube_start(ctube) == 0) {
		return 0;
	} else {
		_ws_ctube_destroy_nostop(ctube);
		return -1;
	}
}

void ws_ctube_destroy(struct ws_ctube *ctube)
{
	ws_ctube_stop(ctube);
	_ws_ctube_destroy_nostop(ctube);
}

int ws_ctube_broadcast(struct ws_ctube *ctube, void *data, size_t data_size)
{
	if (pthread_mutex_trylock(&ctube->data_mutex) == 0) {
		if (ctube->data != NULL) {
			free(ctube->data);
		}
		ctube->data = malloc(data_size);
		if (ctube->data == NULL) {
			perror("ws_ctube_broadcast()");
			pthread_mutex_unlock(&ctube->data_mutex);
			return -1;
		}

		memcpy(ctube->data, data, data_size);
		ctube->data_size = data_size;
		pthread_cond_signal(&ctube->data_cond);
		pthread_mutex_unlock(&ctube->data_mutex);
		return 0;
	} else {
		return -1;
	}
}
