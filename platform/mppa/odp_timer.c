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
#include <odp/api/align.h>
#include <odp_align_internal.h>
#include <odp/api/atomic.h>
#include <odp_atomic_internal.h>
#include <odp/api/buffer.h>
#include <odp_buffer_inlines.h>
#include <odp/api/cpu.h>
#include <odp/api/pool.h>
#include <odp_pool_internal.h>
#include <odp/api/debug.h>
#include <odp_debug_internal.h>
#include <odp/api/event.h>
#include <odp/api/hints.h>
#include <odp_internal.h>
#include <odp/api/queue.h>
#include <odp/api/shared_memory.h>
#include <odp/api/spinlock.h>
#include <odp/api/std_types.h>
#include <odp/api/sync.h>
#include <odp/api/time.h>
#include <odp/api/timer.h>
#include <odp_timer_internal.h>
#include <odp_timer_types_internal.h>

#define TMO_UNUSED   ((uint64_t)0x7FFFFFFFFFFFFFFF)
/* TMO_INACTIVE is or-ed with the expiration tick to indicate an expired timer.
 * The original expiration tick (63 bits) is still available so it can be used
 * for checking the freshness of received timeouts */
#define TMO_INACTIVE ((uint64_t)0x4000000000000000)

/******************************************************************************
 * Mutual exclusion in the absence of CAS16
 *****************************************************************************/

#define NUM_LOCKS 64
static _odp_atomic_flag_t locks[NUM_LOCKS]; /* Multiple locks per cache line! */
#define IDX2LOCK(idx) (&locks[(idx) % NUM_LOCKS])

#define LOCK(a)      do {			\
		INVALIDATE(a);			\
		odp_spinlock_lock(&(a)->lock);	\
	} while(0)
#define UNLOCK(a)    do {				\
		__k1_wmb();				\
		odp_spinlock_unlock(&(a)->lock);	\
	}while(0)

/******************************************************************************
 * Translation between timeout buffer and timeout header
 *****************************************************************************/

static odp_timeout_hdr_t *timeout_hdr_from_buf(odp_buffer_t buf)
{
	return (odp_timeout_hdr_t *)(void *)odp_buf_to_hdr(buf);
}

static odp_timeout_hdr_t *timeout_hdr(odp_timeout_t tmo)
{
	odp_buffer_t buf = odp_buffer_from_event(odp_timeout_to_event(tmo));
	return timeout_hdr_from_buf(buf);
}


static void timer_init(odp_timer *tim,
		tick_buf_t *tb,
		odp_queue_t _q,
		void *_up)
{
	tim->queue = _q;
	tim->user_ptr = _up;
	STORE_PTR(tb->tmo_buf, ODP_BUFFER_INVALID);
	/* All pad fields need a defined and constant value */
	/* Release the timer by setting timer state to inactive */
	_odp_atomic_u64_store_mm(&tb->exp_tck, TMO_INACTIVE, _ODP_MEMMODEL_RLS);
}

/* Teardown when timer is freed */
static void timer_fini(odp_timer *tim, tick_buf_t *tb)
{
	INVALIDATE(tb);
	ODP_ASSERT(tb->exp_tck.v == TMO_UNUSED);
	ODP_ASSERT(tb->tmo_buf == ODP_BUFFER_INVALID);
	tim->queue = ODP_QUEUE_INVALID;
	tim->user_ptr = NULL;
}

static inline uint32_t get_next_free(odp_timer *tim)
{
	/* Reusing 'queue' for next free index */
	return _odp_typeval(tim->queue);
}

static inline void set_next_free(odp_timer *tim, uint32_t nf)
{
	ODP_ASSERT(tim->queue == ODP_QUEUE_INVALID);
	/* Reusing 'queue' for next free index */
	tim->queue = _odp_cast_scalar(odp_queue_t, nf);
}

/******************************************************************************
 * odp_timer_pool abstract datatype
 * Inludes alloc and free timer
 *****************************************************************************/

#define MAX_TIMER_POOLS 255 /* Leave one for ODP_TIMER_INVALID */
#define INDEX_BITS 24
static odp_atomic_u32_t num_timer_pools;
static odp_timer_pool *timer_pool[MAX_TIMER_POOLS];

