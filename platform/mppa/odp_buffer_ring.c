/* Copyright (c) 2015, Kalray
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp_buffer_inlines.h>
#include <odp_buffer_ring_internal.h>
#include <stdlib.h>

int odp_buffer_ring_get_multi(odp_buffer_ring_t *ring,
			      odp_buffer_hdr_t *buffers[],
			      unsigned n_buffers, uint32_t *left)
{
	uint32_t cons_head, prod_tail, cons_next;
	unsigned n_bufs;
	if(n_buffers > POOL_MULTI_MAX)
		n_buffers = POOL_MULTI_MAX;

	do {
		n_bufs = n_buffers;
		cons_head =  odp_atomic_load_u32(&ring->cons_head);
		prod_tail = odp_atomic_load_u32(&ring->prod_tail);
		/* No Buf available */
		if(cons_head == prod_tail){
			return 0;
		}

		if(prod_tail > cons_head) {
			/* Linear buffer list */
			if(prod_tail - cons_head < n_bufs)
				n_bufs = prod_tail - cons_head;
		} else {
			/* Go to the end of the buffer and look for more */
			unsigned avail = prod_tail + ring->buf_num - cons_head;
			if(avail < n_bufs)
				n_bufs = avail;
		}
		cons_next = cons_head + n_bufs;
		if(cons_next > ring->buf_num)
			cons_next = cons_next - ring->buf_num;

		if(_odp_atomic_u32_cmp_xchg_strong_mm(&ring->cons_head, &cons_head,
						      cons_next,
						      _ODP_MEMMODEL_ACQ,
						      _ODP_MEMMODEL_RLX)){
			break;
		}
	} while(1);

	for (unsigned i = 0, idx = cons_head; i < n_bufs; ++i, ++idx){
		if(odp_unlikely(idx == ring->buf_num))
			idx = 0;
		buffers[i] = LOAD_PTR(ring->buf_ptrs[idx]);
	}

	while (odp_atomic_load_u32(&ring->cons_tail) != cons_head)
		odp_spin();

	odp_atomic_store_u32(&ring->cons_tail, cons_next);

	if (left) {
		/* Check for low watermark condition */
		uint32_t bufcount = prod_tail - cons_next;
		if(bufcount > ring->buf_num)
			bufcount += ring->buf_num;
		*left = bufcount;
	}

	return n_bufs;
}

void odp_buffer_ring_push_multi(odp_buffer_ring_t *ring,
				odp_buffer_hdr_t *buffers[],
				unsigned n_buffers, uint32_t *left)
{
	uint32_t prod_head, cons_tail, prod_next;

	do {
		prod_head =  odp_atomic_load_u32(&ring->prod_head);

		prod_next = prod_head + n_buffers;
		if(prod_next > ring->buf_num)
			prod_next = prod_next - ring->buf_num;

		if(_odp_atomic_u32_cmp_xchg_strong_mm(&ring->prod_head, &prod_head,
						      prod_next,
						      _ODP_MEMMODEL_ACQ,
						      _ODP_MEMMODEL_RLX)){
			cons_tail = odp_atomic_load_u32(&ring->cons_tail);
			break;

		}
	} while(1);

	for (unsigned i = 0, idx = prod_head; i < n_buffers; ++i, ++idx) {
		if(odp_unlikely(idx == ring->buf_num))
			idx = 0;

		STORE_PTR(ring->buf_ptrs[idx], buffers[i]);
	}
	while (odp_atomic_load_u32(&ring->prod_tail) != prod_head)
		odp_spin();

	odp_atomic_store_u32(&ring->prod_tail, prod_next);

	if (left) {
		uint32_t bufcount = (prod_next - cons_tail);
		if(bufcount > ring->buf_num)
			bufcount += ring->buf_num;
		*left = bufcount;
	}
}

unsigned odp_buffer_ring_push_list(odp_buffer_ring_t *ring,
				   odp_buffer_hdr_t **head,
				   unsigned n_buffers)
{
	const unsigned total_bufs = n_buffers;
	uint32_t prod_head, prod_next, cons_tail;

	if (!total_bufs)
		return 0;

	do {
		n_buffers = total_bufs;
		prod_head =  odp_atomic_load_u32(&ring->prod_head);
		cons_tail = odp_atomic_load_u32(&ring->cons_tail);

		if (prod_head == cons_tail && n_buffers >= ring->buf_num) {
			n_buffers = ring->buf_num - 1;
		} else if(prod_head < cons_tail && prod_head + n_buffers >= cons_tail) {
			n_buffers = cons_tail - prod_head - 1;
		} else if(prod_head > cons_tail && prod_head + n_buffers >= cons_tail + ring->buf_num) {
			n_buffers = cons_tail + ring->buf_num - prod_head - 1;
		}

		/* Ring is full */
		if (!n_buffers)
			return total_bufs;

		prod_next = prod_head + n_buffers;
		if(prod_next > ring->buf_num)
			prod_next = prod_next - ring->buf_num;

		if(_odp_atomic_u32_cmp_xchg_strong_mm(&ring->prod_head, &prod_head,
						      prod_next,
						      _ODP_MEMMODEL_ACQ,
						      _ODP_MEMMODEL_RLX)){
			break;

		}
	} while(1);

	{
		/* Push buffers to ring buf */
		odp_buffer_hdr_t *buf;
		unsigned i, idx;
		for (i = 0, idx = prod_head, buf = *head; i < n_buffers && buf; ++i, ++idx, buf = buf->next) {
			if(odp_unlikely(idx == ring->buf_num))
				idx = 0;
			ring->buf_ptrs[idx] = buf;
		}
		/* Update head */
		*head = buf;
	}

	__builtin_k1_wpurge();
	__builtin_k1_fence();

	while (odp_atomic_load_u32(&ring->prod_tail) != prod_head)
		odp_spin();

	odp_atomic_store_u32(&ring->prod_tail, prod_next);
	return total_bufs - n_buffers;
}
