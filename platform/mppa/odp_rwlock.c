/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <stdbool.h>
#include <odp/api/atomic.h>
#include <odp/api/cpu.h>
#include <odp_atomic_internal.h>
#include <odp/api/rwlock.h>

void odp_rwlock_init(odp_rwlock_t *rwlock)
{
	odp_atomic_init_u32(&rwlock->cnt, 0);
}

static inline int _odp_rwlock_read_trylock(odp_rwlock_t *rwlock, uint32_t *_cnt)
{
	uint32_t cnt = *_cnt;
	/* waiting for read lock */
	if ((int32_t)cnt < 0) {
		odp_cpu_pause();
		cnt = odp_atomic_load_u32(&rwlock->cnt);
		*_cnt = cnt;
		if ((int32_t)cnt < 0)
			return 0;
	}
	return odp_atomic_cas_u32(&rwlock->cnt,
				  _cnt, cnt + 1);
}

void odp_rwlock_read_lock(odp_rwlock_t *rwlock)
{
	uint32_t cnt = -1;
	int  is_locked = 0;

	__builtin_k1_wpurge();
	while (is_locked == 0) {
		is_locked = _odp_rwlock_read_trylock(rwlock, &cnt);
	}
}

int odp_rwlock_read_trylock(odp_rwlock_t *rwlock)
{
	/* uint32_t cnt = -1; */

	/* return _odp_rwlock_read_trylock(rwlock, &cnt); */
	uint32_t zero = 0;

	return odp_atomic_cas_acq_u32(&rwlock->cnt, &zero, (uint32_t)1);
}

void odp_rwlock_read_unlock(odp_rwlock_t *rwlock)
{
	uint32_t cnt = 1;
	int  is_locked = 0;
	__k1_wmb();
	__builtin_k1_wpurge();
	while (is_locked == 0) {
		int incr = -1;
		/* waiting for read lock */
		if ((int32_t)cnt < 0) {
			incr = +1;
		}
		is_locked = odp_atomic_cas_u32(&rwlock->cnt,
				&cnt, cnt + incr);
	}
}

int odp_rwlock_write_trylock(odp_rwlock_t *rwlock)
{
	uint32_t cnt = 0;
	return odp_atomic_cas_u32(&rwlock->cnt,
				  (uint32_t*)&cnt, (uint32_t)-1);
}

void odp_rwlock_write_lock(odp_rwlock_t *rwlock)
{
	int32_t cnt = 1;
	int is_locked = 0;

	__builtin_k1_wpurge();
	while (is_locked == 0) {
		/* lock aquired, wait */
		if (cnt != 0 && cnt != -2) {
			if(cnt > 0) {
				odp_atomic_cas_u32(&rwlock->cnt,
						   (uint32_t*)&cnt,
						   (uint32_t)(-cnt - 2));
			} else {
				odp_cpu_pause();
				cnt = odp_atomic_load_u32(&rwlock->cnt);
			}
			continue;
		}
		is_locked = odp_atomic_cas_u32(&rwlock->cnt,
					       (uint32_t*)&cnt,
					       (uint32_t)-1);
	}
}

void odp_rwlock_write_unlock(odp_rwlock_t *rwlock)
{
	__k1_wmb();
	odp_atomic_store_u32(&rwlock->cnt, 0);
}
