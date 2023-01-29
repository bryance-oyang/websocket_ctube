#ifndef POLL_LIST_H
#define POLL_LIST_H

#define PL_ENOMEM -1
#define PL_ECONNLIM -2

struct poll_list {
	int *fds;
	nfds_t nfds;
	nfds_t cap;
	nfds_t conn_limit;
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

static int poll_list_add(struct poll_list *restrict pl, int fd)
{
	pthread_mutex_lock(&pl->mutex);
	if (pl->nfds == pl->conn_limit) {
		return PL_ECONNLIM;
	}

	if (pl->nfds == pl->cap) {
		pl->cap = pl->cap * 2 + 1;
		pl->fds = realloc(pl->fds, pl->cap * sizeof(*pl->fds));
		if (pl->fds == NULL) {
			fprintf(stderr, "poll_list_add(): out of mem\n");
			fflush(stderr);
			return PL_ENOMEM;
		}
	}

	pl->fds[pl->nfds] = fd;
	pl->nfds++;
	pthread_mutex_unlock(&pl->mutex);
	return 0;
}

static int poll_list_remove(struct poll_list *restrict pl, int fd)
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
	}
	pl->nfds--;

	nfds_t half_cap = pl->cap / 2;
	if (pl->nfds < half_cap) {
		pl->cap = half_cap;
		pl->fds = realloc(pl->fds, pl->cap * sizeof(*pl->fds));
		if (pl->fds == NULL) {
			fprintf(stderr, "poll_list_add(): out of mem\n");
			fflush(stderr);
			return PL_ENOMEM;
		}
	}
	pthread_mutex_unlock(&pl->mutex);
	return 0;
}

static struct pollfd *poll_list_alloc_cpy(struct poll_list *restrict pl)
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

#endif /* POLL_LIST_H */