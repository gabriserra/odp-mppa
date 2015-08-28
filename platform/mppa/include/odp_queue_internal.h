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

#include <odp/queue.h>
#include <odp_buffer_internal.h>
#include <odp_align_internal.h>
#include <odp/packet_io.h>
#include <odp/align.h>


#define USE_TICKETLOCK

#ifdef USE_TICKETLOCK
#include <odp/ticketlock.h>
#else
#include <odp/spinlock.h>
#endif

#define QUEUE_MULTI_MAX 8

#define QUEUE_STATUS_FREE         0
#define QUEUE_STATUS_DESTROYED    1
#define QUEUE_STATUS_READY        2
#define QUEUE_STATUS_NOTSCHED     3
#define QUEUE_STATUS_SCHED        4


/* forward declaration */
union queue_entry_u;

typedef int (*enq_func_t)(union queue_entry_u *, odp_buffer_hdr_t *);
typedef	odp_buffer_hdr_t *(*deq_func_t)(union queue_entry_u *);

typedef int (*enq_multi_func_t)(union queue_entry_u *,
				odp_buffer_hdr_t **, int);
typedef	int (*deq_multi_func_t)(union queue_entry_u *,
				odp_buffer_hdr_t **, int);

struct queue_entry_s {
#ifdef USE_TICKETLOCK
	odp_ticketlock_t  lock ODP_ALIGNED_CACHE;
#else
	odp_spinlock_t    lock ODP_ALIGNED_CACHE;
#endif

	odp_buffer_hdr_t *head;
	odp_buffer_hdr_t *tail;
	int               status;

	enq_func_t       enqueue ODP_ALIGNED_CACHE;
	deq_func_t       dequeue;
	enq_multi_func_t enqueue_multi;
	deq_multi_func_t dequeue_multi;

	odp_queue_t       pri_queue;
	odp_event_t       cmd_ev;
	odp_queue_type_t  type;
	odp_queue_param_t param;
	odp_pktio_t       pktin;
	odp_pktio_t       pktout;
	char              name[ODP_QUEUE_NAME_LEN];
};

typedef union queue_entry_u {
	struct queue_entry_s s;
	uint8_t pad[ODP_CACHE_LINE_SIZE_ROUNDUP(sizeof(struct queue_entry_s))];
} queue_entry_t;


int queue_enq(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr);
odp_buffer_hdr_t *queue_deq(queue_entry_t *queue);

int queue_enq_multi(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr[], int num);
int queue_deq_multi(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr[], int num);

int queue_enq_dummy(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr);
int queue_enq_multi_dummy(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr[],
			  int num);
int queue_deq_multi_destroy(queue_entry_t *queue, odp_buffer_hdr_t *buf_hdr[],
			    int num);

void queue_lock(queue_entry_t *queue);
void queue_unlock(queue_entry_t *queue);

int queue_sched_atomic(odp_queue_t handle);

uint32_t queue_to_id(odp_queue_t handle);

static inline queue_entry_t *queue_to_qentry(odp_queue_t handle)
{
	return (queue_entry_t*)(handle);
}

static inline int queue_is_atomic(queue_entry_t *qe)
{
	return qe->s.param.sched.sync == ODP_SCHED_SYNC_ATOMIC;
}

static inline odp_queue_t queue_handle(queue_entry_t *qe)
{
	return (odp_queue_t)(qe);
}

static inline int queue_prio(queue_entry_t *qe)
{
	return qe->s.param.sched.prio;
}

void queue_destroy_finalize(queue_entry_t *qe);

#ifdef __cplusplus
}
#endif

#endif
