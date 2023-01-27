#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "ws_ctube.h"

#define BUFLEN 4096
typedef int new_conn_t;

struct conn_handler_td {
	struct ws_ctube *ctube;
	int serv_pipefd;
};

static void pollfds_init(struct pollfd *const fds, int data_ready_pipefd, int serv_pipefd)
{
	fds[0].fd = data_ready_pipefd;
	fds[1].fd = serv_pipefd;
}

/** poll all for reading */
static int poll_read(struct pollfd *const fds, nfds_t nfds)
{
	for (nfds_t i = 0; i < nfds; i++) {
		fds[i].events = POLLIN;
	}
	return poll(fds, nfds, -1);
}

/** poll the non-pipe connections for writing */
static int poll_write(struct pollfd *const fds, nfds_t nfds)
{
	fds[0].events = 0;
	fds[1].events = 0;
	for (nfds_t i = 2; i < nfds; i++) {
		fds[i].events = POLLOUT;
	}
	return poll(fds, nfds, -1);
}

static void handle_data_ready(struct ws_ctube *restrict ctube, struct pollfd *restrict fds, nfds_t nfds)
{
	poll_write(fds, nfds);

	ws_ctube_lock(ctube);
	ws_ctube_unlock(ctube);
}

static void handle_income(int conn)
{
	/* TODO: ping response */
	return;
}

static void handle_new_conn(struct pollfd *restrict *restrict const fds, nfds_t *restrict const nfds, char *const buf)
{
	int nread;
	int ntoread = sizeof(new_conn_t);

	char *pbuf = buf;
	while (ntoread > 0) {
		nread = read((*fds)[1].fd, pbuf, ntoread);
		if (nread <= 0) {
			return;
		}
		ntoread -= nread;
		pbuf += ntoread;
	}
	if (ntoread != 0) {
		fprintf(stderr, "handle_new_conn(): weird pipe read");
		fflush(stderr);
		raise(SIGSEGV);
	}

	new_conn_t conn;
	memcpy(&conn, buf, sizeof(new_conn_t));

	(*nfds)++;
	*fds = realloc(*fds, *nfds * sizeof(**fds));
	(*fds)[*nfds - 1].fd = conn;
}

static void handle_bad_conn(struct pollfd *restrict const fds, nfds_t *restrict const nfds, nfds_t i)
{
	if (i < *nfds - 1) {
		memmove(&fds[i], &fds[i + 1], (*nfds - i - 1) * sizeof(*fds));
	}
	(*nfds)--;
}

/** main for connection handler */
static void *conn_handler_main(void *arg)
{
	struct ws_ctube *ctube = ((struct conn_handler_td *)arg)->ctube;
	int serv_pipefd = ((struct conn_handler_td *)arg)->serv_pipefd;

	nfds_t nfds = 2;
	struct pollfd *fds;

	fds = malloc(nfds * sizeof(*fds));
	if (fds == NULL) {
		goto out_no_fds;
	}
	pollfds_init(fds, ctube->_data_ready_pipefd[0], serv_pipefd);

	char buf[BUFLEN];
	if (BUFLEN < sizeof(new_conn_t)) {
		fprintf(stderr, "conn_handler_main(): buf too small");
		fflush(stderr);
		raise(SIGSEGV);
	}

	/* main event loop */
	for (;;) {
		poll_read(fds, nfds);

		/* pipes */
		if (fds[0].revents & POLLIN) {
			handle_data_ready(ctube, fds, nfds);
		}
		if (fds[1].revents & POLLIN) {
			handle_new_conn(&fds, &nfds, buf);
		}

		/* connections */
		for (nfds_t i = 2; i < nfds; i++) {
			if (fds[i].revents == 0) {
				continue;
			}

			if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				handle_bad_conn(fds, &nfds, i);
				continue;
			}

			if (fds[i].revents & POLLIN) {
				handle_income(fds[i].fd);
			}
		}
	}

	/* cleanup */
	free(fds);
out_no_fds:
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

struct serv_td {
	int port;
	struct ws_ctube *ctube;

	int serv_stat;
	pthread_barrier_t server_ready;
};

