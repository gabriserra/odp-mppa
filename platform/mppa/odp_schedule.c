/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <string.h>
#include <odp/schedule.h>
#include <odp_schedule_internal.h>
#include <odp/align.h>
#include <odp/queue.h>
#include <odp/shared_memory.h>
#include <odp/buffer.h>
#include <odp/pool.h>
#include <odp_internal.h>
#include <odp/config.h>
#include <odp_debug_internal.h>
#include <odp/thread.h>
#include <odp/time.h>
#include <odp/spinlock.h>
#include <odp/hints.h>

#include <odp_queue_internal.h>
#include <odp_packet_io_internal.h>

odp_thrmask_t sched_mask_all;

/* Number of schedule commands.
 * One per scheduled queue and packet interface */
#define NUM_SCHED_CMD (ODP_CONFIG_QUEUES + ODP_CONFIG_PKTIO_ENTRIES)

/* Maximum number of dequeues */
#define MAX_DEQ 4


/* Internal: Start of named groups in group mask arrays */
#define _ODP_SCHED_GROUP_NAMED (ODP_SCHED_GROUP_CONTROL + 1)

typedef struct {
	odp_queue_t    pri_queue[ODP_CONFIG_SCHED_PRIOS];
	odp_spinlock_t mask_lock;
	odp_pool_t     pool;
	odp_shm_t      shm;
	uint32_t       pri_count[ODP_CONFIG_SCHED_PRIOS];
	odp_spinlock_t grp_lock;
	struct {
		char           name[ODP_SCHED_GROUP_NAME_LEN];
		odp_thrmask_t *mask;
	} sched_grp[ODP_CONFIG_SCHED_GRPS];
} sched_t;

/* Schedule command */
typedef struct {
	int           cmd;

	union {
		queue_entry_t *qe;

		struct {
			pktio_entry_t *pe;
			int           prio;
		};
	};
} sched_cmd_t;

#define SCHED_CMD_DEQUEUE    0
#define SCHED_CMD_POLL_PKTIN 1


typedef struct {
	odp_queue_t pri_queue;
	odp_event_t cmd_ev;

	odp_buffer_hdr_t *buf_hdr[MAX_DEQ];
	queue_entry_t *qe;
	queue_entry_t *origin_qe;
	uint64_t order;
	uint64_t sync[ODP_CONFIG_MAX_ORDERED_LOCKS_PER_QUEUE];
	odp_pool_t pool;
	int enq_called;
	uint32_t num;
	uint32_t index;
	uint32_t pause;
	int ignore_ordered_context;
} sched_local_t;

/* Global scheduler context */
static sched_t *sched;

/* Thread local scheduler context */
static __thread sched_local_t sched_local;

/* Internal routine to get scheduler thread mask addrs */
odp_thrmask_t *thread_sched_grp_mask(int index);

static void sched_local_init(void)
{
	memset(&sched_local, 0, sizeof(sched_local_t));

	sched_local.pri_queue = ODP_QUEUE_INVALID;
	sched_local.cmd_ev    = ODP_EVENT_INVALID;
}

int odp_schedule_init_global(void)
{
	odp_shm_t shm;
	odp_pool_t pool;
	int i;
	odp_pool_param_t params;

	ODP_DBG("Schedule init ... ");

	shm = odp_shm_reserve("odp_scheduler",
			      sizeof(sched_t),
			      ODP_CACHE_LINE_SIZE, 0);

	sched = odp_shm_addr(shm);

	if (sched == NULL) {
		ODP_ERR("Schedule init: Shm reserve failed.\n");
		return -1;
	}

	memset(sched, 0, sizeof(sched_t));

	odp_pool_param_init(&params);
	params.buf.size  = sizeof(sched_cmd_t);
	params.buf.align = 0;
	params.buf.num   = NUM_SCHED_CMD;
	params.type      = ODP_POOL_BUFFER;

	pool = odp_pool_create("odp_sched_pool", &params);

	if (pool == ODP_POOL_INVALID) {
		ODP_ERR("Schedule init: Pool create failed.\n");
		return -1;
	}

	sched->pool = pool;
	sched->shm  = shm;
	odp_spinlock_init(&sched->mask_lock);

	for (i = 0; i < ODP_CONFIG_SCHED_PRIOS; i++) {
		odp_queue_t queue;
		char name[] = "odp_priXX";

		name[7] = '0' + i / 10;
		name[8] = '0' + i - 10*(i / 10);

		queue = odp_queue_create(name, ODP_QUEUE_TYPE_POLL, NULL);

		if (queue == ODP_QUEUE_INVALID) {
			ODP_ERR("Sched init: Queue create failed.\n");
			return -1;
		}

		sched->pri_queue[i] = queue;
		sched->pri_count[i] = 0;
	}

	odp_spinlock_init(&sched->grp_lock);

	for (i = 0; i < ODP_CONFIG_SCHED_GRPS; i++) {
		memset(sched->sched_grp[i].name, 0, ODP_SCHED_GROUP_NAME_LEN);
		sched->sched_grp[i].mask = thread_sched_grp_mask(i);
	}

	odp_thrmask_setall(&sched_mask_all);

	ODP_DBG("done\n");

	return 0;
}

