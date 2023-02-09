/** @file
 * @brief thread-safe doubly linked list
 */

#ifndef LIST_H
#define LIST_H

#include <stddef.h>
#include <pthread.h>
#include "container_of.h"

struct list_node {
	struct list_node *prev;
	struct list_node *next;
	pthread_mutex_t mutex;
};

static int list_node_init(struct list_node *node)
{
	node->prev = NULL;
	node->next = NULL;
	pthread_mutex_init(&node->mutex, NULL);
	return 0;
}

static void list_node_destroy(struct list_node *node)
{
	node->prev = NULL;
	node->next = NULL;
	pthread_mutex_destroy(&node->mutex);
}

struct list {
	struct list_node head;
	int len;
	pthread_mutex_t mutex;
};

static int list_init(struct list *l)
{
	list_node_init(&l->head);
	l->head.next = &l->head;
	l->head.prev = &l->head;
	l->len = 0;
	pthread_mutex_init(&l->mutex, NULL);
	return 0;
}

static void list_destroy(struct list *l)
{
	list_node_destroy(&l->head);
	pthread_mutex_destroy(&l->mutex);
}

static void _list_add_after(struct list_node *prior, struct list_node *new_node)
{
	new_node->prev = prior;
	new_node->next = prior->next;
	prior->next->prev = new_node;
	prior->next = new_node;
}

static void _list_node_unlink(struct list_node *node)
{
	node->next->prev = node->prev;
	node->prev->next = node->next;
	node->next = NULL;
	node->prev = NULL;
}

static inline void list_unlink(struct list *l, struct list_node *node)
{
	pthread_mutex_lock(&node->mutex);
	pthread_mutex_lock(&l->mutex);
	_list_node_unlink(node);
	pthread_mutex_unlock(&l->mutex);
	pthread_mutex_unlock(&node->mutex);
}

static inline void list_push_front(struct list *l, struct list_node *node)
{
	pthread_mutex_lock(&node->mutex);
	pthread_mutex_lock(&l->mutex);

	if (node->next != NULL || node->prev != NULL) {
		goto out;
	}
	_list_add_after(&l->head, node);
	l->len++;

out:
	pthread_mutex_unlock(&l->mutex);
	pthread_mutex_unlock(&node->mutex);
}

static inline void list_push_back(struct list *l, struct list_node *node)
{
	pthread_mutex_lock(&node->mutex);
	pthread_mutex_lock(&l->mutex);

	if (node->next != NULL || node->prev != NULL) {
		goto out;
	}
	_list_add_after(l->head.prev, node);
	l->len++;

out:
	pthread_mutex_unlock(&l->mutex);
	pthread_mutex_unlock(&node->mutex);
}

/**
 * pops and returns locked node
 */
static inline struct list_node *list_lockpop_front(struct list *l)
{
	struct list_node *front;

	/* optimistically lock front mutex but check front is still front */
	for (;;) {
		front = l->head.next;

		pthread_mutex_lock(&front->mutex);
		pthread_mutex_lock(&l->mutex);
		if (front == &l->head) {
			pthread_mutex_unlock(&l->mutex);
			pthread_mutex_unlock(&front->mutex);
			return NULL;
		} else if (front != l->head.next) {
			pthread_mutex_unlock(&l->mutex);
			pthread_mutex_unlock(&front->mutex);
			continue;
		} else {
			break;
		}
	}

	_list_node_unlink(front);
	l->len--;
	pthread_mutex_unlock(&l->mutex);
	return front;
}

/**
 * pops and returns locked node
 */
static inline struct list_node *list_lockpop_back(struct list *l)
{
	struct list_node *back;

	/* optimistically lock back mutex but check back is still back */
	for (;;) {
		back = l->head.prev;

		pthread_mutex_lock(&back->mutex);
		pthread_mutex_lock(&l->mutex);
		if (back == &l->head) {
			pthread_mutex_unlock(&l->mutex);
			pthread_mutex_unlock(&back->mutex);
			return NULL;
		} else if (back != l->head.prev) {
			pthread_mutex_unlock(&l->mutex);
			pthread_mutex_unlock(&back->mutex);
			continue;
		} else {
			break;
		}
	}

	_list_node_unlink(back);
	l->len--;
	pthread_mutex_unlock(&l->mutex);
	return back;
}

#define list_for_each(list, node) \
	for ( \
	node = (list)->head.next; \
	node != &((list)->head); \
	node = node->next)

#define list_for_each_entry(list, entry, member) \
	for ( \
	entry = (list)->head.next != &((list)->head) ? \
	container_of((list)->head.next, typeof(*entry), member) : \
	NULL; \
	entry != NULL; \
	entry = entry->member.next != &((list)->head) ? \
	container_of(entry->member.next, typeof(*entry), member) : \
	NULL)

#endif /* LIST_H */
