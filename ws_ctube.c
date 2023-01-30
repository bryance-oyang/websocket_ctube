/**
 * ws_ctube_init() spawns server thread, which spawns reader and writer threads
 */

#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "ws_ctube.h"
#include "ws_base.h"
#include "poll_list.h"

#define WS_CTUBE_DEBUG 1
#define WS_CTUBE_BUFLEN 4096

struct handler_arg {
	struct ws_ctube *ctube;
	struct poll_list *pl;

	/* below are effective copies of pl in order to not hold lock during polling */
	struct pollfd *fds;
	nfds_t nfds;
	int *handshake_complete;
};

/** make a copy of the poll_list so we can release lock */
static int pl_alloc_cpy(struct handler_arg *arg)
{
	pthread_mutex_lock(&arg->pl->mutex);
	arg->nfds = arg->pl->nfds;

	arg->fds = malloc(arg->nfds * sizeof(*arg->fds));
	if (arg->fds == NULL) {
		goto out_nofds;
	}

	arg->handshake_complete = malloc(arg->nfds * sizeof(*arg->handshake_complete));
	if (arg->handshake_complete == NULL) {
		goto out_nohs;
	}

	for (nfds_t i = 0; i < arg->pl->nfds; i++) {
		arg->fds[i].fd = arg->pl->fds[i];
		arg->handshake_complete[i] = arg->pl->handshake_complete[i];
	}
	pthread_mutex_unlock(&arg->pl->mutex);
	return 0;

out_nohs:
	free(arg->fds);
out_nofds:
	arg->fds = NULL;
	arg->handshake_complete = NULL;
	fprintf(stderr, "poll_list_cpy(): out of memory\n");
	fflush(stderr);
	pthread_mutex_unlock(&arg->pl->mutex);
	return -1;
}

static void pl_destroy_cpy(struct handler_arg *arg)
{
	free(arg->fds);
	free(arg->handshake_complete);
	arg->fds = NULL;
	arg->handshake_complete = NULL;
}

static int poll_events(struct pollfd *fds, nfds_t nfds, short events, int timeout)
{
	/* only reader will listen to server comms through pipe */
	if (events & POLLIN) {
		fds[0].events = POLLIN;
	} else {
		fds[0].events = 0;
	}

	for (nfds_t i = 1; i < nfds; i++) {
		fds[i].events = events;
	}

	return poll(fds, nfds, timeout);
}

static void clear_serv_pipe(struct pollfd *fds)
{
	char buf[WS_CTUBE_BUFLEN];
	if (fds[0].revents & POLLIN) {
		read(fds[0].fd, buf, WS_CTUBE_BUFLEN);
	}
}

/** update handshake and remove broken connections */
static int update_pl_from_cpy(struct handler_arg *arg)
{
	int retval = 0;
	pthread_mutex_lock(&arg->pl->mutex);
	/* update handshake status */
	for (nfds_t i = 1; i < arg->nfds; i++) {
		arg->pl->handshake_complete[i] = arg->handshake_complete[i];
	}

	/* remove broken connections */
	for (nfds_t i = 1; i < arg->nfds; i++) {
		if (arg->fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			if (poll_list_remove(arg->pl, arg->fds[i].fd) == PL_ENOMEM) {
				retval = -1;
				break;
			}
		}
	}
	pthread_mutex_unlock(&arg->pl->mutex);
	return retval;
}

/**
 * The reader responds to new connections, answers handshake, and pongs pings
 */
static int conn_reader(struct handler_arg *arg)
{

	/* poll and check if new connection signaled, if so make new cpy and repoll */
	for (;;) {
		if (poll_events(arg->fds, arg->nfds, POLLIN, -1) < 0) {
			perror(NULL);
			return -1;
		}
		if ((arg->fds[0].revents & POLLIN) == 0) {
			/* no more new connections */
			break;
		}
		clear_serv_pipe(arg->fds);
		pl_destroy_cpy(arg);
		pl_alloc_cpy(arg);
	}

	char msg[WS_CTUBE_BUFLEN];
	int msg_size;
	for (nfds_t i = 1; i < arg->nfds; i++) {
		if (!(arg->fds[i].revents & POLLIN)) {
			continue;
		}

		if (!(arg->handshake_complete[i])) {
			if (ws_handshake(arg->fds[i].fd) != 0) {
				arg->fds[i].revents = POLLERR;
				continue;
			}
			arg->handshake_complete[i] = 1;
		} else {
			if (ws_recv(arg->fds[i].fd, msg, &msg_size, WS_CTUBE_BUFLEN) != 0) {
				arg->fds[i].revents = POLLERR;
				continue;
			}
			if (ws_is_ping(msg, msg_size)) {
				if (ws_pong(arg->fds[i].fd, msg, msg_size) != 0) {
					arg->fds[i].revents = POLLERR;
					continue;
				}
			}
		}
	}

	if (update_pl_from_cpy(arg) != 0) {
		return -1;
	}

	return 0;
}

