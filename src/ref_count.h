/**
 * @file
 * @brief thread-safe reference counting
 */

#ifndef WS_CTUBE_REF_COUNT_H
#define WS_CTUBE_REF_COUNT_H

#include <pthread.h>
#include <signal.h>

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

#endif /* WS_CTUBE_REF_COUNT_H */
