#ifndef WS_CTUBE_STRUCT_H
#define WS_CTUBE_STRUCT_H

#include <pthread.h>
#include "container_of.h"
#include "ref_count.h"
#include "list.h"
#include "ws_ctube.h"

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