struct conn_writer_cleanup_arg {
	struct ws_ctube *ctube;
};

static void conn_writer_cleanup(void *arg)
{
	struct ws_ctube *ctube = ((struct conn_writer_cleanup_arg *)arg)->ctube;
	pthread_mutex_unlock(&ctube->mutex);
}

/** writer waits for data to be ready, then broadcasts to all clients */
static int conn_writer(struct handler_arg *arg)
{
	int retval = 0;
	struct ws_ctube *ctube = arg->ctube;
	struct conn_writer_cleanup_arg cleanup_arg = {
		.ctube = ctube
	};

	pthread_mutex_lock(&ctube->mutex);
	pthread_cleanup_push(conn_writer_cleanup, (void *)&cleanup_arg);

	while (!(ctube->data_ready)) {
		pthread_cond_wait(&ctube->data_ready_cond, &ctube->mutex);
	}

	/* check for possible writable sockets for efficiency sake */
	if (poll_events(arg->fds, arg->nfds, POLLOUT, 0) < 0) {
		perror(NULL);
		retval = -1;
		goto out;
	}

	/* send data to all connected clients that were polled as ready and completed handshake */
	for (nfds_t i = 1; i < arg->nfds; i++) {
		if (!(arg->fds[i].revents & POLLOUT)) {
			continue;
		}

		if (!(arg->handshake_complete[i])) {
			continue;
		}

		ws_send(arg->fds[i].fd, ctube->data, ctube->data_size);
	}

out:
	pthread_cleanup_pop(0);
	pthread_mutex_unlock(&arg->ctube->mutex);
	return retval;
}

struct handler_cleanup_arg {
	struct handler_arg *handler_arg;
};

static void handler_cleanup(void *arg) {
	struct handler_arg *handler_arg = ((struct handler_cleanup_arg *)arg)->handler_arg;
	pl_destroy_cpy(handler_arg);
}

struct handler_main_arg {
	struct ws_ctube *ctube;
	struct poll_list *pl;
	int (*handler_action)(struct handler_arg *arg);
	pthread_barrier_t *handler_ready;
};

static void *handler_main(void *arg)
{
	struct ws_ctube *ctube = ((struct handler_main_arg *)arg)->ctube;
	struct poll_list *pl = ((struct handler_main_arg *)arg)->pl;
	int (*handler_action)(struct handler_arg *arg) = ((struct handler_main_arg *)arg)->handler_action;
	pthread_barrier_t *handler_ready = ((struct handler_main_arg *)arg)->handler_ready;

	pthread_barrier_wait(handler_ready);

	int handler_retval;
	struct handler_arg handler_arg = {
		.ctube = ctube,
		.pl = pl,
		.fds = NULL,
		.nfds = 0,
		.handshake_complete = NULL
	};
	for (;;) {
		if (pl_alloc_cpy(&handler_arg) != 0) {
			goto out_err;
		}

		struct handler_cleanup_arg cleanup_arg = {
			.handler_arg = &handler_arg
		};

		pthread_cleanup_push(handler_cleanup, (void *)&cleanup_arg)
		handler_retval = handler_action(&handler_arg);
		pthread_cleanup_pop(0);
		pl_destroy_cpy(&handler_arg);

		if (handler_retval != 0) {
			goto out_err;
		}
	}

out_err:
	fprintf(stderr, "handler_main(): error\n");
	fflush(stderr);
	return NULL;
}

static int bind_serv(int serv_sock, int port) {
	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	return bind(serv_sock, (struct sockaddr *)&sa, sizeof(sa));
}

