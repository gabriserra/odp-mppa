/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/api/queue.h>
#include <odp_queue_internal.h>
#include <odp/api/std_types.h>
#include <odp/api/align.h>
#include <odp/api/buffer.h>
#include <odp_buffer_internal.h>
#include <odp_pool_internal.h>
#include <odp_buffer_inlines.h>
#include <odp_internal.h>
#include <odp/api/shared_memory.h>
#include <odp/api/schedule.h>
#include <odp_schedule_internal.h>
#include <odp_packet_io_internal.h>
#include <odp_packet_io_queue.h>
#include <odp_debug_internal.h>
#include <odp/api/hints.h>
#include <odp/api/sync.h>

#define NUM_INTERNAL_QUEUES (SCHEDULE_NUM_PRIO * SCHEDULE_QUEUES_PER_PRIO \
			     + POLL_CMD_QUEUES)

#ifdef USE_TICKETLOCK
#include <odp/api/ticketlock.h>
#define LOCK(a)      do {				\
		odp_ticketlock_lock(&(a)->s.lock);	\
	} while(0)

#define UNLOCK(a)    do {				\
		odp_ticketlock_unlock(&(a)->s.lock);	\
	}while(0)

#define LOCK_INIT(a) odp_ticketlock_init(&(a)->s.lock)
#define LOCK_TRY(a)  ({ __k1_wmb(); odp_ticketlock_trylock(&(a)->s.lock); })

#else
#include <odp/api/spinlock.h>
#define LOCK(a)      do {				\
		odp_spinlock_lock(&(a)->s.lock);	\
	} while(0)
#define UNLOCK(a)    do {				\
		odp_spinlock_unlock(&(a)->s.lock);	\
	}while(0)
#define LOCK_INIT(a) odp_spinlock_init(&(a)->s.lock)
#define LOCK_TRY(a)  ({ __k1_wmb(); odp_spinlock_trylock(&(a)->s.lock); })
#endif

#include <string.h>

#define RESOLVE_ORDER 0
#define SUSTAIN_ORDER 1

#define NOAPPEND 0
#define APPEND   1

static queue_table_t queue_tbl_cached;
queue_table_t *queue_tbl_ptr;

static inline int queue_is_atomic(queue_entry_t *qe)
{
	return qe->s.param.sched.sync == ODP_SCHED_SYNC_ATOMIC;
}

static inline int queue_is_ordered(queue_entry_t *qe)
{
	return qe->s.param.sched.sync == ODP_SCHED_SYNC_ORDERED;
}


static int queue_init(queue_entry_t *queue, const char *name,
		      const odp_queue_param_t *param)
{
	strncpy(queue->s.name, name, ODP_QUEUE_NAME_LEN - 1);

	memcpy(&queue->s.param, param, sizeof(odp_queue_param_t));
	if (queue->s.param.sched.lock_count >
	    SCHEDULE_ORDERED_LOCKS_PER_QUEUE)
		return -1;

	if (param->type == ODP_QUEUE_TYPE_SCHED)
		queue->s.param.deq_mode = ODP_QUEUE_OP_DISABLED;

	queue->s.type = queue->s.param.type;

	queue->s.enqueue = queue_enq;
	queue->s.dequeue = queue_deq;
	queue->s.enqueue_multi = queue_enq_multi;
	queue->s.dequeue_multi = queue_deq_multi;

	queue->s.pktin = PKTIN_INVALID;

	queue->s.head = NULL;
	queue->s.tail = &queue->s.head;

	queue->s.reorder_head = NULL;
	queue->s.reorder_tail = NULL;

	return 0;
}

int odp_queue_init_global(void)
{
	uint32_t i, j;

	ODP_DBG("Queue init ... ");
	queue_tbl_ptr = CACHED_TO_UNCACHED(&queue_tbl_cached);
	memset(queue_tbl_ptr, 0, sizeof(*queue_tbl_ptr));

	for (i = 0; i < ODP_CONFIG_QUEUES; i++) {
		/* init locks */
		queue_entry_t *queue = &queue_tbl_ptr->queue[i];
		LOCK_INIT(queue);
		for (j = 0; j < SCHEDULE_ORDERED_LOCKS_PER_QUEUE; j++) {
			odp_atomic_init_u64(&queue->s.sync_in[j], 0);
			odp_atomic_init_u64(&queue->s.sync_out[j], 0);
		}
	}

	ODP_DBG("done\n");
	ODP_DBG("Queue init global\n");
	ODP_DBG("  struct queue_entry_s size %zu\n",
		sizeof(struct queue_entry_s));
	ODP_DBG("  queue_entry_t size        %zu\n",
		sizeof(queue_entry_t));
	ODP_DBG("\n");
	__k1_wmb();
	return 0;
}

