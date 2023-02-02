/** thread-safe doubly linked list */

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
	struct list_node tail;
};

static int list_init(struct list *l)
{
	list_node_init(&l->head);
	list_node_init(&l->tail);

	l->head.next = &l->tail;
	l->tail.prev = &l->head;

	return 0;
}

static void list_destroy(struct list *l)
{
	list_node_destroy(&l->head);
	list_node_destroy(&l->tail);
}

static void _list_link(struct list_node *a, struct list_node *b, struct list_node *c)
{
	a->next = b;
	b->prev = a;
	b->next = c;
	c->prev = b;
}

static void list_push_front(struct list *l, struct list_node *node)
{
	pthread_mutex_lock(&l->head.mutex);
	pthread_mutex_lock(&node->mutex);
	pthread_mutex_lock(&l->head.next->mutex);

	_list_link(&l->head, node, l->head.next);

	pthread_mutex_unlock(&l->head.next->mutex);
	pthread_mutex_unlock(&node->mutex);
	pthread_mutex_unlock(&l->head.mutex);
}

static struct list_node *list_pop_front(struct list *l)
{
	struct list_node
	pthread_mutex_lock(&l->head.mutex);
	struct list_node *node = l->head.next;
	if (node == &l->tail) {
		pthread_mutex_unlock(&l->head.mutex);
		return NULL;
	}
	pthread_mutex_lock(&node->mutex);
	pthread_mutex_lock(&node->next->mutex);

	pthread_mutex_unlock(&node->next->mutex);
	pthread_mutex_unlock(&node->mutex);
	pthread_mutex_unlock(&l->head.mutex);
	return node;
}

static void list_push_back(struct list *l, struct list_node *node)
{
	pthread_mutex_lock(&l->tail.prev->mutex);
	pthread_mutex_lock(&node->mutex);
	pthread_mutex_lock(&l->)
	_list_link(l->tail.prev, node, &l->tail);
}

static struct list_node *list_pop_back(struct list *l)
{
	struct list_node *node = l->tail.prev;
	if (node == &l->head) {
		return NULL;
	}

	list_node_unlink(node);
	return node;
}

#define list_for_each(list, node) \
	for ( \
	pthread_mutex_lock(&(list)->head.mutex), \
	node = (list)->head.next, \
	pthread_mutex_lock(&node->mutex), \
	pthread_mutex_unlock(&(list)->head.mutex) \
	; \
	node != &(list)->tail \
	|| (pthread_mutex_unlock(&node->mutex) && 0) \
	; \
	node = node->next, \
	pthread_mutex_lock(&node->mutex), \
	pthread_mutex_unlock(&node->prev->mutex))

#define list_for_each_entry(list, node, entry, member) \
	for ( \
	pthread_mutex_lock(&(list)->head.mutex), \
	node = (list)->head.next, \
	pthread_mutex_lock(&node->mutex), \
	pthread_mutex_unlock(&(list)->head.mutex), \
	entry = container_of(node, typeof(*entry), member) \
	; \
	node != &(list)->tail \
	|| (pthread_mutex_unlock(&node->mutex) && 0)\
	; \
	node = node->next, \
	pthread_mutex_lock(&node->mutex), \
	pthread_mutex_unlock(&node->prev->mutex), \
	entry = container_of(node, typeof(*entry), member))

#endif /* LIST_H */
