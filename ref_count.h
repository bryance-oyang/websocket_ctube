/** thread-safe reference counting */

#ifndef REF_COUNT_H
#define REF_COUNT_H

#include <pthread.h>

struct ref_count {
	int refc;
	pthread_mutex_t mutex;
};

static int ref_count_init(struct ref_count *ref_count)
{
	ref_count->refc = 0;
	pthread_mutex_init(&ref_count->mutex, NULL);
	return 0;
}

static void ref_count_destroy(struct ref_count *ref_count)
{
	ref_count->refc = 0;
	pthread_mutex_destroy(&ref_count->mutex);
}

static void ref_count_acquire(struct ref_count *ref_count)
{
	pthread_mutex_lock(&ref_count->mutex);
	ref_count->refc++;
	pthread_mutex_unlock(&ref_count->mutex);
}

static void ref_count_release(struct ref_count *ref_count, void (*release_routine)(struct ref_count *))
{
	pthread_mutex_lock(&ref_count->mutex);
	ref_count->refc--;
	if (ref_count->refc == 0) {
		pthread_mutex_unlock(&ref_count->mutex);
		release_routine(ref_count);
	} else {
		pthread_mutex_unlock(&ref_count->mutex);
	}
}

#endif /* REF_COUNT_H */