int odp_queue_term_global(void)
{
	int rc = 0;
	queue_entry_t *queue;
	int i;

	for (i = 0; i < ODP_CONFIG_QUEUES; i++) {
		queue = &queue_tbl_ptr->queue[i];
		LOCK(queue);
		if (queue->s.status != QUEUE_STATUS_FREE) {

			ODP_ERR("Not destroyed queue: %s\n", queue->s.name);
			rc = -1;
		}
		UNLOCK(queue);
	}

	return rc;
}

int odp_queue_capability(odp_queue_capability_t *capa)
{
	memset(capa, 0, sizeof(odp_queue_capability_t));

	/* Reserve some queues for internal use */
	capa->max_queues        = ODP_CONFIG_QUEUES - NUM_INTERNAL_QUEUES;
	capa->max_ordered_locks = SCHEDULE_ORDERED_LOCKS_PER_QUEUE;
	capa->max_sched_groups  = sched_fn->num_grps();
	capa->sched_prios       = odp_schedule_num_prio();

	return 0;
}

odp_queue_type_t odp_queue_type(odp_queue_t handle)
{
	queue_entry_t *queue;

	queue = queue_to_qentry(handle);

	return queue->s.type;
}

odp_schedule_sync_t odp_queue_sched_type(odp_queue_t handle)
{
	queue_entry_t *queue;

	queue = queue_to_qentry(handle);

	return queue->s.param.sched.sync;
}

odp_schedule_prio_t odp_queue_sched_prio(odp_queue_t handle)
{
	queue_entry_t *queue;

	queue = queue_to_qentry(handle);

	return queue->s.param.sched.prio;
}

odp_schedule_group_t odp_queue_sched_group(odp_queue_t handle)
{
	queue_entry_t *queue;

	queue = queue_to_qentry(handle);

	return queue->s.param.sched.group;
}

int odp_queue_lock_count(odp_queue_t handle)
{
	queue_entry_t *queue = queue_to_qentry(handle);

	return queue->s.param.sched.sync == ODP_SCHED_SYNC_ORDERED ?
		(int)queue->s.param.sched.lock_count : -1;
}

odp_queue_t odp_queue_create(const char *name, const odp_queue_param_t *param)
{
	uint32_t i;
	queue_entry_t *queue;
	odp_queue_t handle = ODP_QUEUE_INVALID;
	odp_queue_type_t type = ODP_QUEUE_TYPE_PLAIN;
	odp_queue_param_t default_param;

	if (param == NULL) {
		odp_queue_param_init(&default_param);
		param = &default_param;
	}

	for (i = 0; i < ODP_CONFIG_QUEUES; i++) {
		queue = &queue_tbl_ptr->queue[i];

		if (queue->s.status != QUEUE_STATUS_FREE)
			continue;

		LOCK(queue);
		if (queue->s.status == QUEUE_STATUS_FREE) {
			if (queue_init(queue, name, param)) {
				UNLOCK(queue);
				return handle;
			}

			type = queue->s.type;

			if (type == ODP_QUEUE_TYPE_SCHED)
				queue->s.status = QUEUE_STATUS_NOTSCHED;
			else
				queue->s.status = QUEUE_STATUS_READY;

			handle = queue_handle(queue);
			UNLOCK(queue);
			break;
		}
		UNLOCK(queue);
	}

	if (handle != ODP_QUEUE_INVALID && type == ODP_QUEUE_TYPE_SCHED) {
		if (sched_fn->init_queue(qentry_to_id(queue),
					 &queue->s.param.sched)) {
			queue->s.status = QUEUE_STATUS_FREE;
			ODP_ERR("schedule queue init failed\n");
			return ODP_QUEUE_INVALID;
		}
	}

	return handle;
}

void sched_cb_queue_destroy_finalize(uint32_t queue_index)
{
	queue_entry_t *queue = get_qentry(queue_index);
	LOCK(queue);

	if (queue->s.status == QUEUE_STATUS_DESTROYED) {
		queue->s.status = QUEUE_STATUS_FREE;
		sched_fn->destroy_queue(queue_index);
	}
	UNLOCK(queue);
}

