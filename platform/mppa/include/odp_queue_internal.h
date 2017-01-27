/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */


/**
 * @file
 *
 * ODP queue - implementation internal
 */

#ifndef ODP_QUEUE_INTERNAL_H_
#define ODP_QUEUE_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/api/queue.h>
#include <odp_forward_typedefs_internal.h>
#include <odp_buffer_internal.h>
#include <odp_align_internal.h>
#include <odp_config_internal.h>
#include <odp/api/packet_io.h>
#include <odp/api/align.h>
#include <odp/api/hints.h>


#define USE_TICKETLOCK

#ifdef USE_TICKETLOCK
#include <odp/api/ticketlock.h>
#else
#include <odp/api/spinlock.h>
#endif

#define QUEUE_MULTI_MAX 8

#define QUEUE_STATUS_FREE         0
#define QUEUE_STATUS_DESTROYED    1
#define QUEUE_STATUS_READY        2
#define QUEUE_STATUS_NOTSCHED     3
#define QUEUE_STATUS_SCHED        4


/* forward declaration */
union queue_entry_u;

typedef int (*enq_func_t)(union queue_entry_u *, odp_buffer_hdr_t *, int);
typedef	odp_buffer_hdr_t *(*deq_func_t)(union queue_entry_u *);

typedef int (*enq_multi_func_t)(union queue_entry_u *,
				odp_buffer_hdr_t **, int, int);
typedef	int (*deq_multi_func_t)(union queue_entry_u *,
				odp_buffer_hdr_t **, int);

struct queue_entry_s {
#ifdef USE_TICKETLOCK
	odp_ticketlock_t  lock ODP_ALIGNED_CACHE;
#else
	odp_spinlock_t    lock ODP_ALIGNED_CACHE;
#endif

	odp_buffer_hdr_t *head;
	odp_buffer_hdr_t **tail;
	int               status;

	enq_func_t       enqueue ODP_ALIGNED_CACHE;
	deq_func_t       dequeue;
	enq_multi_func_t enqueue_multi;
	deq_multi_func_t dequeue_multi;

	odp_queue_type_t  type;
	odp_queue_param_t param;
	odp_pktin_queue_t pktin;
	odp_pktout_queue_t pktout;
	char              name[ODP_QUEUE_NAME_LEN];
	uint64_t          order_in;
	uint64_t          order_out;
	odp_buffer_hdr_t *reorder_head;
	odp_buffer_hdr_t *reorder_tail;
	odp_atomic_u64_t  sync_in[SCHEDULE_ORDERED_LOCKS_PER_QUEUE];
	odp_atomic_u64_t  sync_out[SCHEDULE_ORDERED_LOCKS_PER_QUEUE];
};

union queue_entry_u {
	struct queue_entry_s s;
	uint8_t pad[ODP_CACHE_LINE_SIZE_ROUNDUP(sizeof(struct queue_entry_s))];
};

typedef struct queue_table_t {
	queue_entry_t  queue[ODP_CONFIG_QUEUES];
} queue_table_t;

extern queue_table_t *queue_tbl_ptr;

int queue_enq(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr, int sustain);
odp_buffer_hdr_t *queue_deq(queue_entry_t *queue);

int queue_enq_multi(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr[], int num,
		    int sustain);
int queue_deq_multi(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr[], int num);

int queue_pktout_enq(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr,
		     int sustain);
int queue_pktout_enq_multi(queue_entry_t *queue,
			   odp_buffer_hdr_t *buf_hdr[], int num, int sustain);

int queue_tm_reenq(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr,
		   int sustain);
int queue_tm_reenq_multi(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr[],
			 int num, int sustain);
int queue_tm_reorder(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr);

void queue_lock(queue_entry_t *queue);
void queue_unlock(queue_entry_t *queue);


static inline queue_entry_t *queue_to_qentry(odp_queue_t handle)
{
	return (queue_entry_t*)(handle);
}

static inline uint32_t queue_to_id(odp_queue_t handle)
{
	queue_entry_t *entry = queue_to_qentry(handle);
	return entry - &queue_tbl_ptr->queue[0];
}


static inline uint32_t qentry_to_id(queue_entry_t *entry)
{
	return entry - &queue_tbl_ptr->queue[0];
}

static inline odp_queue_t queue_handle(queue_entry_t *qe)
{
	return (odp_queue_t)(qe);
}

static inline queue_entry_t *get_qentry(uint32_t queue_id)
{
	return &queue_tbl_ptr->queue[queue_id];
}
static inline odp_queue_t queue_from_id(uint32_t queue_id)
{
	return queue_handle(get_qentry(queue_id));
}


#ifdef __cplusplus
}
#endif

#endif