static int serv_spawn_handlers(
	struct ws_ctube *ctube,
	struct poll_list *pl,
	pthread_t *reader_tid,
	pthread_t *writer_tid,
	pthread_barrier_t *reader_ready,
	pthread_barrier_t *writer_ready)
{
	pthread_barrier_init(reader_ready, NULL, 2);
	struct handler_main_arg reader_arg = {
		.ctube = ctube,
		.pl = pl,
		.handler_action = conn_reader,
		.handler_ready = reader_ready
	};
	if (pthread_create(reader_tid, NULL, handler_main, (void *)&reader_arg) != 0) {
		goto out_noreader;
	}
	pthread_barrier_wait(reader_ready);

	pthread_barrier_init(writer_ready, NULL, 2);
	struct handler_main_arg writer_arg = {
		.ctube = ctube,
		.pl = pl,
		.handler_action = conn_writer,
		.handler_ready = writer_ready
	};
	if (pthread_create(writer_tid, NULL, handler_main, (void *)&writer_arg) != 0) {
		goto out_nowriter;
	}
	pthread_barrier_wait(writer_ready);

	return 0;

out_nowriter:
	pthread_barrier_wait(reader_ready);
	pthread_cancel(*reader_tid);
	pthread_join(*reader_tid, NULL);
out_noreader:
	fprintf(stderr, "serv_spawn_handlers(): failed\n");
	fflush(stderr);
	return -1;
}

static void serv_forever(int serv_sock, int serv_pipe, struct poll_list *pl)
{
	int conn, r;
	char buf = 0;
	for (;;) {
		conn = accept(serv_sock, NULL, NULL);
		r = poll_list_add(pl, conn);
		if (r == PL_ENOMEM) {
			goto out_err;
		}
		write(serv_pipe, &buf, 1);
	}

out_err:
	fprintf(stderr, "serv_forever(): error\n");
	int retval = -1;
	pthread_exit(&retval);
}

struct serv_cleanup_arg {
	int serv_sock;
	int *serv_pipe;
	pthread_t reader_tid;
	pthread_t writer_tid;
	pthread_barrier_t *reader_ready;
	pthread_barrier_t *writer_ready;
	struct poll_list *pl;
};

static void serv_cleanup(void *arg)
{
	int serv_sock = ((struct serv_cleanup_arg *)arg)->serv_sock;
	int *serv_pipe = ((struct serv_cleanup_arg *)arg)->serv_pipe;

	pthread_t reader_tid = ((struct serv_cleanup_arg *)arg)->reader_tid;
	pthread_t writer_tid = ((struct serv_cleanup_arg *)arg)->writer_tid;

	pthread_barrier_t *reader_ready = ((struct serv_cleanup_arg *)arg)->reader_ready;
	pthread_barrier_t *writer_ready = ((struct serv_cleanup_arg *)arg)->writer_ready;

	struct poll_list *pl = ((struct serv_cleanup_arg *)arg)->pl;

	pthread_cancel(writer_tid);
	pthread_cancel(reader_tid);
	pthread_join(writer_tid, NULL);
	pthread_join(reader_tid, NULL);

	pthread_barrier_destroy(reader_ready);
	pthread_barrier_destroy(writer_ready);

	poll_list_destroy(pl);

	close(serv_pipe[0]);
	close(serv_pipe[1]);
	close(serv_sock);
}

struct serv_main_arg {
	int port;
	struct ws_ctube *ctube;
	int conn_limit;

	int serv_stat;
	pthread_barrier_t *server_ready;
};

