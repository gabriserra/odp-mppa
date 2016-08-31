#include <HAL/hal/core/atomic.h>
#include "ring.h"

static inline int atomic_u32_cmp_xchg_strong_mm(
		odp_atomic_u32_t *atom,
		uint32_t *exp,
		uint32_t val)
{
	__k1_uint64_t tmp = 0;
	tmp = __builtin_k1_acwsu((void *)&atom->v, val, *exp );
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

		if(atomic_u32_cmp_xchg_strong_mm(&ring->cons_head, &cons_head,
						      cons_next)){
			break;
		}
	} while(1);

	for (unsigned i = 0, idx = cons_head; i < n_bufs; ++i, ++idx){
		if(unlikely(idx == ring->buf_num))
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

void buffer_ring_push_multi(buffer_ring_t *ring,
				mppa_pcie_noc_rx_buf_t *buffers[],
				unsigned n_buffers, uint32_t *left)
{
	uint32_t prod_head, cons_tail, prod_next;

	do {
		prod_head =  odp_atomic_load_u32(&ring->prod_head);

		prod_next = prod_head + n_buffers;
		if(prod_next > ring->buf_num)
			prod_next = prod_next - ring->buf_num;

		if(atomic_u32_cmp_xchg_strong_mm(&ring->prod_head, &prod_head,
						      prod_next)){
			cons_tail = odp_atomic_load_u32(&ring->cons_tail);
			break;

		}
	} while(1);

	for (unsigned i = 0, idx = prod_head; i < n_buffers; ++i, ++idx) {
		if(unlikely(idx == ring->buf_num))
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