int odp_queue_destroy(odp_queue_t handle)
{
	queue_entry_t *queue;
	queue = queue_to_qentry(handle);

	LOCK(queue);
	if (queue->s.status == QUEUE_STATUS_FREE) {
		UNLOCK(queue);
		ODP_ERR("queue \"%s\" already free\n", queue->s.name);
		return -1;
	}
	if (queue->s.status == QUEUE_STATUS_DESTROYED) {
		UNLOCK(queue);
		ODP_ERR("queue \"%s\" already destroyed\n", queue->s.name);
		return -1;
	}
	if (queue->s.head != NULL) {
		UNLOCK(queue);
		ODP_ERR("queue \"%s\" not empty\n", queue->s.name);
		return -1;
	}
	if (queue_is_ordered(queue) && queue->s.reorder_head) {
		UNLOCK(queue);
		ODP_ERR("queue \"%s\" reorder queue not empty\n",
			queue->s.name);
		return -1;
	}

	switch (queue->s.status) {
	case QUEUE_STATUS_READY:
		queue->s.status = QUEUE_STATUS_FREE;
		break;
	case QUEUE_STATUS_NOTSCHED:
		queue->s.status = QUEUE_STATUS_FREE;
		sched_fn->destroy_queue(qentry_to_id(queue));
		break;
	case QUEUE_STATUS_SCHED:
		/* Queue is still in scheduling */
		queue->s.status = QUEUE_STATUS_DESTROYED;
		break;
	default:
		ODP_ABORT("Unexpected queue status\n");
	}
	UNLOCK(queue);

	return 0;
}


int odp_queue_context_set(odp_queue_t handle, void *context,
			  uint32_t len ODP_UNUSED)
{
	queue_entry_t *queue;
	queue = queue_to_qentry(handle);
	odp_mb_full();
	queue->s.param.context = context;
	odp_mb_full();
	return 0;
}

void *odp_queue_context(odp_queue_t handle)
{
	queue_entry_t *queue;
	queue = queue_to_qentry(handle);
	return queue->s.param.context;
}

odp_queue_t odp_queue_lookup(const char *name)
{
	uint32_t i;

	for (i = 0; i < ODP_CONFIG_QUEUES; i++) {
		queue_entry_t *queue = &queue_tbl_ptr->queue[i];

		if (queue->s.status == QUEUE_STATUS_FREE ||
		    queue->s.status == QUEUE_STATUS_DESTROYED)
			continue;

		LOCK(queue);
		if (strcmp(name, queue->s.name) == 0) {
			/* found it */
			UNLOCK(queue);
			return queue_handle(queue);
		}
		UNLOCK(queue);
	}

	return ODP_QUEUE_INVALID;
}

/* Update queue head and/or tail and schedule status
 * Return if the queue needs to be reschedule.
 * Queue must be locked before calling this function
 */
static int _queue_enq_update(queue_entry_t *queue, odp_buffer_hdr_t *head,
			     odp_buffer_hdr_t *tail, int status){

	odp_buffer_hdr_t ** q_tail = queue->s.tail;

	STORE_PTR(tail->next, NULL);
	queue->s.tail = &tail->next;
	STORE_PTR_IMM(q_tail, head);

	if (status == QUEUE_STATUS_NOTSCHED) {
		queue->s.status = QUEUE_STATUS_SCHED;
		return 1; /* retval: schedule queue */
	}
	return 0;
}


int queue_enq(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr, int sustain)
{
	int ret, sched;

	if (sched_fn->ord_enq(qentry_to_id(queue), buf_hdr, sustain, &ret))
		return ret;


	LOCK(queue);
	int status = queue->s.status;
	if (odp_unlikely(status < QUEUE_STATUS_READY)) {
		UNLOCK(queue);
		ODP_ERR("Bad queue status\n");
		return -1;
	}

	sched = _queue_enq_update(queue, buf_hdr, buf_hdr, status);

	UNLOCK(queue);

	/* Add queue to scheduling */
	if (sched && sched_fn->sched_queue(qentry_to_id(queue)))
		ODP_ABORT("schedule_queue failed\n");

	return 0;
}

