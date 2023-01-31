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

#define WS_CTUBE_DEBUG 1
#define WS_CTUBE_BUFLEN 4096

static int ws_dframe_init(struct ws_dframes *df)
{
	df->frames = NULL;
	df->frames_size = 0;

	df->refs = 0;
	pthread_mutex_init(&df->refs_mutex);
}

static void ws_dframe_destroy(struct ws_dframes *df)
{
	if (df->frames != NULL) {
		free(df->frames);
		df->frames = NULL;
	}
	df->frames_size = 0;
	pthread_mutex_destroy(&df->refs_mutex);
}

static void ws_dframe_acquire(struct ws_dframes *df)
{
	pthread_mutex_lock(&df->refs_mutex);
	df->refs++;
	pthread_mutex_unlock(&df->refs_mutex);
}

static void ws_dframe_release(struct ws_dframes *df)
{
	pthread_mutex_lock(&df->refs_mutex);
	df->refs--;
	if (df->refs == 0) {
		pthread_mutex_unlock(&df->refs_mutex);
		ws_dframe_destroy(df);
		free(df);
	} else {
		pthread_mutex_unlock(&df->refs_mutex);
	}
}

struct conn_struct {
	struct conn_struct *next;
	struct conn_struct *prev;
	pthread_mutex_t list_mutex;

	int fd;

	pthread_t protocol_tid;
	pthread_t sender_tid;

	int sender_inited;
	pthread_mutex_t sender_inited_mutex;
	pthread_cond_t sender_inited_cond;
};

static int conn_struct_init(struct conn_struct *conn)
{
	conn->next = NULL;
	conn->prev = NULL;
	pthread_mutex_init(&conn->list_mutex);

	conn->fd = -1;

	conn->sender_inited = 0;
	pthread_mutex_init(&conn->sender_inited_mutex);
	pthread_cond_init(&conn->sender_inited_cond);
}

static void conn_struct_destroy(struct conn_struct *conn)
{
	conn->next = NULL;
	conn->prev = NULL;
	pthread_mutex_destroy(&conn->list_mutex);

	conn->fd = -1;

	conn->sender_inited = 0;
	pthread_mutex_destroy(&conn->sender_inited_mutex);
	pthread_cond_destroy(&conn->sender_inited_cond);
}

struct conn_list {
	struct conn_struct head;
	struct conn_struct tail;
};

static int conn_list_init(struct conn_list *clist)
{
	conn_struct_init(&clist->head);
	conn_struct_init(&clist->tail);
	clist->head.next = &clist->tail;
	clist->tail.prev = &clist->head;
}

static void conn_list_destroy(struct conn_list *clist)
{
	conn_struct_destroy(&clist->head);
	conn_struct_destroy(&clist->tail);
}

static void conn_list_add(struct conn_list *clist, struct conn_struct *conn)
{
	struct conn_struct *a = &clist->head;
	struct conn_struct *b = conn;
	struct conn_struct *c = clist->head.next;

	pthread_mutex_lock(&a->list_mutex);
	pthread_mutex_lock(&b->list_mutex);
	pthread_mutex_lock(&c->list_mutex);
	a->next = b;
	b->prev = a;
	b->next = c;
	c->prev = b;
	pthread_mutex_unlock(&c->list_mutex);
	pthread_mutex_unlock(&b->list_mutex);
	pthread_mutex_unlock(&a->list_mutex);
}

static void conn_list_remove(struct conn_struct *conn)
{
	struct conn_struct *a = conn->prev;
	struct conn_struct *b = conn;
	struct conn_struct *c = conn->next;

	pthread_mutex_lock(&a->list_mutex);
	pthread_mutex_lock(&b->list_mutex);
	pthread_mutex_lock(&c->list_mutex);
	a->next = c;
	b->prev = NULL;
	b->next = NULL;
	c->prev = a;
	pthread_mutex_unlock(&c->list_mutex);
	pthread_mutex_unlock(&b->list_mutex);
	pthread_mutex_unlock(&a->list_mutex);
}

static void serv_forever()
{
}

struct serv_cleanup_arg {
	int serv_sock;
};

static void serv_cleanup(void *arg)
{
	int serv_sock = ((struct serv_cleanup_arg *)arg)->serv_sock;
	close(serv_sock);
}

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
	if (_bind_serv(serv_sock, port) == -1) {
		perror(NULL);
		goto out_nopipe;
	}

	/* set listening */
	if (listen(serv_sock, 1) == -1) {
		perror(NULL);
		goto out_nopipe;
	}

	/* success */
	*serv_stat = 0;
	struct serv_cleanup_arg serv_cleanup_arg = {
		.serv_sock = serv_sock,
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

static int _syncprim_init(struct ws_ctube *ctube)
{
	return 0;
}

static void _syncprim_destroy(struct ws_ctube *ctube)
{
}

int ws_ctube_init(struct ws_ctube *ctube, int port, int conn_limit, int timeout_ms)
{
	ctube->data = NULL;
	ctube->data_size = 0;
	ctube->data_ready = 0;
	if (_syncprim_init(ctube) != 0) {
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
	_syncprim_destroy(ctube);
out_nosyncprim:
	fprintf(stderr, "_syncprim_init(): failed\n");
	fflush(stderr);
	return -1;
}

void ws_ctube_destroy(struct ws_ctube *ctube)
{
	pthread_cancel(ctube->serv_tid);
	pthread_join(ctube->serv_tid, NULL);

	_syncprim_destroy(ctube);

	free(ctube->data);
	ctube->data = NULL;
}

void ws_ctube_broadcast(struct ws_ctube *ctube, void *data, size_t data_size)
{
	if (pthread_mutex_trylock(&ctube->mutex) == 0) {
		ctube->data = realloc(ctube->data, data_size);
		memcpy(ctube->data, data, data_size);
		ctube->data_size = data_size;

		ctube->data_ready = 1;
		pthread_cond_broadcast(&ctube->data_ready_cond);
		pthread_mutex_unlock(&ctube->mutex);
	}
}
