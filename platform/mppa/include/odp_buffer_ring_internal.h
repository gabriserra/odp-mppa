/* Copyright (c) 2015, Kalray
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */


/**
 * @file
 *
 * ODP buffer ring - internal header
 */

#ifndef ODP_BUFFER_RING_INTERNAL_H_
#define ODP_BUFFER_RING_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp_buffer_internal.h>
#include <odp/atomic.h>
#include <odp_atomic_internal.h>

typedef struct {
	odp_buffer_hdr_t      **buf_ptrs;
	uint32_t                buf_num;
	odp_atomic_u32_t        prod_head;
	odp_atomic_u32_t        prod_tail;
	odp_atomic_u32_t        cons_head;
	odp_atomic_u32_t        cons_tail;
} odp_buffer_ring_t;


static inline void odp_buffer_ring_init(odp_buffer_ring_t *ring, void *addr,
					uint32_t buf_num)
{
	ring->buf_ptrs = addr;
	odp_atomic_init_u32(&ring->prod_head, 0);
	odp_atomic_init_u32(&ring->prod_tail, 0);
	odp_atomic_init_u32(&ring->cons_head, 0);
	odp_atomic_init_u32(&ring->cons_tail, 0);
	ring->buf_num = buf_num;
}

int odp_buffer_ring_get_multi(odp_buffer_ring_t *ring,
			      odp_buffer_hdr_t *buffers[],
			      unsigned n_buffers, unsigned divisor,
			      uint32_t *left);
void odp_buffer_ring_push_multi(odp_buffer_ring_t *ring,
				odp_buffer_hdr_t *buffers[],
				unsigned n_buffers, uint32_t *left);
unsigned odp_buffer_ring_push_list(odp_buffer_ring_t *ring,
				   odp_buffer_hdr_t **head,
				   unsigned n_buffers);

static inline uint32_t odp_buffer_ring_get_count(odp_buffer_ring_t *ring)
{
	uint32_t bufcount = odp_atomic_load_u32(&ring->prod_tail) - odp_atomic_load_u32(&ring->cons_tail);
	if(bufcount > ring->buf_num)
		bufcount += ring->buf_num + 1;

	return bufcount;
}

#ifdef __cplusplus
}
#endif

#endif
