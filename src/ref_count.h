/**
 * @file
 * @brief thread-safe reference counting
 *
 * initialized with a ref count of 0
 *
 * acquire with ws_ctube_ref_count_acquire
 *
 * release with ws_ctube_ref_count_release and provide the routine that can free
 * the object if the ref count goes to 0
 */

#ifndef WS_CTUBE_REF_COUNT_H
#define WS_CTUBE_REF_COUNT_H

#include <pthread.h>
#include <signal.h>

/** including this in a larger struct allows it to be reference counted */
struct ws_ctube_ref_count {
	volatile int refc;
};

static int ws_ctube_ref_count_init(struct ws_ctube_ref_count *ref_count)
{
	__atomic_store_n(&ref_count->refc, (int)0, __ATOMIC_SEQ_CST);
	return 0;
}

static void ws_ctube_ref_count_destroy(struct ws_ctube_ref_count *ref_count)
{
	__atomic_store_n(&ref_count->refc, (int)(-1), __ATOMIC_SEQ_CST);
}

#ifndef __cplusplus
#define ws_ctube_ref_count_acquire(ptr, ref_count_member) do { \
		_Static_assert(__builtin_types_compatible_p(typeof((ptr)->ref_count_member), struct ws_ctube_ref_count), "type mismatch in ws_ctube_ref_count_acquire()"); \
		__atomic_add_fetch(&(ptr)->ref_count_member.refc, (int)1, __ATOMIC_SEQ_CST); \
	} while (0);

#define ws_ctube_ref_count_release(ptr, ref_count_member, release_routine) do { \
		_Static_assert(__builtin_types_compatible_p(typeof((ptr)->ref_count_member), struct ws_ctube_ref_count), "type mismatch in ws_ctube_ref_count_release()"); \
		const int _ref_count_refc = __atomic_sub_fetch(&(ptr)->ref_count_member.refc, (int)1, __ATOMIC_SEQ_CST); \
		if (_ref_count_refc <= 0) { \
			if (__builtin_expect(_ref_count_refc == 0, 1)) { \
				release_routine(ptr); \
			} else { \
				raise(SIGSEGV); \
			} \
		} \
	} while (0);
#else /* __cplusplus */
#define ws_ctube_ref_count_acquire(ptr, ref_count_member) do { \
		__atomic_add_fetch(&(ptr)->ref_count_member.refc, (int)1, __ATOMIC_SEQ_CST); \
	} while (0);

#define ws_ctube_ref_count_release(ptr, ref_count_member, release_routine) do { \
		const int _ref_count_refc = __atomic_sub_fetch(&(ptr)->ref_count_member.refc, (int)1, __ATOMIC_SEQ_CST); \
		if (_ref_count_refc <= 0) { \
			if (__builtin_expect(_ref_count_refc == 0, 1)) { \
				release_routine(ptr); \
			} else { \
				raise(SIGSEGV); \
			} \
		} \
	} while (0);
#endif /* __cplusplus */

#endif /* WS_CTUBE_REF_COUNT_H */