int odp_schedule_term_global(void)
{
	int ret = 0;
	int rc = 0;
	int i;

	for (i = 0; i < ODP_CONFIG_SCHED_PRIOS; i++) {
		odp_queue_t  pri_q;
		odp_event_t  ev;

		pri_q = sched->pri_queue[i];

		while ((ev = odp_queue_deq(pri_q)) !=
		       ODP_EVENT_INVALID) {
			odp_buffer_t buf;
			sched_cmd_t *sched_cmd;

			buf = odp_buffer_from_event(ev);
			sched_cmd = odp_buffer_addr(buf);

			if (sched_cmd->cmd == SCHED_CMD_DEQUEUE) {
				queue_entry_t *qe;
				odp_buffer_hdr_t *buf_hdr[1];
				int num;

				qe  = sched_cmd->qe;
				num = queue_deq_multi(qe, buf_hdr, 1);

				if (num < 0)
					queue_destroy_finalize(qe);

				if (num > 0)
					ODP_ERR("Queue not empty\n");
			} else
				odp_buffer_free(buf);
		}

		if (odp_queue_destroy(pri_q)) {
			ODP_ERR("Pri queue destroy fail.\n");
			rc = -1;
		}
	}

	if (odp_pool_destroy(sched->pool) != 0) {
		ODP_ERR("Pool destroy fail.\n");
		rc = -1;
	}

	ret = odp_shm_free(sched->shm);
	if (ret < 0) {
		ODP_ERR("Shm free failed for odp_scheduler");
		rc = -1;
	}

	return rc;
}

int odp_schedule_init_local(void)
{
	sched_local_init();
	return 0;
}

int odp_schedule_term_local(void)
{
	if (sched_local.num) {
		ODP_ERR("Locally pre-scheduled events exist.\n");
		return -1;
	}

	odp_schedule_release_context();

	sched_local_init();
	return 0;
}

static odp_queue_t pri_set(int prio)
{
	odp_spinlock_lock(&sched->mask_lock);
	int count = LOAD_U32(sched->pri_count[prio]);
	STORE_U32(sched->pri_count[prio], count + 1);
	odp_spinlock_unlock(&sched->mask_lock);

	return sched->pri_queue[prio];
}

static void pri_clr(int prio)
{
	odp_spinlock_lock(&sched->mask_lock);

	/* Clear mask bit when last queue is removed*/
	int count = LOAD_U32(sched->pri_count[prio]);
	STORE_U32(sched->pri_count[prio], count - 1);

	odp_spinlock_unlock(&sched->mask_lock);
}

int schedule_queue_init(queue_entry_t *qe)
{
	odp_buffer_t buf;
	sched_cmd_t *sched_cmd;

	buf = odp_buffer_alloc(sched->pool);

	if (buf == ODP_BUFFER_INVALID)
		return -1;

	sched_cmd      = odp_buffer_addr(buf);
	sched_cmd->cmd = SCHED_CMD_DEQUEUE;
	sched_cmd->qe  = qe;

	qe->s.cmd_ev    = odp_buffer_to_event(buf);
	qe->s.pri_queue = pri_set(queue_prio(qe));

	return 0;
}

void schedule_queue_destroy(queue_entry_t *qe)
{
	odp_event_free(qe->s.cmd_ev);

	pri_clr(queue_prio(qe));

	qe->s.cmd_ev    = ODP_EVENT_INVALID;
	qe->s.pri_queue = ODP_QUEUE_INVALID;
}

