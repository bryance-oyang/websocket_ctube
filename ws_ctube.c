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

typedef void (*cleanup_f)(void *);

static int ws_dframe_init(struct ws_dframes *df)
{
	df->frames = NULL;
	df->frames_size = 0;

	df->refs = 0;
	pthread_mutex_init(&df->refs_mutex, NULL);
}

static void ws_dframe_destroy(struct ws_dframes *df)
{
	if (df->frames != NULL) {
		free(df->frames);
		df->frames = NULL;
	}
	df->frames_size = 0;

	df->refs = 0;
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

void *reader_main(void *arg)
{
}

void *writer_main(void *arg)
{
}

struct conn_struct {
	struct conn_struct *next;
	struct conn_struct *prev;
	pthread_mutex_t list_mutex;

	int fd;
	struct ws_ctube *ctube;

	pthread_t reader_tid;
	pthread_t writer_tid;

	int refs;
	pthread_mutex_t refs_mutex;
};

static int conn_struct_init(struct conn_struct *conn, int fd, struct ws_ctube *ctube)
{
	conn->next = NULL;
	conn->prev = NULL;
	pthread_mutex_init(&conn->list_mutex, NULL);

	conn->fd = fd;
	conn->ctube = ctube;

	conn->refs = 0;
	pthread_mutex_init(&conn->refs_mutex, NULL);
}

static void conn_struct_destroy(struct conn_struct *conn)
{
	conn->next = NULL;
	conn->prev = NULL;
	pthread_mutex_destroy(&conn->list_mutex);

	conn->fd = -1;
	conn->ctube = NULL;

	conn->refs = 0;
	pthread_mutex_destroy(&conn->refs_mutex);
}

struct conn_list {
	/* sentinels */
	struct conn_struct head;
	struct conn_struct tail;
};

static int conn_list_init(struct conn_list *clist)
{
	conn_struct_init(&clist->head, -1, NULL);
	conn_struct_init(&clist->tail, -1, NULL);
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

static void conn_struct_acquire(struct conn_struct *conn)
{
	pthread_mutex_lock(&conn->refs_mutex);
	conn->refs++;
	pthread_mutex_unlock(&conn->refs_mutex);
}

static void conn_struct_release(struct conn_struct *conn)
{
	pthread_mutex_lock(&conn->refs_mutex);
	conn->refs--;
	if (conn->refs == 0) {
		conn_list_remove(conn);
		pthread_mutex_unlock(&conn->refs_mutex);

	} else {
		pthread_mutex_unlock(&conn->refs_mutex);
	}
}


static int bind_server(int server_sock, int port) {
	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	return bind(server_sock, (struct sockaddr *)&sa, sizeof(sa));
}

static void serve_forever()
{
}

static void *serv_main(void *arg)
{
	struct ws_ctube *ctube = (struct ws_ctube *)arg;

	/* create conn list */
	struct conn_list clist;
	if (conn_list_init(&clist) != 0) {
		goto err_noclist;
	}
	pthread_cleanup_push((cleanup_f)conn_list_destroy,

	/* create server socket */
	int server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0) {
		perror(NULL);
		goto err_nosock;
	}
	ctube->server_sock = server_sock;

	int yes = 1;
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &yes,
		   sizeof(yes)) < 0) {
		perror(NULL);
		goto out_nopipe;
	}

	/* set server socket address/port */
	if (bind_server(server_sock, ctube->port) < 0) {
		perror(NULL);
		goto out_nopipe;
	}

	/* set listening */
	if (listen(server_sock, 1) < 0) {
		perror(NULL);
		goto out_nopipe;
	}

	/* success */
	pthread_cleanup_push(server_cleanup, (void *)ctube);
	serve_forever();
	pthread_cleanup_pop(0);

	/* code doesn't get here unless error */
out_nopipe:
	close(server_sock);
err_nosock:
	return NULL;
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

	if (pthread_create(&ctube->framer_tid, NULL, framer_main, (void *)ctube) != 0) {
		fprintf(stderr, "ws_ctube_init(): create framer failed\n");
		goto err_noframer;
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
	pthread_cancel(ctube->framer_tid);
	pthread_join(ctube->framer_tid);
err_noframer:
	return -1;
}

void ws_ctube_destroy(struct ws_ctube *ctube)
{
	pthread_cancel(ctube->framer_tid);
	pthread_cancel(ctube->server_tid);
	pthread_join(ctube->framer_tid);
	pthread_join(ctube->server_tid);

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
		memcpy(ctube->data, data, data_size);
		ctube->data_size = data_size;
		pthread_cond_signal(&ctube->data_cond);
		pthread_mutex_unlock(&ctube->data_mutex);
		return 0;
	} else {
		return -1;
	}
}