static inline odp_timer_pool *handle_to_tp(odp_timer_t hdl)
{
	uint32_t tp_idx = _odp_typeval(hdl) >> INDEX_BITS;
	if (odp_likely(tp_idx < MAX_TIMER_POOLS)) {
		odp_timer_pool *tp = timer_pool[tp_idx];
		if (odp_likely(tp != NULL))
			return timer_pool[tp_idx];
	}
	ODP_ABORT("Invalid timer handle %#x\n", hdl);
}

static inline uint32_t handle_to_idx(odp_timer_t hdl,
		struct odp_timer_pool_s *tp)
{
	uint32_t idx = _odp_typeval(hdl) & ((1U << INDEX_BITS) - 1U);
	if (odp_likely(idx < odp_atomic_load_u32(&tp->high_wm)))
		return idx;
	ODP_ABORT("Invalid timer handle %#x\n", hdl);
}

static inline odp_timer_t tp_idx_to_handle(struct odp_timer_pool_s *tp,
		uint32_t idx)
{
	ODP_ASSERT(idx < (1U << INDEX_BITS));
	return _odp_cast_scalar(odp_timer_t, (tp->tp_idx << INDEX_BITS) | idx);
}

static odp_timer_pool *odp_timer_pool_new(
	const char *_name,
	const odp_timer_pool_param_t *param)
{
	uint32_t tp_idx = odp_atomic_fetch_add_u32(&num_timer_pools, 1);
	if (odp_unlikely(tp_idx >= MAX_TIMER_POOLS)) {
		/* Restore the previous value */
		odp_atomic_sub_u32(&num_timer_pools, 1);
		__odp_errno = ENFILE; /* Table overflow */
		return NULL;
	}
	size_t sz0 = ODP_ALIGN_ROUNDUP(sizeof(odp_timer_pool),
			ODP_CACHE_LINE_SIZE);
	size_t sz1 = ODP_ALIGN_ROUNDUP(sizeof(tick_buf_t) * param->num_timers,
			ODP_CACHE_LINE_SIZE);
	size_t sz2 = ODP_ALIGN_ROUNDUP(sizeof(odp_timer) * param->num_timers,
			ODP_CACHE_LINE_SIZE);
	odp_shm_t shm = odp_shm_reserve(_name, sz0 + sz1 + sz2,
			ODP_CACHE_LINE_SIZE, ODP_SHM_SW_ONLY);
	if (odp_unlikely(shm == ODP_SHM_INVALID))
		ODP_ABORT("%s: timer pool shm-alloc(%zuKB) failed\n",
			  _name, (sz0 + sz1 + sz2) / 1024);
	odp_timer_pool *tp = (odp_timer_pool *)odp_shm_addr(shm);
	odp_atomic_init_u64(&tp->cur_tick, 0);
	snprintf(tp->name, sizeof(tp->name), "%s", _name);
	tp->shm = shm;
	tp->param = *param;
	tp->min_rel_tck = odp_timer_ns_to_tick(tp, param->min_tmo);
	tp->max_rel_tck = odp_timer_ns_to_tick(tp, param->max_tmo);
	tp->num_alloc = 0;
	odp_atomic_init_u32(&tp->high_wm, 0);
	tp->first_free = 0;
	tp->tick_buf = (void *)((char *)odp_shm_addr(shm) + sz0);
	tp->timers = (void *)((char *)odp_shm_addr(shm) + sz0 + sz1);
	/* Initialize all odp_timer entries */
	uint32_t i;
	for (i = 0; i < tp->param.num_timers; i++) {
		tp->timers[i].queue = ODP_QUEUE_INVALID;
		set_next_free(&tp->timers[i], i + 1);
		tp->timers[i].user_ptr = NULL;
		odp_atomic_init_u64(&tp->tick_buf[i].exp_tck, TMO_UNUSED);
		STORE_PTR(tp->tick_buf[i].tmo_buf, ODP_BUFFER_INVALID);
	}
	tp->tp_idx = tp_idx;
	odp_spinlock_init(&tp->lock);
	odp_spinlock_init(&tp->itimer_running);
	timer_pool[tp_idx] = tp;
	if (tp->param.clk_src == ODP_CLOCK_CPU)
		_odp_timer_init(tp);
	return tp;
}

