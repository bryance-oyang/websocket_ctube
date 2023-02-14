/**
 * @file
 * @brief thread-safe reference counting
 */

#ifndef REF_COUNT_H
#define REF_COUNT_H

#include <pthread.h>
#include <signal.h>

#pragma GCC visibility push(hidden)

struct ref_count {
	volatile int refc;
};

static int ref_count_init(struct ref_count *ref_count)
{
	__atomic_store_n(&ref_count->refc, (int)0, __ATOMIC_SEQ_CST);
	return 0;
}

static void ref_count_destroy(struct ref_count *ref_count)
{
	__atomic_store_n(&ref_count->refc, (int)(-1), __ATOMIC_SEQ_CST);
}

#define ref_count_acquire(ptr, ref_count_member) do { \
		_Static_assert(__builtin_types_compatible_p(typeof((ptr)->ref_count_member), struct ref_count), "type mismatch in ref_count_acquire()"); \
		__atomic_add_fetch(&(ptr)->ref_count_member.refc, (int)1, __ATOMIC_SEQ_CST); \
	} while (0);

#define ref_count_release(ptr, ref_count_member, release_routine) do { \
		_Static_assert(__builtin_types_compatible_p(typeof((ptr)->ref_count_member), struct ref_count), "type mismatch in ref_count_release()"); \
		const int _ref_count_refc = __atomic_sub_fetch(&(ptr)->ref_count_member.refc, (int)1, __ATOMIC_SEQ_CST); \
		if (_ref_count_refc <= 0) { \
			if (__builtin_expect(_ref_count_refc == 0, 1)) { \
				release_routine(ptr); \
			} else { \
				raise(SIGSEGV); \
			} \
		} \
	} while (0);

#pragma GCC visibility pop

#endif /* REF_COUNT_H */
