#ifndef CONTAINER_OF_H
#define CONTAINER_OF_H

#include "static_assert.h"

#define container_of(ptr, type, member) ({ \
	typeof(((type *)0)->member) *_container_of_ptr = ptr; \
	((type *)((void *)(_container_of_ptr) - offsetof(type, member)));})

#endif /* CONTAINER_OF_H */
