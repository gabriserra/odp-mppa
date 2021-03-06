/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp_atomic_internal.h>
#include <odp/rwlock_recursive.h>
#include <odp/thread.h>
#include <string.h>

#define NO_OWNER (-1)

void odp_rwlock_recursive_init(odp_rwlock_recursive_t *rlock)
{
	INVALIDATE(rlock);
	memset(rlock, 0, sizeof(odp_rwlock_recursive_t));
	rlock->wr_owner = NO_OWNER;
	__builtin_k1_wpurge();
	odp_rwlock_init(&rlock->lock);
}

/* Multiple readers can recurse the lock concurrently */
void odp_rwlock_recursive_read_lock(odp_rwlock_recursive_t *rlock)
{
	int thr = odp_thread_id();

	if (rlock->rd_cnt[thr]) {
		rlock->rd_cnt[thr]++;
		return;
	}

	odp_rwlock_read_lock(&rlock->lock);
	rlock->rd_cnt[thr] = 1;
}

void odp_rwlock_recursive_read_unlock(odp_rwlock_recursive_t *rlock)
{
	int thr = odp_thread_id();

	rlock->rd_cnt[thr]--;

	if (rlock->rd_cnt[thr] > 0)
		return;

	odp_rwlock_read_unlock(&rlock->lock);
}

/* Only one writer can recurse the lock */
void odp_rwlock_recursive_write_lock(odp_rwlock_recursive_t *rlock)
{
	int thr = odp_thread_id();

	if (LOAD_S32(rlock->wr_owner) == thr) {
		rlock->wr_cnt++;
		return;
	}

	odp_rwlock_write_lock(&rlock->lock);
	STORE_U32(rlock->wr_owner, thr);
	rlock->wr_cnt   = 1;
}

void odp_rwlock_recursive_write_unlock(odp_rwlock_recursive_t *rlock)
{
	rlock->wr_cnt--;

	if (rlock->wr_cnt > 0)
		return;

	STORE_S32(rlock->wr_owner, NO_OWNER);
	odp_rwlock_write_unlock(&rlock->lock);
}
