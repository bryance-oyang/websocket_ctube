/**
 * @file
 * @brief thread-safe doubly linked list
 */

#ifndef WS_CTUBE_LIST_H
#define WS_CTUBE_LIST_H

#include <pthread.h>
#include <stddef.h>
#include "container_of.h"

struct ws_ctube_list_node {
	struct ws_ctube_list_node *prev;
	struct ws_ctube_list_node *next;
	pthread_mutex_t mutex;
};

static int ws_ctube_list_node_init(struct ws_ctube_list_node *node)
{
	node->prev = NULL;
	node->next = NULL;
	pthread_mutex_init(&node->mutex, NULL);
	return 0;
}

static void ws_ctube_list_node_destroy(struct ws_ctube_list_node *node)
{
	node->prev = NULL;
	node->next = NULL;
	pthread_mutex_destroy(&node->mutex);
}

struct ws_ctube_list {
	struct ws_ctube_list_node head;
	int len;
	pthread_mutex_t mutex;
};

static int ws_ctube_list_init(struct ws_ctube_list *l)
{
	ws_ctube_list_node_init(&l->head);
	l->head.next = &l->head;
	l->head.prev = &l->head;
	l->len = 0;
	pthread_mutex_init(&l->mutex, NULL);
	return 0;
}

static void ws_ctube_list_destroy(struct ws_ctube_list *l)
{
	ws_ctube_list_node_destroy(&l->head);
	pthread_mutex_destroy(&l->mutex);
}

static void _ws_ctube_list_add_after(struct ws_ctube_list_node *prior, struct ws_ctube_list_node *new_node)
{
	new_node->prev = prior;
	new_node->next = prior->next;
	prior->next->prev = new_node;
	prior->next = new_node;
}

static void _ws_ctube_list_node_unlink(struct ws_ctube_list_node *node)
{
	node->next->prev = node->prev;
	node->prev->next = node->next;
	node->next = NULL;
	node->prev = NULL;
}

static inline void ws_ctube_list_unlink(struct ws_ctube_list *l, struct ws_ctube_list_node *node)
{
	pthread_mutex_lock(&l->mutex);
	pthread_mutex_lock(&node->mutex);
	_ws_ctube_list_node_unlink(node);
	l->len--;
	pthread_mutex_unlock(&node->mutex);
	pthread_mutex_unlock(&l->mutex);
}

static inline int ws_ctube_list_push_front(struct ws_ctube_list *l, struct ws_ctube_list_node *node)
{
	int retval = 0;
	pthread_mutex_lock(&l->mutex);
	pthread_mutex_lock(&node->mutex);

	if (node->next != NULL || node->prev != NULL) {
		retval = -1;
		goto out;
	}
	_ws_ctube_list_add_after(&l->head, node);
	l->len++;

out:
	pthread_mutex_unlock(&node->mutex);
	pthread_mutex_unlock(&l->mutex);
	return retval;
}

static inline int ws_ctube_list_push_back(struct ws_ctube_list *l, struct ws_ctube_list_node *node)
{
	int retval = 0;
	pthread_mutex_lock(&l->mutex);
	pthread_mutex_lock(&node->mutex);

	if (node->next != NULL || node->prev != NULL) {
		retval = -1;
		goto out;
	}
	_ws_ctube_list_add_after(l->head.prev, node);
	l->len++;

out:
	pthread_mutex_unlock(&node->mutex);
	pthread_mutex_unlock(&l->mutex);
	return retval;
}

static inline struct ws_ctube_list_node *ws_ctube_list_pop_front(struct ws_ctube_list *l)
{
	struct ws_ctube_list_node *front;

	pthread_mutex_lock(&l->mutex);
	if (l->len == 0) {
		pthread_mutex_unlock(&l->mutex);
		return NULL;
	}
	front = l->head.next;
	pthread_mutex_lock(&front->mutex);

	_ws_ctube_list_node_unlink(front);
	l->len--;

	pthread_mutex_unlock(&front->mutex);
	pthread_mutex_unlock(&l->mutex);
	return front;
}

static inline struct ws_ctube_list_node *ws_ctube_list_pop_back(struct ws_ctube_list *l)
{
	struct ws_ctube_list_node *back;

	pthread_mutex_lock(&l->mutex);
	if (l->len == 0) {
		pthread_mutex_unlock(&l->mutex);
		return NULL;
	}
	back = l->head.prev;
	pthread_mutex_lock(&back->mutex);

	_ws_ctube_list_node_unlink(back);
	l->len--;

	pthread_mutex_unlock(&back->mutex);
	pthread_mutex_unlock(&l->mutex);
	return back;
}

/** loop through list_node */
#define ws_ctube_list_for_each(list, node) \
	for ( \
	node = (list)->head.next; \
	node != &((list)->head); \
	node = node->next)

/** loop through containers of list_node */
#define ws_ctube_list_for_each_entry(list, entry, member) \
	for ( \
	entry = (list)->head.next != &((list)->head) ? \
	ws_ctube_container_of((list)->head.next, typeof(*entry), member) : \
	NULL; \
	entry != NULL; \
	entry = entry->member.next != &((list)->head) ? \
	ws_ctube_container_of(entry->member.next, typeof(*entry), member) : \
	NULL)

#endif /* WS_CTUBE_LIST_H */
