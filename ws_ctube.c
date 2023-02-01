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

void *reader_main(void *arg)
{
}

void *writer_main(void *arg)
{
}

static void *framer_main(void *arg)
{
}

static void *handler_main(void *arg)
{
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
	}
}

static void *server_main(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;

	/* create server socket */
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		perror("server_main()");
		goto err_nosock;
	}
	ctube->server_sock = server_sock;

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
	close(server_sock);
err_nosock:
	pthread_mutex_lock(&ctube->server_init_mutex);
	ctube->server_inited = -1;
	pthread_cond_broadcast(&ctube->server_init_cond);
	pthread_mutex_unlock(&ctube->server_init_mutex);
	return NULL;
}

static int ws_ctube_start(struct ws_ctube *ctube)
{
	if (pthread_create(&ctube->framer_tid, NULL, framer_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_init(): create framer failed\n");
		goto err_noframer;
	}
	if (pthread_create(&ctube->handler_tid, NULL, handler_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_init(): create handler failed\n");
		goto err_nohandler;
	}
	if (pthread_create(&ctube->server_tid, NULL, server_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_init(): create server failed\n");
		goto err_noserver;
	}

	pthread_mutex_lock(&ctube->server_init_mutex);
	while (!ctube->server_inited) {
		pthread_cond_timedwait(&ctube->server_init_cond, &ctube->server_init_mutex, &ctube->timeout);
	}
	if (ctube->server_inited < 0) {
		fprintf(stderr, "ws_ctube_init(): server failed to init\n");
		pthread_mutex_unlock(&ctube->server_init_mutex);
		goto err_noserver;
	}
	pthread_mutex_unlock(&ctube->server_init_mutex);

	return 0;

err_noserver:
	pthread_cancel(ctube->handler_tid);
	pthread_join(ctube->handler_tid, NULL);
err_nohandler:
	pthread_cancel(ctube->framer_tid);
	pthread_join(ctube->framer_tid, NULL);
err_noframer:
	return -1;
}

static void ws_ctube_stop(struct ws_ctube *ctube)
{
	pthread_cancel(ctube->framer_tid);
	pthread_cancel(ctube->handler_tid);
	pthread_cancel(ctube->server_tid);

	pthread_join(ctube->framer_tid);
	pthread_join(ctube->handler_tid);
	pthread_join(ctube->server_tid);
}

int ws_ctube_init(struct ws_ctube *ctube, int port, int conn_limit, int timeout_ms)
{
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

	return ws_ctube_start(ctube);
}

void ws_ctube_destroy(struct ws_ctube *ctube)
{
	ws_ctube_stop(ctube);

	ctube->server_sock = -1;
	ctube->port = -1;
	ctube->conn_limit = -1;
	ctube->timeout.tv_sec = 0;
	ctube->timeout.tv_nsec = 0;

	ctube->data = NULL;
	ctube->data_size = 0;
	pthread_mutex_destroy(&ctube->data_mutex);
	pthread_cond_destroy(&ctube->data_cond);

	ctube->dframes = NULL;
	pthread_mutex_destroy(&ctube->dframes_mutex);
	pthread_cond_destroy(&ctube->dframes_cond);

	ctube->server_inited = 0;
	pthread_mutex_destroy(&ctube->server_init_mutex);
	pthread_cond_destroy(&ctube->server_init_cond);
}

int ws_ctube_broadcast(struct ws_ctube *ctube, void *data, size_t data_size)
{
	if (pthread_mutex_trylock(&ctube->data_mutex) == 0) {
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
