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

static void dframes_free(struct ref_count *refc)
{
	struct dframes *df = container_of(refc, struct dframes, refc);
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
}

static void conn_struct_destroy(struct conn_struct *conn)
{
	conn->fd = -1;
	conn->ctube = NULL;

	ref_count_destroy(&conn->refc);
	list_node_destroy(&conn->lnode);
}

static void conn_struct_free(struct ref_count *refc)
{
	struct conn_struct *conn = container_of(refc, struct conn_struct, refc);
	conn_struct_destroy(conn);
	free(conn);
}

enum qaction {
	CONN_CREATE,
	CONN_DESTROY
};

struct conn_qentry {
	struct conn_struct *conn;
	enum qaction act;

	struct list_node *lnode;
};

#endif /* WS_CTUBE_STRUCT_H */