static void odp_timer_pool_del(odp_timer_pool *tp)
{
	LOCK(tp);
	timer_pool[tp->tp_idx] = NULL;
	/* Wait for itimer thread to stop running */
	odp_spinlock_lock(&tp->itimer_running);
	if (tp->num_alloc != 0) {
		/* It's a programming error to attempt to destroy a */
		/* timer pool which is still in use */
		ODP_ABORT("%s: timers in use\n", tp->name);
	}
	if (tp->param.clk_src == ODP_CLOCK_CPU)
		_odp_timer_fini(tp);
	int rc = odp_shm_free(tp->shm);
	if (rc != 0)
		ODP_ABORT("Failed to free shared memory (%d)\n", rc);
	__k1_wmb();
}

static inline odp_timer_t timer_alloc(odp_timer_pool *tp,
				      odp_queue_t queue,
				      void *user_ptr)
{
	odp_timer_t hdl;
	LOCK(tp);
	if (odp_likely(tp->num_alloc < tp->param.num_timers)) {
		tp->num_alloc++;
		/* Remove first unused timer from free list */
		ODP_ASSERT(tp->first_free != tp->param.num_timers);
		uint32_t idx = tp->first_free;
		odp_timer *tim = &tp->timers[idx];
		tp->first_free = get_next_free(tim);
		/* Initialize timer */
		timer_init(tim, &tp->tick_buf[idx], queue, user_ptr);
		if (odp_unlikely(tp->num_alloc >
				 odp_atomic_load_u32(&tp->high_wm)))
			/* Update high_wm last with release model to
			 * ensure timer initialization is visible */
			odp_atomic_store_u32(&tp->high_wm, tp->num_alloc);
		hdl = tp_idx_to_handle(tp, idx);
	} else {
		__odp_errno = ENFILE; /* Reusing file table overflow */
		hdl = ODP_TIMER_INVALID;
	}
	UNLOCK(tp);
	return hdl;
}

static odp_buffer_t timer_cancel(odp_timer_pool *tp,
		uint32_t idx,
		uint64_t new_state);

static inline odp_buffer_t timer_free(odp_timer_pool *tp, uint32_t idx)
{
	odp_timer *tim = &tp->timers[idx];

	/* Free the timer by setting timer state to unused and
	 * grab any timeout buffer */
	odp_buffer_t old_buf = timer_cancel(tp, idx, TMO_UNUSED);

	/* Destroy timer */
	timer_fini(tim, &tp->tick_buf[idx]);

	/* Insert timer into free list */
	LOCK(tp);
	set_next_free(tim, tp->first_free);
	tp->first_free = idx;
	ODP_ASSERT(tp->num_alloc != 0);
	tp->num_alloc--;
	UNLOCK(tp);

	return old_buf;
}

/******************************************************************************
 * Operations on timers
 * expire/reset/cancel timer
 *****************************************************************************/

static bool timer_reset(uint32_t idx,
		uint64_t abs_tck,
		odp_buffer_t *tmo_buf,
		odp_timer_pool *tp)
{
	bool success = true;
	tick_buf_t *tb = &tp->tick_buf[idx];

	if (tmo_buf == NULL || *tmo_buf == ODP_BUFFER_INVALID) {
		/* Take a related lock */
		while (_odp_atomic_flag_tas(IDX2LOCK(idx)))
			/* While lock is taken, spin using relaxed loads */
			while (_odp_atomic_flag_load(IDX2LOCK(idx)))
				odp_cpu_pause();

		/* Only if there is a timeout buffer can be reset the timer */
		if (odp_likely(LOAD_PTR(tb->tmo_buf) != ODP_BUFFER_INVALID)) {
			/* Write the new expiration tick */
			_odp_atomic_u64_store_mm(&tb->exp_tck, abs_tck, _ODP_MEMMODEL_RLS);
		} else {
			/* Cannot reset a timer with neither old nor new
			 * timeout buffer */
			success = false;
		}

		/* Release the lock */
		_odp_atomic_flag_clear(IDX2LOCK(idx));
	} else {
		/* We have a new timeout buffer which replaces any old one */
		/* Fill in some (constant) header fields for timeout events */
		if (odp_event_type(odp_buffer_to_event(*tmo_buf)) ==
		    ODP_EVENT_TIMEOUT) {
			/* Convert from buffer to timeout hdr */
			odp_timeout_hdr_t *tmo_hdr =
				timeout_hdr_from_buf(*tmo_buf);
			tmo_hdr->timer = tp_idx_to_handle(tp, idx);
			tmo_hdr->user_ptr = tp->timers[idx].user_ptr;
			/* expiration field filled in when timer expires */
		}
		/* Else ignore buffers of other types */
		odp_buffer_t old_buf = ODP_BUFFER_INVALID;

		/* Take a related lock */
		while (_odp_atomic_flag_tas(IDX2LOCK(idx)))
			/* While lock is taken, spin using relaxed loads */
			while (_odp_atomic_flag_load(IDX2LOCK(idx)))
				odp_cpu_pause();

		/* Swap in new buffer, save any old buffer */
		old_buf = LOAD_PTR(tb->tmo_buf);
		STORE_PTR(tb->tmo_buf, *tmo_buf);

		/* Write the new expiration tick */
		_odp_atomic_u64_store_mm(&tb->exp_tck, abs_tck, _ODP_MEMMODEL_RLS);

		/* Release the lock */
		_odp_atomic_flag_clear(IDX2LOCK(idx));

		/* Return old timeout buffer */
		*tmo_buf = old_buf;
	}
	return success;
}

