#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "ws_ctube.h"
#include "ws_base.h"

#define WS_CTUBE_BUFLEN 4096

struct poll_list {
	int *fds;
	nfds_t nfds;
	nfds_t cap;
	pthread_mutex_t mutex;
};

static void poll_list_init(struct poll_list *pl)
{
	pl->fds = NULL;
	pl->nfds = 0;
	pl->cap = 0;
	pthread_mutex_init(&pl->mutex, NULL);
}

static void poll_list_destroy(struct poll_list *pl)
{
	free(pl->fds);
	pthread_mutex_destroy(&pl->mutex);
}

static void poll_list_add(struct poll_list *restrict pl, int fd)
{
	pthread_mutex_lock(&pl->mutex);
	if (pl->nfds == pl->cap) {
		pl->cap = pl->cap * 2 + 1;
		pl->fds = realloc(pl->fds, pl->cap * sizeof(*pl->fds));
	}

	pl->fds[pl->nfds] = fd;
	pl->nfds++;
	pthread_mutex_unlock(&pl->mutex);
}

static void poll_list_remove(struct poll_list *restrict pl, int fd)
{
	pthread_mutex_lock(&pl->mutex);
	nfds_t i;
	for (i = 0; i < pl->nfds; i++) {
		if (fd == pl->fds[i]) {
			break;
		}
	}
	if (i == pl->nfds) {
		return;
	}

	if (i < pl->nfds - 1) {
		memmove(&pl->fds[i], &pl->fds[i + 1], (pl->nfds - i - 1) * sizeof(*pl->fds));
	}
	pl->nfds--;

	nfds_t half_cap = pl->cap / 2;
	if (pl->nfds < half_cap) {
		pl->cap = half_cap;
		pl->fds = realloc(pl->fds, pl->cap * sizeof(*pl->fds));
	}
	pthread_mutex_unlock(&pl->mutex);
}

static struct pollfd *poll_list_alloc_cpy(const struct poll_list *restrict pl)
{
	pthread_mutex_lock(&pl->mutex);
	struct pollfd *fds = malloc(pl->nfds * sizeof(*fds));
	if (fds == NULL) {
		fprintf(stderr, "poll_list_cpy(): out of memory\n");
		fflush(stderr);
		return NULL;
	}
	for (nfds_t i = 0; i < pl->nfds; i++) {
		fds[i].fd = pl->fds[i];
	}
	pthread_mutex_unlock(&pl->mutex);
	return fds;
}

struct conn_handler_td {
	struct ws_ctube *ctube;
	int serv_pipefd;
};

static void handle_data_ready(struct ws_ctube *restrict ctube, struct pollfd *restrict fds, nfds_t nfds)
{
}

static void handle_income(int conn)
{
	/* TODO: ping response */
	return;
}

static void handle_new_conn(struct pollfd *restrict *restrict const fds, nfds_t *restrict const nfds, char *const buf)
{
}

static void handle_bad_conn(struct pollfd *restrict const fds, nfds_t *restrict const nfds, nfds_t i)
{
}

/** main for connection handler */
static void *conn_handler_main(void *arg)
{
	struct ws_ctube *ctube = ((struct conn_handler_td *)arg)->ctube;
	int serv_pipefd = ((struct conn_handler_td *)arg)->serv_pipefd;

	return NULL;
}

static int conn_handler_init(struct ws_ctube *ctube, int serv_pipefd)
{
	struct conn_handler_td td = {
		.ctube = ctube,
		.serv_pipefd = serv_pipefd
	};
	return pthread_create(&ctube->_conn_tid, NULL, conn_handler_main, (void *)&td);
}

static int bind_serv(int serv_sock, int port) {
	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	return bind(serv_sock, (struct sockaddr *)&sa, sizeof(sa));
}

static void serve_forever(int serv_sock)
{
	for (;;) {
		int conn = accept(serv_sock, NULL, NULL);
	}
}

struct serv_main_arg {
	int port;
	struct ws_ctube *ctube;

	int serv_stat;
	pthread_barrier_t server_ready;
};

struct serv_cleanup_arg {
	int serv_sock;
};

static void *serv_cleanup(void *arg)
{
	int serv_sock = ((struct serv_cleanup_arg *)arg)->serv_sock;
	close(serv_sock);
	return NULL;
}

/** main for server */
static void *serv_main(void *arg)
{
	int port = ((struct serv_main_arg *)arg)->port;
	struct ws_ctube *ctube = ((struct serv_main_arg *)arg)->ctube;
	int *serv_stat = &((struct serv_main_arg *)arg)->serv_stat;
	pthread_barrier_t *server_ready = &((struct serv_main_arg *)arg)->server_ready;

	/* create server socket */
	int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sock == -1) {
		perror(NULL);
		goto out_nosock;
	}
	int true = 1;
	if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &true,
		   sizeof(int)) == -1) {
		perror(NULL);
		goto out_err;
	}

	/* set server socket address/port */
	if (bind_serv(serv_sock, port) == -1) {
		perror(NULL);
		goto out_err;
	}

	/* set listening */
	if (listen(serv_sock, 1) == -1) {
		perror(NULL);
		goto out_err;
	}

	/* success */
	*serv_stat = 0;
	struct serv_cleanup_arg serv_cleanup_arg = {
		.serv_sock = serv_sock
	};
	pthread_cleanup_push(serv_cleanup, (void *)&serv_cleanup_arg);
	pthread_barrier_wait(server_ready);
	serv_forever();
	pthread_cleanup_pop(0);
	pthread_exit(0); // not necessary but just for aesthetics: thread exits only by being cancelled

	/* code doesn't get here unless error */
out_err:
	close(serv_sock);
out_nosock:
	*serv_stat = -1;
	pthread_barrier_wait(server_ready);
	return NULL;
}

static int syncprim_init(struct ws_ctube *ctube)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&ctube->_mutex, &attr);
	pthread_mutexattr_destroy(&attr);

	pthread_cond_init(&ctube->_data_ready_cond, NULL);
	return 0;
}

int ws_ctube_init(struct ws_ctube *ctube, int port)
{
	if (syncprim_init(ctube) != 0) {
		fprintf(stderr, "syncprim_init(): failed\n");
		fflush(stderr);
		return -1;
	}

	struct serv_main_arg serv_main_arg;
	serv_main_arg.port = port;
	serv_main_arg.ctube = ctube;
	serv_main_arg.serv_stat = 0;
	pthread_barrier_init(&serv_main_arg.server_ready, NULL, 2);

	if (pthread_create(&ctube->_serv_tid, NULL, serv_main, (void *)&serv_main_arg) != 0) {
		pthread_barrier_destroy(&serv_main_arg.server_ready);
		return -1;
	} else {
		pthread_barrier_wait(&serv_main_arg.server_ready);
		pthread_barrier_destroy(&serv_main_arg.server_ready);
		return serv_main_arg.serv_stat;
	}
}

void ws_ctube_lock(struct ws_ctube *ctube)
{
	pthread_mutex_lock(&ctube->_mutex);
}

void ws_ctube_unlock(struct ws_ctube *ctube)
{
	pthread_mutex_unlock(&ctube->_mutex);
}

void ws_ctube_broadcast(struct ws_ctube *ctube)
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
	pthread_cond_destroy(&ctube->_data_ready_cond);
}
