#ifndef CONTAINER_OF_H
#define CONTAINER_OF_H

#include "static_assert.h"

#pragma GCC visibility push(hidden)

#define container_of(ptr, type, member) ({ \
	typeof(((type *)0)->member) *_container_of_ptr = ptr; \
	((type *)((void *)(_container_of_ptr) - offsetof(type, member)));})

#pragma GCC visibility pop

#endif /* CONTAINER_OF_H */