static odp_buffer_t timer_cancel(odp_timer_pool *tp,
		uint32_t idx,
		uint64_t new_state)
{
	tick_buf_t *tb = &tp->tick_buf[idx];
	odp_buffer_t old_buf;

	/* Take a related lock */
	while (_odp_atomic_flag_tas(IDX2LOCK(idx)))
		/* While lock is taken, spin using relaxed loads */
		while (_odp_atomic_flag_load(IDX2LOCK(idx)))
			odp_cpu_pause();

	/* Update the timer state (e.g. cancel the current timeout) */
	_odp_atomic_u64_store_mm(&tb->exp_tck, new_state, _ODP_MEMMODEL_RLS);

	/* Swap out the old buffer */
	old_buf = LOAD_PTR(tb->tmo_buf);
	STORE_PTR(tb->tmo_buf, ODP_BUFFER_INVALID);

	/* Release the lock */
	_odp_atomic_flag_clear(IDX2LOCK(idx));

	/* Return the old buffer */
	return old_buf;
}

static unsigned timer_expire(odp_timer_pool *tp, uint32_t idx, uint64_t tick)
{
	odp_timer *tim = &tp->timers[idx];
	tick_buf_t *tb = &tp->tick_buf[idx];
	odp_buffer_t tmo_buf = ODP_BUFFER_INVALID;
	uint64_t exp_tck;

	/* Take a related lock */
	while (_odp_atomic_flag_tas(IDX2LOCK(idx)))
		/* While lock is taken, spin using relaxed loads */
		while (_odp_atomic_flag_load(IDX2LOCK(idx)))
			odp_cpu_pause();
	/* Proper check for timer expired */
	exp_tck = odp_atomic_load_u64(&tb->exp_tck);
	if (odp_likely(exp_tck <= tick)) {
		/* Verify that there is a timeout buffer */
		tmo_buf = LOAD_PTR(tb->tmo_buf);
		if (odp_likely(tmo_buf != ODP_BUFFER_INVALID)) {
			/* Grab timeout buffer, replace with inactive timer
			 * and invalid buffer */
			STORE_PTR(tb->tmo_buf, ODP_BUFFER_INVALID);
			/* Set the inactive/expired bit keeping the expiration
			 * tick so that we can check against the expiration
			 * tick of the timeout when it is received */
			_odp_atomic_u64_store_mm(&tb->exp_tck, exp_tck | TMO_INACTIVE,  _ODP_MEMMODEL_RLS);
		}
		/* Else somehow active timer without user buffer */
	}
	/* Else false positive, ignore */
	/* Release the lock */
	_odp_atomic_flag_clear(IDX2LOCK(idx));

	if (odp_likely(tmo_buf != ODP_BUFFER_INVALID)) {
		/* Fill in expiration tick for timeout events */
		if (odp_event_type(odp_buffer_to_event(tmo_buf)) ==
		    ODP_EVENT_TIMEOUT) {
			/* Convert from buffer to timeout hdr */
			odp_timeout_hdr_t *tmo_hdr =
				timeout_hdr_from_buf(tmo_buf);
			tmo_hdr->expiration = exp_tck;
			/* timer and user_ptr fields filled in when timer
			 * was set */
		}
		/* Else ignore events of other types */
		/* Post the timeout to the destination queue */
		int rc = odp_queue_enq(tim->queue,
				       odp_buffer_to_event(tmo_buf));
		if (odp_unlikely(rc != 0)) {
			odp_buffer_free(tmo_buf);
			ODP_ABORT("Failed to enqueue timeout buffer (%d)\n",
				  rc);
		}
		return 1;
	} else {
		/* Else false positive, ignore */
		return 0;
	}
}

