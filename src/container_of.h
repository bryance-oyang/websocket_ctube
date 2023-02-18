#ifndef WS_CTUBE_CONTAINER_OF_H
#define WS_CTUBE_CONTAINER_OF_H

/**
 * get the container of ptr which is a member of a struct
 *
 * Example:
 *
 * struct a {
 * 	struct b bmember;
 * 	struct c cmember;
 * };
 *
 * struct a *a_ptr;
 * struct c *c_ptr = &a_ptr->cmember;
 *
 * assert(a_ptr == ws_ctube_container_of(c_ptr, struct a, cmember))
 */
#define ws_ctube_container_of(ptr, type, member) ({ \
	typeof(((type *)0)->member) *_ws_ctube_container_of_ptr = (ptr); \
	((type *)((char *)(_ws_ctube_container_of_ptr) - offsetof(type, member)));})

#endif /* WS_CTUBE_CONTAINER_OF_H */
