/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP timer service
 *
 */

#include <vbsp.h>
#include <utask.h>

#include <odp_buffer_inlines.h>
#include <odp_debug_internal.h>
#include <odp/api/time.h>
#include <odp_timer_types_internal.h>


/******************************************************************************
 * mOS timer support
 * Functions that use MPPA per-process timers and related facilities
 *****************************************************************************/
static odp_timer_pool * _odp_timer_pool_global[BSP_NB_PE_MAX]= { NULL };

static void timer_notify (int timer_id ODP_UNUSED)
{
	odp_timer_pool * tp = _odp_timer_pool_global[__k1_get_cpu_id()];
	uint64_t prev_tick = odp_atomic_fetch_inc_u64(&tp->cur_tick);
	/* Attempt to acquire the lock, check if the old value was clear */
	if (odp_spinlock_trylock(&tp->itimer_running)) {
		/* Scan timer array, looking for timers to expire */
		(void)_odp_timer_pool_expire(tp, prev_tick);
		odp_spinlock_unlock(&tp->itimer_running);
	}
	/* Else skip scan of timers. cur_tick was updated and next itimer
	 * invocation will process older expiration ticks as well */
}
void _odp_timer_init(odp_timer_pool *tp)
{
	int pid = __k1_get_cpu_id();
	if(_odp_timer_pool_global[pid] != NULL){
		ODP_ABORT("Cannot have more than one timer at once");
	}
	_odp_timer_pool_global[pid] = tp;
	utask_timer64_create(timer_notify);

	uint64_t res  = tp->param.res_ns;
	odp_time_t time = odp_time_local_from_ns(res);
	utask_timer64_set_time(time.cycles);
	return;
}

void _odp_timer_fini(odp_timer_pool *tp)
{
	int pid = __k1_get_cpu_id();
	/* FIXME: Disable for real */
	utask_timer64_set_time(1ULL<< 62);
	if(_odp_timer_pool_global[pid] == tp){
		_odp_timer_pool_global[pid] = NULL;
	}
	return;
}