int schedule_pktio_start(odp_pktio_t pktio, int prio)
{
	odp_buffer_t buf;
	sched_cmd_t *sched_cmd;
	odp_queue_t pri_queue;

	buf = odp_buffer_alloc(sched->pool);

	if (buf == ODP_BUFFER_INVALID)
		return -1;

	sched_cmd        = odp_buffer_addr(buf);
	sched_cmd->cmd   = SCHED_CMD_POLL_PKTIN;
	sched_cmd->pe    = get_pktio_entry(pktio);
	sched_cmd->prio  = prio;

	pri_queue  = pri_set(prio);

	if (odp_queue_enq(pri_queue, odp_buffer_to_event(buf)))
		ODP_ABORT("schedule_pktio_start failed\n");


	return 0;
}

void odp_schedule_release_atomic(void)
{
	if (sched_local.pri_queue != ODP_QUEUE_INVALID &&
	    sched_local.num       == 0) {
		/* Release current atomic queue */
		if (odp_queue_enq(sched_local.pri_queue, sched_local.cmd_ev))
			ODP_ABORT("odp_schedule_release_atomic failed\n");
		sched_local.pri_queue = ODP_QUEUE_INVALID;
	}
}

void odp_schedule_release_ordered(void)
{
	if (sched_local.origin_qe) {
		int rc = release_order(sched_local.origin_qe,
				       sched_local.order,
				       sched_local.pool,
				       sched_local.enq_called);
		if (rc == 0)
			sched_local.origin_qe = NULL;
	}
}

void odp_schedule_release_context(void)
{
	if (sched_local.origin_qe) {
		release_order(sched_local.origin_qe, sched_local.order,
			      sched_local.pool, sched_local.enq_called);
		sched_local.origin_qe = NULL;
	} else
		odp_schedule_release_atomic();
}

static inline int copy_events(odp_event_t out_ev[], unsigned int max)
{
	int num = max > sched_local.num ? sched_local.num : max;
	memcpy(out_ev, sched_local.buf_hdr + sched_local.index, num * sizeof(*out_ev));
	sched_local.index += num;
	sched_local.num -= num;
	return num;
}

/*
 * Schedule queues
 */
static int schedule(odp_queue_t *out_queue, odp_event_t out_ev[],
		    unsigned int max_num, unsigned int max_deq)
{
	int i;
	int thr;
	int ret;
	uint32_t k;

	if (sched_local.num) {
		ret = copy_events(out_ev, max_num);

		if (out_queue)
			*out_queue = queue_handle(sched_local.qe);

		return ret;
	}

	odp_schedule_release_context();

	if (odp_unlikely(sched_local.pause))
		return 0;

	thr = odp_thread_id();

	for (i = 0; i < ODP_CONFIG_SCHED_PRIOS; i++) {

		if (LOAD_U32(sched->pri_count[i]) == 0)
			continue;

		odp_queue_t  pri_q;
		odp_event_t  ev;
		odp_buffer_t buf;
		sched_cmd_t *sched_cmd;
		queue_entry_t *qe;
		int num;
		int qe_grp;

		pri_q = LOAD_PTR(sched->pri_queue[i]);
		ev    = odp_queue_deq(pri_q);

		if (ev == ODP_EVENT_INVALID)
			continue;

		buf   = odp_buffer_from_event(ev);
		sched_cmd = odp_buffer_addr(buf);
		INVALIDATE(sched_cmd);

		if (sched_cmd->cmd == SCHED_CMD_POLL_PKTIN) {
			/* Poll packet input */
			if (pktin_poll(sched_cmd->pe)) {
				/* Stop scheduling the pktio */
				pri_clr(sched_cmd->prio);
				odp_buffer_free(buf);
			} else {
				/* Continue scheduling the pktio */
				if (odp_queue_enq(pri_q, ev))
					ODP_ABORT("schedule failed\n");
			}

			continue;
		}

		qe  = sched_cmd->qe;
		qe_grp = qe->s.param.sched.group;

		if (qe_grp > ODP_SCHED_GROUP_ALL) {
			const odp_thrmask_t *mask =
				LOAD_PTR(sched->sched_grp[qe_grp].mask);

			if(!odp_thrmask_isset(mask, thr)) {
				/* This thread is not eligible for work from
				 * this queue, so continue scheduling it.
				 */
				if (odp_queue_enq(pri_q, ev))
					ODP_ABORT("schedule failed\n");
				continue;
			}
		}

		/* For ordered queues we want consecutive events to
		 * be dispatched to separate threads, so do not cache
		 * them locally.
		 */
		if (queue_is_ordered(qe))
			max_deq = 1;
		num = queue_deq_multi(qe, sched_local.buf_hdr, max_deq);

		if (num < 0) {
			/* Destroyed queue */
			queue_destroy_finalize(qe);
			continue;
		}

		if (num == 0) {
			/* Remove empty queue from scheduling */
			continue;
		}

		sched_local.num   = num;
		sched_local.index = 0;
		sched_local.qe    = qe;
		ret = copy_events(out_ev, max_num);

		if (queue_is_ordered(qe)) {
			/* Continue scheduling ordered queues */
			if (odp_queue_enq(pri_q, ev))
				ODP_ABORT("schedule failed\n");
			/* Cache order info about this event */
			sched_local.origin_qe = qe;
			sched_local.order =
				sched_local.buf_hdr[0]->order;
			sched_local.pool =
				sched_local.buf_hdr[0]->pool_hdl;
			for (k = 0; k < qe->s.param.sched.lock_count; k++) {
				sched_local.sync[k] =
					sched_local.buf_hdr[0]->sync[k];
			}
			sched_local.enq_called = 0;
		} else if (queue_is_atomic(qe)) {
			/* Hold queue during atomic access */
			sched_local.pri_queue = pri_q;
			sched_local.cmd_ev    = ev;
		} else {
			/* Continue scheduling the queue */
			if (odp_queue_enq(pri_q, ev))
				ODP_ABORT("schedule failed\n");
		}

		/* Output the source queue handle */
		if (out_queue)
			*out_queue = queue_handle(qe);

		return ret;
	}

	return 0;
}


