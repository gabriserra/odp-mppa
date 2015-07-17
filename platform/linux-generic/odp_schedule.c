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

/* Number of schedule commands.
 * One per scheduled queue and packet interface */
#define NUM_SCHED_CMD (ODP_CONFIG_QUEUES + ODP_CONFIG_PKTIO_ENTRIES)

/* Scheduler sub queues */
#define QUEUES_PER_PRIO  4

/* Maximum number of dequeues */
#define MAX_DEQ 4


/* Mask of queues per priority */
typedef uint8_t pri_mask_t;

_ODP_STATIC_ASSERT((8*sizeof(pri_mask_t)) >= QUEUES_PER_PRIO,
		   "pri_mask_t_is_too_small");


typedef struct {
	odp_queue_t    pri_queue[ODP_CONFIG_SCHED_PRIOS][QUEUES_PER_PRIO];
	pri_mask_t     pri_mask[ODP_CONFIG_SCHED_PRIOS];
	odp_spinlock_t mask_lock;
	odp_pool_t     pool;
	odp_shm_t      shm;
	uint32_t       pri_count[ODP_CONFIG_SCHED_PRIOS][QUEUES_PER_PRIO];
} sched_t;

/* Schedule command */
typedef struct {
	int           cmd;

	union {
		queue_entry_t *qe;

		struct {
			odp_pktio_t   pktio;
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
	int num;
	int index;
	int pause;

} sched_local_t;

/* Global scheduler context */
static sched_t *sched;

/* Thread local scheduler context */
static __thread sched_local_t sched_local;

static void sched_local_init(void)
{
	int i;

	memset(&sched_local, 0, sizeof(sched_local_t));

	sched_local.pri_queue = ODP_QUEUE_INVALID;
	sched_local.cmd_ev    = ODP_EVENT_INVALID;
	sched_local.qe        = NULL;

	for (i = 0; i < MAX_DEQ; i++)
		sched_local.buf_hdr[i] = NULL;
}

int odp_schedule_init_global(void)
{
	odp_shm_t shm;
	odp_pool_t pool;
	int i, j;
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

	params.buf.size  = sizeof(sched_cmd_t);
	params.buf.align = 0;
	params.buf.num   = NUM_SCHED_CMD;
	params.type      = ODP_POOL_BUFFER;

	pool = odp_pool_create("odp_sched_pool", ODP_SHM_NULL, &params);

	if (pool == ODP_POOL_INVALID) {
		ODP_ERR("Schedule init: Pool create failed.\n");
		return -1;
	}

	sched->pool = pool;
	sched->shm  = shm;
	odp_spinlock_init(&sched->mask_lock);

	for (i = 0; i < ODP_CONFIG_SCHED_PRIOS; i++) {
		odp_queue_t queue;
		char name[] = "odp_priXX_YY";

		name[7] = '0' + i / 10;
		name[8] = '0' + i - 10*(i / 10);

		for (j = 0; j < QUEUES_PER_PRIO; j++) {
			name[10] = '0' + j / 10;
			name[11] = '0' + j - 10*(j / 10);

			queue = odp_queue_create(name,
						 ODP_QUEUE_TYPE_POLL, NULL);

			if (queue == ODP_QUEUE_INVALID) {
				ODP_ERR("Sched init: Queue create failed.\n");
				return -1;
			}

			sched->pri_queue[i][j] = queue;
			sched->pri_mask[i]     = 0;
		}
	}

	ODP_DBG("done\n");

	return 0;
}

int odp_schedule_term_global(void)
{
	int ret = 0;
	int rc = 0;
	int i, j;

	for (i = 0; i < ODP_CONFIG_SCHED_PRIOS; i++) {
		for (j = 0; j < QUEUES_PER_PRIO; j++) {
			odp_queue_t  pri_q;
			odp_event_t  ev;

			pri_q = sched->pri_queue[i][j];

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

	odp_schedule_release_atomic();

	sched_local_init();
	return 0;
}

static int pri_id_queue(odp_queue_t queue)
{
	return (QUEUES_PER_PRIO-1) & (queue_to_id(queue));
}

static int pri_id_pktio(odp_pktio_t pktio)
{
	return (QUEUES_PER_PRIO-1) & (pktio_to_id(pktio));
}

static odp_queue_t pri_set(int id, int prio)
{
	odp_spinlock_lock(&sched->mask_lock);
	sched->pri_mask[prio] |= 1 << id;
	sched->pri_count[prio][id]++;
	odp_spinlock_unlock(&sched->mask_lock);

	return sched->pri_queue[prio][id];
}

static void pri_clr(int id, int prio)
{
	odp_spinlock_lock(&sched->mask_lock);

	/* Clear mask bit when last queue is removed*/
	sched->pri_count[prio][id]--;

	if (sched->pri_count[prio][id] == 0)
		sched->pri_mask[prio] &= (uint8_t)(~(1 << id));

	odp_spinlock_unlock(&sched->mask_lock);
}

static odp_queue_t pri_set_queue(odp_queue_t queue, int prio)
{
	int id = pri_id_queue(queue);

	return pri_set(id, prio);
}

static odp_queue_t pri_set_pktio(odp_pktio_t pktio, int prio)
{
	int id = pri_id_pktio(pktio);

	return pri_set(id, prio);
}

static void pri_clr_queue(odp_queue_t queue, int prio)
{
	int id = pri_id_queue(queue);
	pri_clr(id, prio);
}

static void pri_clr_pktio(odp_pktio_t pktio, int prio)
{
	int id = pri_id_pktio(pktio);
	pri_clr(id, prio);
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
	qe->s.pri_queue = pri_set_queue(queue_handle(qe), queue_prio(qe));

	return 0;
}

void schedule_queue_destroy(queue_entry_t *qe)
{
	odp_buffer_t buf;

	buf = odp_buffer_from_event(qe->s.cmd_ev);
	odp_buffer_free(buf);

	pri_clr_queue(queue_handle(qe), queue_prio(qe));

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
	sched_cmd->pktio = pktio;
	sched_cmd->pe    = get_pktio_entry(pktio);
	sched_cmd->prio  = prio;

	pri_queue  = pri_set_pktio(pktio, prio);

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


static inline int copy_events(odp_event_t out_ev[], unsigned int max)
{
	int i = 0;

	while (sched_local.num && max) {
		odp_buffer_hdr_t *hdr = sched_local.buf_hdr[sched_local.index];
		out_ev[i] = odp_buffer_to_event(hdr->handle.handle);
		sched_local.index++;
		sched_local.num--;
		max--;
		i++;
	}

	return i;
}


/*
 * Schedule queues
 *
 * TODO: SYNC_ORDERED not implemented yet
 */
static int schedule(odp_queue_t *out_queue, odp_event_t out_ev[],
		    unsigned int max_num, unsigned int max_deq)
{
	int i, j;
	int thr;
	int ret;

	if (sched_local.num) {
		ret = copy_events(out_ev, max_num);

		if (out_queue)
			*out_queue = queue_handle(sched_local.qe);

		return ret;
	}

	odp_schedule_release_atomic();

	if (odp_unlikely(sched_local.pause))
		return 0;

	thr = odp_thread_id();

	for (i = 0; i < ODP_CONFIG_SCHED_PRIOS; i++) {
		int id;

		if (sched->pri_mask[i] == 0)
			continue;

		id = thr & (QUEUES_PER_PRIO-1);

		for (j = 0; j < QUEUES_PER_PRIO; j++, id++) {
			odp_queue_t  pri_q;
			odp_event_t  ev;
			odp_buffer_t buf;
			sched_cmd_t *sched_cmd;
			queue_entry_t *qe;
			int num;

			if (id >= QUEUES_PER_PRIO)
				id = 0;

			if (odp_unlikely((sched->pri_mask[i] & (1 << id)) == 0))
				continue;

			pri_q = sched->pri_queue[i][id];
			ev    = odp_queue_deq(pri_q);
			buf   = odp_buffer_from_event(ev);

			if (buf == ODP_BUFFER_INVALID)
				continue;

			sched_cmd = odp_buffer_addr(buf);

			if (sched_cmd->cmd == SCHED_CMD_POLL_PKTIN) {
				/* Poll packet input */
				if (pktin_poll(sched_cmd->pe)) {
					/* Stop scheduling the pktio */
					pri_clr_pktio(sched_cmd->pktio,
						      sched_cmd->prio);
					odp_buffer_free(buf);
				} else {
					/* Continue scheduling the pktio */
					if (odp_queue_enq(pri_q, ev))
						ODP_ABORT("schedule failed\n");
				}

				continue;
			}

			qe  = sched_cmd->qe;
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

			if (queue_is_atomic(qe)) {
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
	}

	return 0;
}


static int schedule_loop(odp_queue_t *out_queue, uint64_t wait,
			 odp_event_t out_ev[],
			 unsigned int max_num, unsigned int max_deq)
{
	uint64_t start_cycle, cycle, diff;
	int ret;

	start_cycle = 0;

	while (1) {
		ret = schedule(out_queue, out_ev, max_num, max_deq);

		if (ret)
			break;

		if (wait == ODP_SCHED_WAIT)
			continue;

		if (wait == ODP_SCHED_NO_WAIT)
			break;

		if (start_cycle == 0) {
			start_cycle = odp_time_cycles();
			continue;
		}

		cycle = odp_time_cycles();
		diff  = odp_time_diff_cycles(start_cycle, cycle);

		if (wait < diff)
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
	if (ns <= ODP_SCHED_NO_WAIT)
		ns = ODP_SCHED_NO_WAIT + 1;

	return odp_time_ns_to_cycles(ns);
}


int odp_schedule_num_prio(void)
{
	return ODP_CONFIG_SCHED_PRIOS;
}
