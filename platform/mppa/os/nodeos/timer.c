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

/* For snprint, POSIX timers and sigevent */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <odp_buffer_inlines.h>
#include <odp_debug_internal.h>
#include <odp/api/time.h>
#include <odp_timer_types_internal.h>


/******************************************************************************
 * POSIX timer support
 * Functions that use Linux/POSIX per-process timers and related facilities
 *****************************************************************************/

static odp_timer_pool * _odp_timer_pool_global = NULL;

static void timer_notify(union sigval sigval ODP_UNUSED)
{
	odp_timer_pool *tp = _odp_timer_pool_global;
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
	struct sigevent   sigev;
	struct itimerspec ispec;
	uint64_t res, sec, nsec;

	if(_odp_timer_pool_global != NULL){
		ODP_ABORT("Cannot have more than one timer at once");
	}
	ODP_DBG("Creating POSIX timer for timer pool %s, period %"
		PRIu64" ns\n", tp->name, tp->param.res_ns);

	memset(&sigev, 0, sizeof(sigev));
	memset(&ispec, 0, sizeof(ispec));

	_odp_timer_pool_global = tp;
	sigev.sigev_notify          = SIGEV_CALLBACK;
	sigev.sigev_notify_function = timer_notify;
	sigev.sigev_value.sival_ptr = tp;

	if (timer_create(CLOCK_MONOTONIC, &sigev, &tp->timerid))
		ODP_ABORT("timer_create() returned error %s\n",
			  strerror(errno));

	res  = tp->param.res_ns;
	sec  = res / ODP_TIME_SEC_IN_NS;
	nsec = res - sec * ODP_TIME_SEC_IN_NS;

	ispec.it_interval.tv_sec  = (time_t)sec;
	ispec.it_interval.tv_nsec = (long)nsec;
	ispec.it_value.tv_sec     = (time_t)sec;
	ispec.it_value.tv_nsec    = (long)nsec;

	if (timer_settime(tp->timerid, 0, &ispec, NULL))
		ODP_ABORT("timer_settime() returned error %s\n",
			  strerror(errno));
}

void _odp_timer_fini(odp_timer_pool *tp)
{
	if (timer_delete(tp->timerid) != 0)
		ODP_ABORT("timer_delete() returned error %s\n",
			  strerror(errno));
	if(_odp_timer_pool_global == tp)
		_odp_timer_pool_global = NULL;
}