static int schedule_loop(odp_queue_t *out_queue, uint64_t wait,
			 odp_event_t out_ev[],
			 unsigned int max_num, unsigned int max_deq)
{
	odp_time_t next, wtime;
	int first = 1;
	int ret;

	while (1) {
		ret = schedule(out_queue, out_ev, max_num, max_deq);

		if (ret)
			break;

		if (wait == ODP_SCHED_WAIT)
			continue;

		if (wait == ODP_SCHED_NO_WAIT)
			break;

		if (!first) {
			wtime = odp_time_local_from_ns(wait);
			next = odp_time_sum(odp_time_local(), wtime);
			first = 0;
			continue;
		}

		if (odp_time_cmp(next, odp_time_local()) < 0)
			break;
	}

	return ret;
}


odp_event_t odp_schedule(odp_queue_t *out_queue, uint64_t wait)
{
	odp_event_t ev;

	ev = ODP_EVENT_INVALID;

	schedule_loop(out_queue, wait, &ev, 1, MAX_DEQ);

	return ev;
}


int odp_schedule_multi(odp_queue_t *out_queue, uint64_t wait,
		       odp_event_t events[], int num)
{
	return schedule_loop(out_queue, wait, events, num, MAX_DEQ);
}


void odp_schedule_pause(void)
{
	sched_local.pause = 1;
}


void odp_schedule_resume(void)
{
	sched_local.pause = 0;
}


uint64_t odp_schedule_wait_time(uint64_t ns)
{
	return ns;
}


int odp_schedule_num_prio(void)
{
	return ODP_CONFIG_SCHED_PRIOS;
}

odp_schedule_group_t odp_schedule_group_create(const char *name,
					       const odp_thrmask_t *mask)
{
	odp_schedule_group_t group = ODP_SCHED_GROUP_INVALID;
	int i;

	odp_spinlock_lock(&sched->grp_lock);
	INVALIDATE(sched);

	for (i = _ODP_SCHED_GROUP_NAMED; i < ODP_CONFIG_SCHED_GRPS; i++) {
		if (sched->sched_grp[i].name[0] == 0) {
			strncpy(sched->sched_grp[i].name, name,
				ODP_SCHED_GROUP_NAME_LEN - 1);
			odp_thrmask_copy(sched->sched_grp[i].mask, mask);
			group = (odp_schedule_group_t)i;
			break;
		}
	}

	odp_spinlock_unlock(&sched->grp_lock);
	return group;
}

