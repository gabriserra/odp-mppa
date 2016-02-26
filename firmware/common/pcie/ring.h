#include <inttypes.h>
#include "internal/atomic.h"

#ifndef MPPA_PCIE_BUF_ALLOC_H
#define MPPA_PCIE_BUF_ALLOC_H

#define POOL_MULTI_MAX 64

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

typedef struct mppa_pcie_noc_rx_buf {
	void *buf_addr;
	uint8_t pkt_count;		/* Count of packet in this buffer */
} mppa_pcie_noc_rx_buf_t; 

typedef struct {
	mppa_pcie_noc_rx_buf_t **buf_ptrs;
	uint32_t buf_num;
	odp_atomic_u32_t prod_head;
	odp_atomic_u32_t prod_tail;
	odp_atomic_u32_t cons_head;
	odp_atomic_u32_t cons_tail;
} buffer_ring_t;


static inline void buffer_ring_init(buffer_ring_t *ring, mppa_pcie_noc_rx_buf_t **addr,
					uint32_t buf_num)
{
	ring->buf_ptrs = addr;
	__builtin_k1_swu(&ring->prod_head, 0);
	__builtin_k1_swu(&ring->prod_tail, 0);
	__builtin_k1_swu(&ring->cons_head, 0);
	__builtin_k1_swu(&ring->cons_tail, 0);
	ring->buf_num = buf_num;
}

int buffer_ring_get_multi(buffer_ring_t *ring,
			      mppa_pcie_noc_rx_buf_t *buffers[],
			      unsigned n_buffers, uint32_t *left);
void buffer_ring_push_multi(buffer_ring_t *ring,
				mppa_pcie_noc_rx_buf_t *buffers[],
				unsigned n_buffers, uint32_t *left);

static inline uint32_t odp_buffer_ring_get_count(buffer_ring_t *ring)
{
	uint32_t bufcount = __builtin_k1_lwu(&ring->prod_tail) - __builtin_k1_lwu(&ring->cons_tail);
	if(bufcount > ring->buf_num)
		bufcount += ring->buf_num + 1;

	return bufcount;
}


#endif
