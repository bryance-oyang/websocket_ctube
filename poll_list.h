#ifndef POLL_LIST_H
#define POLL_LIST_H

#define PL_ENOMEM -1
#define PL_ECONNLIM -2

struct poll_list {
	int *fds;
	int *handshake_complete;
	nfds_t nfds;
	nfds_t cap;
	nfds_t conn_limit;
	pthread_mutex_t mutex;
};

static void poll_list_init(struct poll_list *pl, int conn_limit)
{
	pl->fds = NULL;
	pl->handshake_complete = NULL;
	pl->nfds = 0;
	pl->cap = 0;
	pl->conn_limit = conn_limit;
	pthread_mutex_init(&pl->mutex, NULL);
}

static void poll_list_destroy(struct poll_list *pl)
{
	if (pl->fds != NULL) {
		free(pl->fds);
	}
	pl->fds = NULL;
	if (pl->handshake_complete != NULL) {
		free(pl->handshake_complete);
	}
	pl->handshake_complete = NULL;
	pthread_mutex_destroy(&pl->mutex);
}

static int poll_list_add(struct poll_list *pl, int fd)
{
	pthread_mutex_lock(&pl->mutex);
	/* 0th is reserved for pipe */
	if (pl->nfds >= pl->conn_limit + 1) {
		return PL_ECONNLIM;
	}

	if (pl->nfds == pl->cap) {
		pl->cap = pl->cap * 2 + 1;
		pl->fds = realloc(pl->fds, pl->cap * sizeof(*pl->fds));
		pl->handshake_complete = realloc(pl->handshake_complete, pl->cap * sizeof(*pl->handshake_complete));
		if (pl->fds == NULL || pl->handshake_complete == NULL) {
			fprintf(stderr, "poll_list_add(): out of mem\n");
			fflush(stderr);
			return PL_ENOMEM;
		}
	}

	pl->fds[pl->nfds] = fd;
	pl->handshake_complete[pl->nfds] = 0;
	pl->nfds++;
	pthread_mutex_unlock(&pl->mutex);
	return 0;
}

static int poll_list_remove(struct poll_list *pl, int fd)
{
	pthread_mutex_lock(&pl->mutex);
	nfds_t i;
	for (i = 0; i < pl->nfds; i++) {
		if (fd == pl->fds[i]) {
			break;
		}
	}
	if (i == pl->nfds) {
		return 0;
	}

	if (i < pl->nfds - 1) {
		memmove(&pl->fds[i], &pl->fds[i + 1], (pl->nfds - i - 1) * sizeof(*pl->fds));
		memmove(&pl->handshake_complete[i], &pl->handshake_complete[i + 1], (pl->nfds - i - 1) * sizeof(*pl->handshake_complete));
	}
	pl->nfds--;

	nfds_t half_cap = pl->cap / 2;
	if (pl->nfds < half_cap) {
		pl->cap = half_cap;
		pl->fds = realloc(pl->fds, pl->cap * sizeof(*pl->fds));
		pl->handshake_complete = realloc(pl->handshake_complete, pl->cap * sizeof(*pl->handshake_complete));
		if (pl->fds == NULL || pl->handshake_complete == NULL) {
			fprintf(stderr, "poll_list_add(): out of mem\n");
			fflush(stderr);
			return PL_ENOMEM;
		}
	}
	pthread_mutex_unlock(&pl->mutex);
	return 0;
}

#endif /* POLL_LIST_H */