unsigned _odp_timer_pool_expire(odp_timer_pool_t tpid, uint64_t tick)
{
	tick_buf_t *array = &tpid->tick_buf[0];
	uint32_t high_wm = odp_atomic_load_u32(&tpid->high_wm);
	unsigned nexp = 0;
	uint32_t i;

	ODP_ASSERT(high_wm <= tpid->param.num_timers);
	for (i = 0; i < high_wm;) {
		uint64_t exp_tck = odp_atomic_load_u64(&array[i++].exp_tck);
		if (odp_unlikely(exp_tck <= tick)) {
			/* Attempt to expire timer */
			nexp += timer_expire(tpid, i - 1, tick);
		}
	}
	return nexp;
}


/******************************************************************************
 * Public API functions
 * Some parameter checks and error messages
 * No modificatios of internal state
 *****************************************************************************/
odp_timer_pool_t
odp_timer_pool_create(const char *name,
		      const odp_timer_pool_param_t *param)
{
	/* Verify that buffer pool can be used for timeouts */
	/* Verify that we have a valid (non-zero) timer resolution */
	if (param->res_ns == 0) {
		__odp_errno = EINVAL;
		return NULL;
	}
	odp_timer_pool_t tp = odp_timer_pool_new(name, param);
	return tp;
}

void odp_timer_pool_start(void)
{
	/* Nothing to do here, timer pools are started by the create call */
}

void odp_timer_pool_destroy(odp_timer_pool_t tpid)
{
	odp_timer_pool_del(tpid);
}

uint64_t odp_timer_tick_to_ns(odp_timer_pool_t tpid, uint64_t ticks)
{
	return ticks * tpid->param.res_ns;
}

uint64_t odp_timer_ns_to_tick(odp_timer_pool_t tpid, uint64_t ns)
{
	return (uint64_t)(ns / tpid->param.res_ns);
}

uint64_t odp_timer_current_tick(odp_timer_pool_t tpid)
{
	/* Relaxed atomic read for lowest overhead */
	return odp_atomic_load_u64(&tpid->cur_tick);
}

int odp_timer_pool_info(odp_timer_pool_t tpid,
			odp_timer_pool_info_t *buf)
{
	buf->param = tpid->param;
	buf->cur_timers = tpid->num_alloc;
	buf->hwm_timers = odp_atomic_load_u32(&tpid->high_wm);
	buf->name = tpid->name;
	return 0;
}

uint64_t odp_timer_pool_to_u64(odp_timer_pool_t tpid)
{
	return _odp_pri(tpid);
}

odp_timer_t odp_timer_alloc(odp_timer_pool_t tpid,
			    odp_queue_t queue,
			    void *user_ptr)
{
	if (odp_unlikely(queue == ODP_QUEUE_INVALID))
		ODP_ABORT("%s: Invalid queue handle\n", tpid->name);
	/* We don't care about the validity of user_ptr because we will not
	 * attempt to dereference it */
	odp_timer_t hdl = timer_alloc(tpid, queue, user_ptr);
	if (odp_likely(hdl != ODP_TIMER_INVALID)) {
		/* Success */
		return hdl;
	}
	/* errno set by timer_alloc() */
	return ODP_TIMER_INVALID;
}

odp_event_t odp_timer_free(odp_timer_t hdl)
{
	odp_timer_pool *tp = handle_to_tp(hdl);
	uint32_t idx = handle_to_idx(hdl, tp);
	odp_buffer_t old_buf = timer_free(tp, idx);
	return odp_buffer_to_event(old_buf);
}

int odp_timer_set_abs(odp_timer_t hdl,
		      uint64_t abs_tck,
		      odp_event_t *tmo_ev)
{
	odp_timer_pool *tp = handle_to_tp(hdl);
	uint32_t idx = handle_to_idx(hdl, tp);
	uint64_t cur_tick = odp_atomic_load_u64(&tp->cur_tick);
	if (odp_unlikely(abs_tck < cur_tick + tp->min_rel_tck))
		return ODP_TIMER_TOOEARLY;
	if (odp_unlikely(abs_tck > cur_tick + tp->max_rel_tck))
		return ODP_TIMER_TOOLATE;
	if (timer_reset(idx, abs_tck, (odp_buffer_t *)tmo_ev, tp))
		return ODP_TIMER_SUCCESS;
	else
		return ODP_TIMER_NOEVENT;
}