int queue_enq_multi(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr[],
		    int num, int sustain)
{
	int sched = 0;
	int i, ret;
	odp_buffer_hdr_t *tail;

	/* Chain input buffers together */
	for (i = 0; i < num - 1; i++)
		buf_hdr[i]->next = buf_hdr[i + 1];

	tail = buf_hdr[num - 1];
	buf_hdr[num - 1]->next = NULL;

	if (sched_fn->ord_enq_multi(qentry_to_id(queue), (void **)buf_hdr, num,
				    sustain, &ret))
		return ret;

	/* Handle unordered enqueues */
	LOCK(queue);
	int status = queue->s.status;
	if (odp_unlikely(status < QUEUE_STATUS_READY)) {
		UNLOCK(queue);
		ODP_ERR("Bad queue status\n");
		return -1;
	}

	sched = _queue_enq_update(queue, buf_hdr[0], tail, status);
	UNLOCK(queue);

	/* Add queue to scheduling */
	if (sched && sched_fn->sched_queue(qentry_to_id(queue)))
		ODP_ABORT("schedule_queue failed\n");

	return num; /* All events enqueued */
}

int odp_queue_enq_multi(odp_queue_t handle, const odp_event_t ev[], int num)
{
	queue_entry_t *queue;

	queue = queue_to_qentry(handle);
	return queue->s.enqueue_multi(queue, (odp_buffer_hdr_t **)ev, num, 1);
}


int odp_queue_enq(odp_queue_t handle, odp_event_t ev)
{
	queue_entry_t *queue;
	queue   = queue_to_qentry(handle);

	/* No chains via this entry */
	((odp_buffer_hdr_t*)handle)->link = NULL;

	return queue->s.enqueue(queue, (odp_buffer_hdr_t *)ev, 1);
}

odp_buffer_hdr_t *queue_deq(queue_entry_t *queue)
{
	odp_buffer_hdr_t *buf_hdr;
	uint32_t i;

	if (queue->s.head == NULL)
		return NULL;

	LOCK(queue);

	buf_hdr       = queue->s.head;
	if (buf_hdr == NULL) {
		/* Already empty queue */
		if (queue->s.status == QUEUE_STATUS_SCHED)
			queue->s.status = QUEUE_STATUS_NOTSCHED;
		UNLOCK(queue);
		return NULL;
	}

	INVALIDATE((odp_packet_hdr_t*)buf_hdr);

	/* Note that order should really be assigned on enq to an
	 * ordered queue rather than deq, however the logic is simpler
	 * to do it here and has the same effect.
	 */
	if (queue_is_ordered(queue)) {
		buf_hdr->origin_qe = queue;
		buf_hdr->order = queue->s.order_in++;
		for (i = 0; i < queue->s.param.sched.lock_count; i++) {
			buf_hdr->sync[i] =
				odp_atomic_fetch_inc_u64(&queue->s.sync_in[i]);
		}
		buf_hdr->flags.sustain = SUSTAIN_ORDER;
	} else {
		buf_hdr->origin_qe = NULL;
	}

	queue->s.head = buf_hdr->next;
	if (buf_hdr->next == NULL) {
		/* Queue is now empty */
		queue->s.tail = &queue->s.head;
	}
	buf_hdr->next = NULL;

	UNLOCK(queue);

	return buf_hdr;
}


int queue_deq_multi(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr[], int num)
{
	odp_buffer_hdr_t *hdr;
	int i;
	uint32_t j;

	LOCK(queue);
	int status = queue->s.status;
	if (odp_unlikely(status < QUEUE_STATUS_READY)) {
		/* Bad queue, or queue has been destroyed.
		 * Scheduler finalizes queue destroy after this. */
		UNLOCK(queue);
		return -1;
	}

	hdr = queue->s.head;

	if (hdr == NULL) {
		/* Already empty queue */
		if (status == QUEUE_STATUS_SCHED)
			queue->s.status = QUEUE_STATUS_NOTSCHED;

		UNLOCK(queue);
		return 0;
	}

	for (i = 0; i < num && hdr; i++) {
		INVALIDATE((odp_packet_hdr_t*)hdr);
		buf_hdr[i]       = hdr;
		hdr              = hdr->next;
		buf_hdr[i]->next = NULL;
		if (queue_is_ordered(queue)) {
			buf_hdr[i]->origin_qe = queue;
			buf_hdr[i]->order     = queue->s.order_in++;
			for (j = 0; j < queue->s.param.sched.lock_count; j++) {
				buf_hdr[i]->sync[j] =
					odp_atomic_fetch_inc_u64
					(&queue->s.sync_in[j]);
			}
			buf_hdr[i]->flags.sustain = SUSTAIN_ORDER;
		} else {
			buf_hdr[i]->origin_qe = NULL;
		}
	}

	queue->s.head = hdr;

	if (hdr == NULL) {
		/* Queue is now empty */
		queue->s.tail = &queue->s.head;
	}

	UNLOCK(queue);

	return i;
}

