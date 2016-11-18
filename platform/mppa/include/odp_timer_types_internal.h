/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */


/**
 * @file
 *
 * ODP timer types descriptor - implementation internal
 */

#ifndef ODP_TIMER_TYPES_INTERNAL_H_
#define ODP_TIMER_TYPES_INTERNAL_H_

#include <odp/api/align.h>
#include <odp/api/debug.h>
#include <odp_buffer_internal.h>
#include <odp_pool_internal.h>
#include <odp/api/timer.h>
#include <odp/api/spinlock.h>

/******************************************************************************
 * odp_timer abstract datatype
 *****************************************************************************/

typedef struct tick_buf_s {
	odp_atomic_u64_t exp_tck;/* Expiration tick or TMO_xxx */
	odp_buffer_t tmo_buf;/* ODP_BUFFER_INVALID if timer not active */
} tick_buf_t;

typedef struct odp_timer_s {
	void *user_ptr;
	odp_queue_t queue;/* Used for free list when timer is free */
} odp_timer;

/******************************************************************************
 * odp_timer_pool abstract datatype
 * Inludes alloc and free timer
 *****************************************************************************/

typedef struct odp_timer_pool_s {
/* Put frequently accessed fields in the first cache line */
	odp_atomic_u64_t cur_tick;/* Current tick value */
	uint64_t min_rel_tck;
	uint64_t max_rel_tck;
	tick_buf_t *tick_buf; /* Expiration tick and timeout buffer */
	odp_timer *timers; /* User pointer and queue handle (and lock) */
	odp_atomic_u32_t high_wm;/* High watermark of allocated timers */
	odp_spinlock_t itimer_running;
	odp_spinlock_t lock;
	uint32_t num_alloc;/* Current number of allocated timers */
	uint32_t first_free;/* 0..max_timers-1 => free timer */
	uint32_t tp_idx;/* Index into timer_pool array */
	odp_timer_pool_param_t param;
	char name[ODP_TIMER_POOL_NAME_LEN];
	odp_shm_t shm;
	timer_t timerid;
} odp_timer_pool;


/** OS Specific timer init */
void _odp_timer_init(odp_timer_pool *tp);
/** OS Specific timer destroy */
void _odp_timer_fini(odp_timer_pool *tp);
unsigned _odp_timer_pool_expire(odp_timer_pool_t tpid, uint64_t tick);
#endif
