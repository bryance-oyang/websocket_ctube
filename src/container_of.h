/**
 * Copyright (c) 2023 Bryance Oyang
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

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
 * a_ptr == ws_ctube_container_of(c_ptr, struct a, cmember) // true
 */
#define ws_ctube_container_of(ptr, type, member) ({ \
	typeof(((type *)0)->member) *_container_of_ptr = ptr; \
	((type *)((char *)(_container_of_ptr) - offsetof(type, member)));})

#endif /* WS_CTUBE_CONTAINER_OF_H */