int odp_queue_deq_multi(odp_queue_t handle, odp_event_t events[], int num)
{
	queue_entry_t *queue;
	int ret;

	queue = queue_to_qentry(handle);
	ret = queue->s.dequeue_multi(queue, (odp_buffer_hdr_t **)events, num);

	return ret;
}


odp_event_t odp_queue_deq(odp_queue_t handle)
{
	queue_entry_t *queue;
	odp_buffer_hdr_t *buf_hdr;

	queue   = queue_to_qentry(handle);
	buf_hdr = queue->s.dequeue(queue);

	if (buf_hdr)
		return (odp_event_t)buf_hdr;

	return ODP_EVENT_INVALID;
}

void queue_lock(queue_entry_t *queue)
{
	LOCK(queue);
}

void queue_unlock(queue_entry_t *queue)
{
	UNLOCK(queue);
}

void odp_queue_param_init(odp_queue_param_t *params)
{
	memset(params, 0, sizeof(odp_queue_param_t));
	params->type = ODP_QUEUE_TYPE_PLAIN;
	params->enq_mode = ODP_QUEUE_OP_MT;
	params->deq_mode = ODP_QUEUE_OP_MT;
	params->sched.prio  = ODP_SCHED_PRIO_DEFAULT;
	params->sched.sync  = ODP_SCHED_SYNC_PARALLEL;
	params->sched.group = ODP_SCHED_GROUP_ALL;
}

int odp_queue_info(odp_queue_t handle, odp_queue_info_t *info)
{
	uint32_t queue_id;
	queue_entry_t *queue;
	int status;

	if (odp_unlikely(info == NULL)) {
		ODP_ERR("Unable to store info, NULL ptr given\n");
		return -1;
	}

	queue_id = queue_to_id(handle);

	if (odp_unlikely(queue_id >= ODP_CONFIG_QUEUES)) {
		ODP_ERR("Invalid queue handle:%" PRIu64 "\n",
			odp_queue_to_u64(handle));
		return -1;
	}

	queue = get_qentry(queue_id);

	LOCK(queue);
	status = queue->s.status;

	if (odp_unlikely(status == QUEUE_STATUS_FREE ||
			 status == QUEUE_STATUS_DESTROYED)) {
		UNLOCK(queue);
		ODP_ERR("Invalid queue status:%d\n", status);
		return -1;
	}

	info->name = queue->s.name;
	info->param = queue->s.param;

	UNLOCK(queue);

	return 0;
}

int sched_cb_num_queues(void)
{
	return ODP_CONFIG_QUEUES;
}

int sched_cb_queue_prio(uint32_t queue_index)
{
	queue_entry_t *qe = get_qentry(queue_index);

	return qe->s.param.sched.prio;
}

int sched_cb_queue_grp(uint32_t queue_index)
{
	queue_entry_t *qe = get_qentry(queue_index);

	return qe->s.param.sched.group;
}

int sched_cb_queue_is_ordered(uint32_t queue_index)
{
	return queue_is_ordered(get_qentry(queue_index));
}

int sched_cb_queue_is_atomic(uint32_t queue_index)
{
	return queue_is_atomic(get_qentry(queue_index));
}

odp_queue_t sched_cb_queue_handle(uint32_t queue_index)
{
	return queue_from_id(queue_index);
}

int sched_cb_queue_deq_multi(uint32_t queue_index, odp_event_t ev[], int num)
{
	int ret;
	queue_entry_t *qe = get_qentry(queue_index);

	ret = queue_deq_multi(qe, (odp_buffer_hdr_t **)ev, num);

	return ret;
}

int sched_cb_queue_empty(uint32_t queue_index)
{
	queue_entry_t *queue = get_qentry(queue_index);
	int ret = 0;

	LOCK(queue);

	if (odp_unlikely(queue->s.status < QUEUE_STATUS_READY)) {
		/* Bad queue, or queue has been destroyed. */
		UNLOCK(queue);
		return -1;
	}

	if (queue->s.head == NULL) {
		/* Already empty queue. Update status. */
		if (queue->s.status == QUEUE_STATUS_SCHED)
			queue->s.status = QUEUE_STATUS_NOTSCHED;

		ret = 1;
	}

	UNLOCK(queue);

	return ret;
}