/** main for server */
static void *serv_main(void *arg)
{
	int port = ((struct serv_main_arg *)arg)->port;
	struct ws_ctube *ctube = ((struct serv_main_arg *)arg)->ctube;
	int conn_limit = ((struct serv_main_arg *)arg)->conn_limit;
	int *serv_stat = &((struct serv_main_arg *)arg)->serv_stat;
	pthread_barrier_t *server_ready = ((struct serv_main_arg *)arg)->server_ready;

	int serv_pipe[2];
	int serv_sock;

	/* create server socket */
	serv_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (serv_sock == -1) {
		perror(NULL);
		goto out_nosock;
	}
	int true = 1;
	if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &true,
		   sizeof(int)) == -1) {
		perror(NULL);
		goto out_nopipe;
	}

	/* set server socket address/port */
	if (bind_serv(serv_sock, port) == -1) {
		perror(NULL);
		goto out_nopipe;
	}

	/* set listening */
	if (listen(serv_sock, 1) == -1) {
		perror(NULL);
		goto out_nopipe;
	}

	/* create pipe to comm with handlers */
	if (pipe(serv_pipe) == -1) {
		perror(NULL);
		goto out_nopipe;
	}
	fcntl(serv_pipe[0], F_SETFL, O_NONBLOCK);
	fcntl(serv_pipe[1], F_SETFL, O_NONBLOCK);

	/* add pipe to pipe_list */
	struct poll_list pl;
	poll_list_init(&pl, conn_limit);
	if (poll_list_add(&pl, serv_pipe[0]) == -1) {
		goto out_nohandlers;
	}

	/* spawn reader and writer */
	pthread_t reader_tid, writer_tid;
	pthread_barrier_t reader_ready, writer_ready;
	if (serv_spawn_handlers(ctube, &pl, &reader_tid, &writer_tid, &reader_ready, &writer_ready) != 0) {
		goto out_nohandlers;
	}

	/* success */
	*serv_stat = 0;
	struct serv_cleanup_arg serv_cleanup_arg = {
		.serv_sock = serv_sock,
		.serv_pipe = serv_pipe,
		.reader_tid = reader_tid,
		.writer_tid = writer_tid,
		.reader_ready = &reader_ready,
		.writer_ready = &writer_ready,
		.pl = &pl
	};
	pthread_cleanup_push(serv_cleanup, (void *)&serv_cleanup_arg);
	pthread_barrier_wait(server_ready);
	serv_forever(serv_sock, serv_pipe[1], &pl);
	pthread_cleanup_pop(0);

	/* code doesn't get here unless error */
	pthread_barrier_destroy(&reader_ready);
	pthread_barrier_destroy(&writer_ready);
out_nohandlers:
	close(serv_pipe[0]);
	close(serv_pipe[1]);
out_nopipe:
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
	pthread_mutex_init(&ctube->mutex, &attr);
	pthread_mutexattr_destroy(&attr);

	pthread_cond_init(&ctube->data_ready_cond, NULL);
	pthread_barrier_init(&ctube->server_ready, NULL, 2);
	return 0;
}

static void syncprim_destroy(struct ws_ctube *ctube)
{
	pthread_mutex_destroy(&ctube->mutex);
	pthread_cond_destroy(&ctube->data_ready_cond);
	pthread_barrier_destroy(&ctube->server_ready);
}

int ws_ctube_init(struct ws_ctube *ctube, int port, int conn_limit)
{
	ctube->data = NULL;
	ctube->data_size = 0;
	ctube->data_ready = 0;
	if (syncprim_init(ctube) != 0) {
		goto out_nosyncprim;
	}

	struct serv_main_arg serv_main_arg = {
		.port = port,
		.ctube = ctube,
		.conn_limit = conn_limit,
		.serv_stat = 0,
		.server_ready = &ctube->server_ready
	};
	if (pthread_create(&ctube->serv_tid, NULL, serv_main, (void *)&serv_main_arg) != 0) {
		goto out_nothread;
	}

	pthread_barrier_wait(&ctube->server_ready);
	return serv_main_arg.serv_stat;

out_nothread:
	syncprim_destroy(ctube);
out_nosyncprim:
	fprintf(stderr, "syncprim_init(): failed\n");
	fflush(stderr);
	return -1;
}

void ws_ctube_destroy(struct ws_ctube *ctube)
{
	pthread_cancel(ctube->serv_tid);
	pthread_join(ctube->serv_tid, NULL);

	syncprim_destroy(ctube);

	free(ctube->data);
	ctube->data = NULL;
}

void ws_ctube_broadcast(struct ws_ctube *ctube, void *data, size_t bytes)
{
	if (pthread_mutex_trylock(&ctube->mutex) == 0) {
		ctube->data = realloc(ctube->data, bytes);
		memcpy(ctube->data, data, bytes);

		ctube->data_ready = 1;
		pthread_cond_broadcast(&ctube->data_ready_cond);
		pthread_mutex_unlock(&ctube->mutex);
	}
}
