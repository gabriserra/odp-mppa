#include <HAL/hal/core/atomic.h>
#include "ring.h"

static inline int u32_xchg(volatile uint32_t *atom,
			   uint32_t *exp,
			   uint32_t val)
{
	__k1_uint64_t tmp = 0;
	tmp = __builtin_k1_acws((void *)atom, val, *exp );
	if((tmp & 0xFFFFFFFF) == *exp){
		return 1;
	}else{
		*exp = (tmp & 0xFFFFFFFF);
		return 0;
	}
}

static inline void odp_spin(void)
{
	__k1_cpu_backoff(10);
}

int buffer_ring_get_multi(buffer_ring_t *ring,
			      mppa_pcie_noc_rx_buf_t *buffers[],
			      unsigned n_buffers, uint32_t *left)
{
	uint32_t cons_head, prod_tail, cons_next;
	unsigned n_bufs;
	if(n_buffers > POOL_MULTI_MAX)
		n_buffers = POOL_MULTI_MAX;

	do {
		n_bufs = n_buffers;
		cons_head =  ring->cons_head;
		prod_tail = ring->prod_tail;

		/* No Buf available */
		if(cons_head == prod_tail)
			return 0;

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

		if(u32_xchg(&ring->cons_head, &cons_head, cons_next))
			break;

	} while(1);

	for (unsigned i = 0, idx = cons_head; i < n_bufs; ++i, ++idx){
		if(unlikely(idx == ring->buf_num))
			idx = 0;
		buffers[i] = ring->buf_ptrs[idx];
	}

	while (ring->cons_tail != cons_head)
		odp_spin();

	ring->cons_tail = cons_next;

	if (left) {
		/* Check for low watermark condition */
		uint32_t bufcount = prod_tail - cons_next;
		if(bufcount > ring->buf_num)
			bufcount += ring->buf_num;
		*left = bufcount;
	}

	return n_bufs;
}

void buffer_ring_push_multi(buffer_ring_t *ring,
				mppa_pcie_noc_rx_buf_t *buffers[],
				unsigned n_buffers, uint32_t *left)
{
	uint32_t prod_head, cons_tail, prod_next, slotcount;

	do {
		prod_head =  ring->prod_head;

		slotcount = ring->cons_tail > prod_head ?
			(ring->cons_tail - 1 - prod_head) :
			(ring->cons_tail + ring->buf_num - 1 - prod_head);

		/* Stall until we can push all at once */
		if (slotcount < n_buffers)
			continue;
		prod_next = prod_head + n_buffers;
		if(prod_next > ring->buf_num)
			prod_next = prod_next - ring->buf_num;

		if(u32_xchg(&ring->prod_head, &prod_head, prod_next)){
			cons_tail = ring->cons_tail;
			break;

		}
	} while(1);

	for (unsigned i = 0, idx = prod_head; i < n_buffers; ++i, ++idx) {
		if(unlikely(idx == ring->buf_num))
			idx = 0;

		ring->buf_ptrs[idx] = buffers[i];
	}
	while (ring->prod_tail != prod_head)
		odp_spin();

	ring->prod_tail = prod_next;

	if (left) {
		uint32_t bufcount = (prod_next - cons_tail);
		if(bufcount > ring->buf_num)
			bufcount += ring->buf_num;
		*left = bufcount;
	}
}