int odp_schedule_group_destroy(odp_schedule_group_t group)
{
	int ret;

	odp_spinlock_lock(&sched->grp_lock);
	INVALIDATE(sched);

	if (group < ODP_CONFIG_SCHED_GRPS &&
	    group >= _ODP_SCHED_GROUP_NAMED &&
	    sched->sched_grp[group].name[0] != 0) {
		odp_thrmask_zero(sched->sched_grp[group].mask);
		memset(sched->sched_grp[group].name, 0,
		       ODP_SCHED_GROUP_NAME_LEN);
		ret = 0;
	} else {
		ret = -1;
	}

	odp_spinlock_unlock(&sched->grp_lock);
	return ret;
}

odp_schedule_group_t odp_schedule_group_lookup(const char *name)
{
	odp_schedule_group_t group = ODP_SCHED_GROUP_INVALID;
	int i;

	odp_spinlock_lock(&sched->grp_lock);
	INVALIDATE(sched);

	for (i = _ODP_SCHED_GROUP_NAMED; i < ODP_CONFIG_SCHED_GRPS; i++) {
		if (strcmp(name, sched->sched_grp[i].name) == 0) {
			group = (odp_schedule_group_t)i;
			break;
		}
	}

	odp_spinlock_unlock(&sched->grp_lock);
	return group;
}

int odp_schedule_group_join(odp_schedule_group_t group,
			    const odp_thrmask_t *mask)
{
	int ret;

	odp_spinlock_lock(&sched->grp_lock);
	INVALIDATE(sched);

	if (group < ODP_CONFIG_SCHED_GRPS &&
	    group >= _ODP_SCHED_GROUP_NAMED &&
	    sched->sched_grp[group].name[0] != 0) {
		odp_thrmask_or(sched->sched_grp[group].mask,
			       sched->sched_grp[group].mask,
			       mask);
		ret = 0;
	} else {
		ret = -1;
	}

	odp_spinlock_unlock(&sched->grp_lock);
	return ret;
}

int odp_schedule_group_leave(odp_schedule_group_t group,
			     const odp_thrmask_t *mask)
{
	int ret;

	odp_spinlock_lock(&sched->grp_lock);
	INVALIDATE(sched);

	if (group < ODP_CONFIG_SCHED_GRPS &&
	    group >= _ODP_SCHED_GROUP_NAMED &&
	    sched->sched_grp[group].name[0] != 0) {
		odp_thrmask_t leavemask;

		odp_thrmask_xor(&leavemask, mask, &sched_mask_all);
		odp_thrmask_and(sched->sched_grp[group].mask,
				sched->sched_grp[group].mask,
				&leavemask);
		ret = 0;
	} else {
		ret = -1;
	}

	odp_spinlock_unlock(&sched->grp_lock);
	return ret;
}

int odp_schedule_group_thrmask(odp_schedule_group_t group,
			       odp_thrmask_t *thrmask)
{
	int ret;

	odp_spinlock_lock(&sched->grp_lock);
	INVALIDATE(sched);

	if (group < ODP_CONFIG_SCHED_GRPS &&
	    group >= _ODP_SCHED_GROUP_NAMED &&
	    sched->sched_grp[group].name[0] != 0) {
		*thrmask = *sched->sched_grp[group].mask;
		ret = 0;
	} else {
		ret = -1;
	}

	odp_spinlock_unlock(&sched->grp_lock);
	return ret;
}

/* This function is a no-op in linux-generic */
void odp_schedule_prefetch(int num ODP_UNUSED)
{
}

void sched_enq_called(void)
{
	sched_local.enq_called = 1;
}

void get_sched_order(queue_entry_t **origin_qe, uint64_t *order)
{
	if (sched_local.ignore_ordered_context) {
		sched_local.ignore_ordered_context = 0;
		*origin_qe = NULL;
	} else {
		*origin_qe = sched_local.origin_qe;
		*order     = sched_local.order;
	}
}

void get_sched_sync(queue_entry_t **origin_qe, uint64_t **sync, uint32_t ndx)
{
	*origin_qe = sched_local.origin_qe;
	*sync      = &sched_local.sync[ndx];
}

void sched_order_resolved(odp_buffer_hdr_t *buf_hdr)
{
	if (buf_hdr)
		buf_hdr->origin_qe = NULL;
	sched_local.origin_qe = NULL;
}

int schedule_queue(const queue_entry_t *qe)
{
	sched_local.ignore_ordered_context = 1;
	return odp_queue_enq(qe->s.pri_queue, qe->s.cmd_ev);
}