int odp_timer_set_rel(odp_timer_t hdl,
		      uint64_t rel_tck,
		      odp_event_t *tmo_ev)
{
	odp_timer_pool *tp = handle_to_tp(hdl);
	uint32_t idx = handle_to_idx(hdl, tp);
	uint64_t abs_tck = odp_atomic_load_u64(&tp->cur_tick) + rel_tck;
	if (odp_unlikely(rel_tck < tp->min_rel_tck))
		return ODP_TIMER_TOOEARLY;
	if (odp_unlikely(rel_tck > tp->max_rel_tck))
		return ODP_TIMER_TOOLATE;
	if (timer_reset(idx, abs_tck, (odp_buffer_t *)tmo_ev, tp))
		return ODP_TIMER_SUCCESS;
	else
		return ODP_TIMER_NOEVENT;
}

int odp_timer_cancel(odp_timer_t hdl, odp_event_t *tmo_ev)
{
	odp_timer_pool *tp = handle_to_tp(hdl);
	uint32_t idx = handle_to_idx(hdl, tp);
	/* Set the expiration tick of the timer to TMO_INACTIVE */
	odp_buffer_t old_buf = timer_cancel(tp, idx, TMO_INACTIVE);
	if (old_buf != ODP_BUFFER_INVALID) {
		*tmo_ev = odp_buffer_to_event(old_buf);
		return 0; /* Active timer cancelled, timeout returned */
	} else {
		return -1; /* Timer already expired, no timeout returned */
	}
}

uint64_t odp_timer_to_u64(odp_timer_t hdl)
{
	return _odp_pri(hdl);
}

odp_timeout_t odp_timeout_from_event(odp_event_t ev)
{
	/* This check not mandated by the API specification */
	if (odp_event_type(ev) != ODP_EVENT_TIMEOUT)
		ODP_ABORT("Event not a timeout");
	return (odp_timeout_t)ev;
}

odp_event_t odp_timeout_to_event(odp_timeout_t tmo)
{
	return (odp_event_t)tmo;
}

uint64_t odp_timeout_to_u64(odp_timeout_t tmo)
{
	return _odp_pri(tmo);
}

int odp_timeout_fresh(odp_timeout_t tmo)
{
	const odp_timeout_hdr_t *hdr = timeout_hdr(tmo);
	odp_timer_t hdl = hdr->timer;
	odp_timer_pool *tp = handle_to_tp(hdl);
	uint32_t idx = handle_to_idx(hdl, tp);
	tick_buf_t *tb = &tp->tick_buf[idx];
	uint64_t exp_tck = odp_atomic_load_u64(&tb->exp_tck);
	/* Return true if the timer still has the same expiration tick
	 * (ignoring the inactive/expired bit) as the timeout */
	return hdr->expiration == (exp_tck & ~TMO_INACTIVE);
}

odp_timer_t odp_timeout_timer(odp_timeout_t tmo)
{
	return timeout_hdr(tmo)->timer;
}

uint64_t odp_timeout_tick(odp_timeout_t tmo)
{
	return timeout_hdr(tmo)->expiration;
}

void *odp_timeout_user_ptr(odp_timeout_t tmo)
{
	return timeout_hdr(tmo)->user_ptr;
}

odp_timeout_t odp_timeout_alloc(odp_pool_t pool)
{
	odp_buffer_t buf = odp_buffer_alloc(pool);
	if (odp_unlikely(buf == ODP_BUFFER_INVALID))
		return ODP_TIMEOUT_INVALID;
	return odp_timeout_from_event(odp_buffer_to_event(buf));
}

void odp_timeout_free(odp_timeout_t tmo)
{
	odp_event_t ev = odp_timeout_to_event(tmo);
	odp_buffer_free(odp_buffer_from_event(ev));
}

int odp_timer_init_global(void)
{
	uint32_t i;
	for (i = 0; i < NUM_LOCKS; i++)
		_odp_atomic_flag_clear(&locks[i]);
	odp_atomic_init_u32(&num_timer_pools, 0);
	return 0;
}
