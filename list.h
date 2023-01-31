#ifndef LIST_H
#define LIST_H

#include <stddef.h>
#include <pthread.h>

struct list_node {
	struct list_node *prev;
	struct list_node *next;
	pthread_mutex_t mod_mutex;
	pthread_mutex_t link_mutex;
};

static int list_node_init(struct list_node *node)
{
	node->prev = NULL;
	node->next = NULL;
	pthread_mutex_init(&node->mod_mutex, NULL);
	pthread_mutex_init(&node->link_mutex, NULL);
	return 0;
}

/** unlink first before destroying; you should not destroy a linked node */
static void list_node_destroy(struct list_node *node)
{
	node->prev = NULL;
	node->next = NULL;
	pthread_mutex_destroy(&node->mod_mutex);
	pthread_mutex_destroy(&node->link_mutex);
}

/** do nothing if node already unlinked */
static void list_node_unlink(struct list_node *node)
{
	pthread_mutex_lock(&node->mod_mutex);
	struct list_node *a = node->prev;
	struct list_node *b = node;
	struct list_node *c = node->next;
	if (a == NULL || c == NULL)

	pthread_mutex_lock(&a->link_mutex);
	pthread_mutex_lock(&b->link_mutex);
	pthread_mutex_lock(&c->link_mutex);
	a->next = c;
	b->prev = NULL;
	b->next = NULL;
	c->prev = a;
	pthread_mutex_unlock(&c->link_mutex);
	pthread_mutex_unlock(&b->link_mutex);
	pthread_mutex_unlock(&a->link_mutex);
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
	list_node_destroy(l->head);
	list_node_destroy(l->tail);
}

/** do nothing if list already linked */
static void _list_link(struct list_node *a, struct list_node *b, struct list_node *c)
{
	pthread_mutex_lock(&a->link_mutex);
	pthread_mutex_lock(&b->link_mutex);
	pthread_mutex_lock(&c->link_mutex);
	a->next = b;
	b->prev = a;
	b->next = c;
	c->prev = b;
	pthread_mutex_unlock(&c->link_mutex);
	pthread_mutex_unlock(&b->link_mutex);
	pthread_mutex_unlock(&a->link_mutex);
}

static void list_push_front(struct list *l, struct list_node *node)
{

}

#endif /* LIST_H */
