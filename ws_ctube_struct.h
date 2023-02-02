#ifndef WS_CTUBE_STRUCT_H
#define WS_CTUBE_STRUCT_H

#include <pthread.h>
#include "container_of.h"
#include "ref_count.h"
#include "list.h"
#include "ws_ctube.h"

struct dframes {
	char *frames;
	size_t frames_size;
	struct ref_count refc;
};

static int dframes_init(struct dframes *df)
{
	df->frames = NULL;
	df->frames_size = 0;
	ref_count_init(&df->refc);
	return 0;
}

static void dframes_destroy(struct dframes *df)
{
	if (df->frames != NULL) {
		free(df->frames);
		df->frames = NULL;
	}
	df->frames_size = 0;
	ref_count_destroy(&df->refc);
}

static void dframes_free(struct dframes *df)
{
	dframes_destroy(df);
	free(df);
}

struct conn_struct {
	int fd;
	struct ws_ctube *ctube;

	pthread_t reader_tid;
	pthread_t writer_tid;

	struct ref_count refc;
	struct list_node lnode;
};

static int conn_struct_init(struct conn_struct *conn, int fd, struct ws_ctube *ctube)
{
	conn->fd = fd;
	conn->ctube = ctube;

	ref_count_init(&conn->refc);
	list_node_init(&conn->lnode);
	return 0;
}

static void conn_struct_destroy(struct conn_struct *conn)
{
	conn->fd = -1;
	conn->ctube = NULL;

	ref_count_destroy(&conn->refc);
	list_node_destroy(&conn->lnode);
}

static void conn_struct_free(struct conn_struct *conn)
{
	conn_struct_destroy(conn);
	free(conn);
}

enum qaction {
	WS_CONN_CREATE,
	WS_CONN_DESTROY
};

struct conn_qentry {
	struct conn_struct *conn;
	enum qaction act;
	struct list_node lnode;
};

static struct conn_qentry *conn_qentry_alloc(struct conn_struct *conn, enum qaction act)
{
	struct conn_qentry *qentry = malloc(sizeof(*qentry));
	if (qentry == NULL) {
		return NULL;
	}

	ref_count_acquire(conn, refc);
	qentry->conn = conn;
	qentry->act = act;
	list_node_init(&qentry->lnode);

	return qentry;
}

static void conn_qentry_free(struct conn_qentry *qentry)
{
	ref_count_release(qentry->conn, refc, conn_struct_free);
	qentry->conn = NULL;
	list_node_destroy(&qentry->lnode);

	free(qentry);
}

#endif /* WS_CTUBE_STRUCT_H */
