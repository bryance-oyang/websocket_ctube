/** shamelessly copied from linux kernel source code */

#ifndef CONTAINER_OF_H
#define CONTAINER_OF_H

#include "static_assert.h"

#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 * WARNING: any const qualifier of @ptr is lost.
 */
#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	_Static_assert(__same_type(*(ptr), ((type *)0)->member) ||	\
		      __same_type(*(ptr), void),			\
		      "pointer type mismatch in container_of()");	\
	((type *)(__mptr - offsetof(type, member))); })

#endif /* CONTAINER_OF_H */