static int bind_serv(int serv_sock, int port) {
	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	return bind(serv_sock, (struct sockaddr *)&sa, sizeof(sa));
}

/** main for server */
static void *serv_main(void *arg)
{
	int port = ((struct serv_td *)arg)->port;
	struct ws_ctube *ctube = ((struct serv_td *)arg)->ctube;
	int *serv_stat = &((struct serv_td *)arg)->serv_stat;
	pthread_barrier_t *server_ready = &((struct serv_td *)arg)->server_ready;

	/* create server socket */
	int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sock == -1) {
		perror(NULL);
		*serv_stat = -1;
		pthread_barrier_wait(server_ready);
		goto out_nosock;
	}
	int true = 1;
	if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &true,
		   sizeof(int)) == -1) {
		perror(NULL);
		*serv_stat = -1;
		pthread_barrier_wait(server_ready);
		goto out_nopipe;
	}

	/* set server socket address/port */
	if (bind_serv(serv_sock, port) == -1) {
		perror(NULL);
		*serv_stat = -1;
		pthread_barrier_wait(server_ready);
		goto out_nopipe;
	}

	/* set listening */
	if (listen(serv_sock, 1) == -1) {
		perror(NULL);
		*serv_stat = -1;
		pthread_barrier_wait(server_ready);
		goto out_nopipe;
	}

	/* setup client connection handler */
	/* we use this pipe to communicate new connections to the client
	 * connection handler thread */
	int serv_pipefd[2];
	if (pipe(serv_pipefd) != 0) {
		perror(NULL);
		*serv_stat = -1;
		pthread_barrier_wait(server_ready);
		goto out_nopipe;
	}
	if (conn_handler_init(ctube, serv_pipefd[0]) != 0) {
		*serv_stat = -1;
		pthread_barrier_wait(server_ready);
		goto out;
	}

	/* success */
	*serv_stat = 0;
	pthread_barrier_wait(server_ready);

	/* serve forever */
	new_conn_t conn;
	char buf[BUFLEN];
	for (;;) {
		/* new connection fd are written through the pipe to connection
		 * handler thread */
		conn = accept(serv_sock, NULL, NULL);
		memcpy(buf, &conn, sizeof(new_conn_t));
		write(serv_pipefd[1], buf, sizeof(new_conn_t));
	}

	/* cleanup */
out:
	close(serv_pipefd[0]);
	close(serv_pipefd[1]);
out_nopipe:
	close(serv_sock);
out_nosock:
	return NULL;
}

static int syncprim_init(struct ws_ctube *ctube)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&ctube->_mutex, &attr);
	pthread_mutexattr_destroy(&attr);

	return pipe(ctube->_data_ready_pipefd);
}

int ws_ctube_init(struct ws_ctube *ctube, int port)
{
	if (syncprim_init(ctube) != 0) {
		fprintf(stderr, "syncprim_init(): failed");
		fflush(stderr);
		return -1;
	}

	struct serv_td serv_td;
	serv_td.port = port;
	serv_td.ctube = ctube;
	serv_td.serv_stat = 0;
	pthread_barrier_init(&serv_td.server_ready, NULL, 2);

	if (pthread_create(&ctube->_serv_tid, NULL, serv_main, (void *)&serv_td) != 0) {
		pthread_barrier_destroy(&serv_td.server_ready);
		return -1;
	} else {
		pthread_barrier_wait(&serv_td.server_ready);
		pthread_barrier_destroy(&serv_td.server_ready);
		return serv_td.serv_stat;
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
	char buf = '\0';
	write(ctube->_data_ready_pipefd[1], &buf, 1);
}

void ws_ctube_destroy(struct ws_ctube *ctube)
{
	pthread_cancel(ctube->_serv_tid);
	pthread_cancel(ctube->_conn_tid);
	pthread_join(ctube->_serv_tid, NULL);
	pthread_join(ctube->_conn_tid, NULL);

	pthread_mutex_destroy(&ctube->_mutex);

	close(ctube->_data_ready_pipefd[0]);
	close(ctube->_data_ready_pipefd[1]);
}
