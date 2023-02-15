#ifndef WS_CTUBE_CONTAINER_OF_H
#define WS_CTUBE_CONTAINER_OF_H

#include "static_assert.h"

#define ws_ctube_container_of(ptr, type, member) ({ \
	typeof(((type *)0)->member) *_container_of_ptr = ptr; \
	((type *)((void *)(_container_of_ptr) - offsetof(type, member)));})

#endif /* WS_CTUBE_CONTAINER_OF_H */
