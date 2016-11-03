/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp_packet_io_internal.h>
#include <odp/thread.h>
#include <odp/cpumask.h>
#include <odp/errno.h>
#include <errno.h>
#include <mppa_noc.h>
#include <HAL/hal/core/optimize.h>

#ifdef K1_NODEOS
#include <pthread.h>
#else
#include <utask.h>
#endif

#include "odp_pool_internal.h"
#include "odp_rx_internal.h"

#define N_RX 256
#define MAX_RX (30 * 4)
#define PKT_BURST_SZ (30)
#define N_ITER_LOCKED 1000000 /* About once per sec */
#define FLUSH_PERIOD 7 /* Must pe a (power of 2) - 1*/
#define MIN_RING_SIZE (2 * PKT_BURST_SZ)
#define BUFFER_ORDER_BITS 25

typedef struct {
	odp_packet_t pkt;
	uint8_t broken;
	uint8_t pktio_id;
} rx_tag_t;

/** Per If data */
typedef struct {
	odp_buffer_hdr_t  *head;
	odp_buffer_hdr_t **tail;
	unsigned count;
} rx_buffer_list_t;

typedef struct {
	rx_buffer_list_t hdr_list;
	uint64_t recv_pkts;
	uint64_t dropped_pkts;
	uint64_t oom_pkts;
} rx_ifce_th_t;

typedef struct {
	uint64_t ev_masks[N_EV_MASKS];    /**< Mask to isolate events that
					   * belong to us */
	uint8_t pool_id;
	rx_config_t rx_config;
	odp_buffer_ring_t ring;

	enum {
		RX_IFCE_DOWN,
		RX_IFCE_UP
	} status;
} rx_ifce_t;

typedef struct {
	odp_packet_t spares[PKT_BURST_SZ + MAX_RX];
	int n_spares;
	int n_rx;
} rx_pool_t;

/** Per thread data */
typedef struct {
	uint8_t min_mask;               /**< Rank of minimum non-null mask */
	uint8_t max_mask;               /**< Rank of maximum non-null mask */
	uint64_t ev_masks[4];           /**< Mask to isolate events that belong
					 *   to us */
	rx_ifce_th_t ifce[MAX_RX_IF];
	rx_pool_t pools[ODP_CONFIG_POOLS];

#ifdef K1_NODEOS
	pthread_t thr;
#else
	utask_t task;
#endif

	enum {
		RX_TH_RESET,
		RX_TH_ON,
		RX_TH_OFF
	} status;
} rx_th_t;

typedef struct rx_thread {
	odp_atomic_u64_t update_id;
	odp_rwlock_t lock;		/**< entry RW lock */
	uint32_t if_opened;

	odp_packet_t drop_pkt;          /**< ODP Packet used to temporary store
					 *   dropped data */
	uint8_t *drop_pkt_ptr;          /**< Pointer to drop_pkt buffer */
	uint32_t drop_pkt_len;          /**< Size of drop_pkt buffer in bytes */

	rx_tag_t tag[N_RX];        /**<  */
	rx_ifce_t ifce[MAX_RX_IF];
	rx_th_t th[MAX_RX_THR];

	int destroy;
} rx_thread_t;

static rx_thread_t rx_hdl;

static inline void _print_eth_header(const mppa_ethernet_header_t * _hdr)
{
	mppa_ethernet_header_t hdr;
	hdr.timestamp = LOAD_U64(_hdr->timestamp);
	hdr.info.dword = LOAD_U64(_hdr->info.dword);
	printf("Timestamp: %llx\n\tSize: %d Hash: %d\n\tLane: %d Io: %d\n\tRule: %d Id: %d\n",
	       hdr.timestamp,
	       hdr.info._.pkt_size, hdr.info._.hash_key,
	       hdr.info._.lane_id, hdr.info._.io_id,
	       hdr.info._.rule_id, hdr.info._.pkt_id);
}

static inline int MIN(int a, int b)
{
	return a > b ? b : a;
}

static inline int MAX(int a, int b)
{
	return b > a ? b : a;
}

static int _configure_rx(rx_config_t *rx_config, int rx_id)
{
	odp_packet_t pkt = _odp_packet_alloc(rx_config->pool);
	const int dma_if = 0;

	if (pkt == ODP_PACKET_INVALID)
		return -1;

	odp_packet_hdr_t *pkt_hdr = odp_packet_hdr(pkt);
	rx_hdl.tag[rx_id].pkt = pkt;

	int ret;
	mppa_noc_dnoc_rx_configuration_t conf = {
		.buffer_base = (unsigned long)pkt_hdr->buf_hdr.addr +
		rx_config->pkt_offset,
		.buffer_size = pkt_hdr->buf_hdr.size -
		1 * rx_config->header_sz,
		.current_offset = 0,
		.event_counter = 0,
		.item_counter = 1,
		.item_reload = 1,
		.reload_mode = MPPA_DNOC_DECR_NOTIF_RELOAD_ETH,
		.activation = 0x3,
		.counter_id = 0
	};

	ret = mppa_noc_dnoc_rx_configure(dma_if, rx_id, conf);
	ODP_ASSERT(!ret);

	ret = mppa_dnoc[dma_if]->rx_queues[rx_id].
		get_drop_pkt_nb_and_activate.reg;
	mppa_noc_enable_event(dma_if,
			      MPPA_NOC_INTERRUPT_LINE_DNOC_RX,
			      rx_id, (1 << BSP_NB_PE_P) - 1);

	return 0;
}

static int _close_rx(rx_config_t *rx_config ODP_UNUSED, int rx_id)
{
	const int dma_if = 0;
	typeof(mppa_dnoc[dma_if]->rx_queues[0]) * const rx_queue =
		&mppa_dnoc[dma_if]->rx_queues[rx_id];

	for (int j = 0; j < 16; ++j)
		rx_queue->activation.reg = 0x0;

	if (rx_hdl.tag[rx_id].pkt != ODP_PACKET_INVALID)
		odp_packet_free_multi(&rx_hdl.tag[rx_id].pkt, 1);

	rx_hdl.tag[rx_id].pkt = ODP_PACKET_INVALID;

	mppa_noc_dnoc_rx_free(dma_if, rx_id);

	return 0;
}

/*
 * Returns 1 if a < b
 */
static inline int _buffer_ordered_cmp(const odp_buffer_hdr_t *a,
				      const odp_buffer_hdr_t *b)
{
	if (BUFFER_ORDER_BITS == 64)
		return a->order < b->order;

	/* Order may be wrapping, check that they are not too far from each other */
	unsigned long long diff = llabs(b->order - a->order);
	if (diff  > (1ULL << (BUFFER_ORDER_BITS - 1))) {
		/*  Diff is too big, the order is reversed */
		return b->order < a->order;
	} else {
		return a->order < b->order;
	}
}

static void _sort_buffers(odp_buffer_hdr_t **head, odp_buffer_hdr_t ***tail,
			  const unsigned n_buffers)
{
	odp_buffer_hdr_t *sorted  = (*head);
	odp_buffer_hdr_t **last_insertion = head;
	odp_buffer_hdr_t *buf, *next;
	unsigned count;

	for (count = 1, buf = sorted->next; buf && count < n_buffers; count+=1, buf = next) {
		next = buf->next;
		if (odp_unlikely(_buffer_ordered_cmp(buf, sorted))){
			/* Remove unsorted elnt */
			sorted->next = buf->next;

			/* Out of order */
			odp_buffer_hdr_t **prev = last_insertion;
			odp_buffer_hdr_t *ptr = *last_insertion;

			if (odp_unlikely(_buffer_ordered_cmp(buf, ptr))) {
				/* We need to be inserted before the last insertion point */
				prev = head;
				ptr = *head;
			}
			/* Find slot */
			for(; ptr && _buffer_ordered_cmp(ptr, buf); prev=&ptr->next, ptr = ptr->next){}
			/* Insert element in its rightful place */
			*prev = buf;
			buf->next = ptr;
			last_insertion = prev;
		} else {
			sorted = buf;
		}
	}
	/* Update tail ptr */
	*tail = &(sorted->next);
}


static uint64_t _reload_rx(int th_id, int rx_id, uint64_t *mask)
{
	const int dma_if = 0;
	const int pktio_id = rx_hdl.tag[rx_id].pktio_id;
	rx_ifce_th_t *if_th = &rx_hdl.th[th_id].ifce[pktio_id];
	rx_ifce_t *ifce = &rx_hdl.ifce[pktio_id];
	const rx_config_t * rx_config = &rx_hdl.ifce[pktio_id].rx_config;
	rx_pool_t * rx_pool = &rx_hdl.th[th_id].
		pools[rx_hdl.ifce[pktio_id].pool_id];
	int mapped_pkt = 0;
	uint64_t _mask = (*mask) & ifce->ev_masks[rx_id / 64];
	int n_events = __builtin_k1_cbs(_mask & 0xFFFFFFFF) + __builtin_k1_cbs(_mask >> 32) + 1;


	if (rx_config->flow_controlled) {
		if (n_events > rx_pool->n_spares) {
			/* Alloc */
			pool_entry_t * p_entry = (pool_entry_t*) rx_config->pool;
			struct pool_entry_s *entry = &p_entry->s;

			rx_pool->n_spares +=
				get_buf_multi(entry,
					      (odp_buffer_hdr_t **)(rx_pool->spares + rx_pool->n_spares),
					      MIN(MAX(n_events, rx_pool->n_rx), PKT_BURST_SZ));
		}
		if (n_events > rx_pool->n_spares) {
			/* Clear all bits of this pktio as
			 * we don't want to break order */
			*mask = (*mask) & ~(ifce->ev_masks[rx_id / 64]);
			return 0;
		}
	} else if (odp_unlikely(!rx_pool->n_spares)) {
		/* Alloc */
		pool_entry_t * p_entry = (pool_entry_t*) rx_config->pool;
		struct pool_entry_s *entry = &p_entry->s;
		rx_pool->n_spares =
			get_buf_multi(entry,
				      (odp_buffer_hdr_t **)rx_pool->spares,
				      MIN(rx_pool->n_rx, PKT_BURST_SZ));
	}

	mppa_dnoc[dma_if]->rx_queues[rx_id].event_lac.hword;
	odp_packet_t pkt = rx_hdl.tag[rx_id].pkt;
	odp_packet_t newpkt = ODP_PACKET_INVALID;

	if (odp_unlikely(pkt == ODP_PACKET_INVALID)){
		if (rx_hdl.tag[rx_id].broken) {
			rx_hdl.tag[rx_id].broken = false;
			if_th->dropped_pkts++;
		} else {
			if_th->oom_pkts++;
		}
	} else {
		/* Move timestamp to order so it can be sorted in the ring buf */
		odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t*)pkt;
		const mppa_ethernet_header_t *header;
		header = (const mppa_ethernet_header_t*)
			(((uint8_t *)pkt_hdr->buf_hdr.addr) +
			 rx_config->pkt_offset);

		union mppa_ethernet_header_info_t info;
		info.dword = LOAD_U64(header->info);
		pkt_hdr->buf_hdr.order = info._.pkt_id;
		if (info._.pkt_size < sizeof(*header)) {
			/* Probably a spurious EOT. */
			STORE_U64(header->info, 0ULL);
			newpkt = pkt;
			pkt = ODP_PACKET_INVALID;
			if_th->dropped_pkts++;
			mapped_pkt = 1;
		}
	}

	typeof(mppa_dnoc[dma_if]->rx_queues[0]) * const rx_queue =
		&mppa_dnoc[dma_if]->rx_queues[rx_id];

	if (mapped_pkt) {
		/* We had a spurious EOT or a broken pkt.
		 * Leave in in place and juste restore the current offset */
		rx_queue->current_offset.reg = 0ULL;
	} else if (odp_unlikely(!rx_pool->n_spares)) {
		/* No packets were available. Map small dirty
		 * buffer to receive NoC packet but drop
		 * the frame */
		rx_queue->buffer_base.dword =
			(unsigned long)rx_hdl.drop_pkt_ptr;
		rx_queue->buffer_size.dword = rx_hdl.drop_pkt_len;
		/* We willingly do not change the offset here as we want
		 * to spread DMA Rx within the drop_pkt buffer */
	} else {
		/* Map the buffer in the DMA Rx */
		odp_packet_hdr_t *pkt_hdr;

		newpkt = rx_pool->spares[--rx_pool->n_spares];
		pkt_hdr = odp_packet_hdr(newpkt);

		rx_queue->buffer_base.dword = (unsigned long)
			((uint8_t *)(pkt_hdr->buf_hdr.addr) +
			 rx_config->pkt_offset);

		/* Rearm the DMA Rx and check for droppped packets */
		rx_queue->current_offset.reg = 0ULL;

		rx_queue->buffer_size.dword = pkt_hdr->buf_hdr.size -
			1 * rx_config->header_sz;
	}

	int dropped = rx_queue->
		get_drop_pkt_nb_and_activate.reg;

	if (odp_unlikely(dropped)) {
		/* We dropped some we need to try and
		 * drop more to get better */

		/* Put back a dummy buffer.
		 * We will drop those next ones anyway ! */
		if (newpkt != ODP_PACKET_INVALID) {
			rx_queue->buffer_base.dword =
				(unsigned long)rx_hdl.drop_pkt_ptr;
			rx_queue->buffer_size.dword = rx_hdl.drop_pkt_len;
			/* Value was still in rx_pool. No need to store it again */
			rx_pool->n_spares++;
		}
		/* Really force those values.
		 * Item counter must be 2 in this case. */
		int j;

		for (j = 0; j < 16; ++j)
			rx_queue->item_counter.reg = 2;
		for (j = 0; j < 16; ++j)
			rx_queue->activation.reg = 0x1;

		/* +1 for the extra item counter we just configure.
		 * The second item counter
		 * will be counted by the pkt == ODP_PACKET_INVALID */
		if_th->dropped_pkts += dropped + 1;
		rx_hdl.tag[rx_id].broken = true;

		/* We didn't actually used the spare one */
		rx_hdl.tag[rx_id].pkt = ODP_PACKET_INVALID;
	} else {
		rx_hdl.tag[rx_id].pkt = newpkt;
	}

	if (odp_likely(pkt != ODP_PACKET_INVALID)) {
		rx_buffer_list_t * hdr_list = &if_th->hdr_list;

		if_th->recv_pkts++;
		*(hdr_list->tail) = (odp_buffer_hdr_t *)pkt;
		hdr_list->tail = &((odp_buffer_hdr_t *)pkt)->next;
		hdr_list->count++;
		return 1ULL << pktio_id;
	}

	return 0;
}

static void _poll_masks(int th_id)
{
	int i;
	uint64_t mask;

	const int dma_if = 0;
	const rx_th_t * const th = &rx_hdl.th[th_id];
	const int min_mask =  th->min_mask;
	const int max_mask =  th->max_mask;
	uint64_t if_mask = 0ULL;

	for (int iter = 0; iter < N_ITER_LOCKED; ++iter) {

		for (i = min_mask; i <= max_mask; ++i) {
			mask = mppa_dnoc[dma_if]->rx_global.events[i].dword &
				th->ev_masks[i];

			if (mask == 0ULL)
				continue;

			/* We have an event */
			while (mask != 0ULL) {
				const int mask_bit = __k1_ctzdl(mask);
				const int rx_id = mask_bit + i * 64;

				mask = mask ^ (1ULL << mask_bit);
				if_mask |=  _reload_rx(th_id, rx_id, &mask);
			}
		}

		if ((iter & FLUSH_PERIOD) == FLUSH_PERIOD) {
			uint64_t if_mask_incomplete = 0ULL;
			while (if_mask) {
				i = __builtin_k1_ctzdl(if_mask);
				if_mask ^= (1ULL << i);

				rx_buffer_list_t * hdr_list =
					&rx_hdl.th[th_id].ifce[i].hdr_list;

				if (hdr_list->tail == &hdr_list->head)
					continue;

				/* Do not sort if there is more than 1 rx thread.
				 * order will be broken anyway */
				if (odp_global_data.sort_buffers)
					_sort_buffers(&hdr_list->head, &hdr_list->tail,
						      hdr_list->count);
				hdr_list->count =
					odp_buffer_ring_push_list(&rx_hdl.ifce[i].ring,
								  &hdr_list->head,
								  hdr_list->count);
				if (!hdr_list->count) {
					/* All were flushed */
					hdr_list->tail = &hdr_list->head;
				} else {
					/* Not all buffers were flushed to the ring */
					if_mask_incomplete = 1ULL << i;
				}

			}
			if_mask = if_mask_incomplete;
		}

	}
	return;
}

static void *_rx_thread_start(void *arg)
{
	int th_id = (unsigned long)(arg);
	for (int i = 0; i < MAX_RX_IF; ++i) {
		rx_buffer_list_t * hdr_list =
			&rx_hdl.th[th_id].ifce[i].hdr_list;
		hdr_list->tail = &hdr_list->head;
		hdr_list->count = 0;
	}
	uint64_t last_update= -1LL;

	rx_hdl.th[th_id].status = RX_TH_ON;

	while (1) {
		odp_rwlock_read_lock(&rx_hdl.lock);
		uint64_t update_id = odp_atomic_load_u64(&rx_hdl.update_id);
		if (update_id != last_update) {
			__builtin_k1_dinval();
			last_update = update_id;
		}

		if (rx_hdl.destroy)
			break;

		if (!rx_hdl.if_opened){
			odp_rwlock_read_unlock(&rx_hdl.lock);
			__k1_cpu_backoff(10000);
			continue;
		}

		_poll_masks(th_id);

		for (int i = 0; i < ODP_CONFIG_POOLS; ++i) {
			if (!rx_hdl.th[th_id].pools[i].n_spares)
				continue;
			/* Free all the spares pre allocated */
			odp_packet_free_multi(rx_hdl.th[th_id].pools[i].spares,
					  rx_hdl.th[th_id].pools[i].n_spares);
			rx_hdl.th[th_id].pools[i].n_spares = 0;
		}

		odp_rwlock_read_unlock(&rx_hdl.lock);
	}

	/* Cleanup and exit */


	rx_hdl.th[th_id].status = RX_TH_OFF;
	__k1_wmb();
	return NULL;
}

int rx_thread_link_open(rx_config_t *rx_config, int n_ports,
			int rr_policy, int rr_offset,
			int min_rx, int max_rx)
{
	const int dma_if = 0;
	if (rx_config->pktio_id >= MAX_RX_IF) {
		ODP_ERR("Pktio ID too large\n");
		return -1;
	}
	if (n_ports > MAX_RX) {
		ODP_ERR("asking for too many Rx port");
		return -1;
	}
	rx_ifce_t *ifce =
		&rx_hdl.ifce[rx_config->pktio_id];
	char loopq_name[ODP_QUEUE_NAME_LEN];
	int n_rx = 0, first_rx;

	snprintf(loopq_name, sizeof(loopq_name), "%d-pktio_rx",
		 rx_config->pktio_id);

	if (min_rx >= 0 || max_rx >= 0) {
		ODP_ASSERT(min_rx >= 0);
		ODP_ASSERT(max_rx >= 0);
		ODP_ASSERT(n_ports == max_rx - min_rx + 1);
	}

	/*
	 * Allocate contiguous RX ports
	 */
	if (min_rx < 0)
		min_rx = 0;
	if (max_rx < 0)
		max_rx = MPPA_DNOC_RX_QUEUES_NUMBER - 1;
	for (first_rx = min_rx; first_rx < max_rx + 2 - n_ports; ++first_rx) {
		/* If the ifce has "flow control", make sure all events are in the
		 * dword in DNoC event register */
		if (rx_config->flow_controlled &&
		    first_rx / 64 != (first_rx + n_rx) / 64)
			continue;

		for (n_rx = 0; n_rx < n_ports; ++n_rx) {
			mppa_noc_ret_t ret;
			ret = mppa_noc_dnoc_rx_alloc(dma_if,
						     first_rx + n_rx);
			if (ret != MPPA_NOC_RET_SUCCESS)
				break;
		}
		if (n_rx < n_ports) {
			n_rx--;
			for ( ; n_rx >= 0; --n_rx) {
				mppa_noc_dnoc_rx_free(dma_if,
						      first_rx + n_rx);
			}
		} else {
			break;
		}
	}
	if (n_rx < n_ports) {
		ODP_ERR("failed to allocate %d contiguous Rx ports\n", n_ports);
		ODP_ASSERT(n_rx == 0);
		return -1;
	}

	rx_config->min_port = first_rx;
	rx_config->max_port = first_rx + n_rx - 1;
	rx_config->pkt_offset = ((pool_entry_t *)rx_config->pool)->s.headroom -
		rx_config->header_sz;

	/*
	 * Compute event mask to detect events on our own tags later
	 */
	const unsigned nrx_per_th = (n_ports + odp_global_data.n_rx_thr - 1) /
		odp_global_data.n_rx_thr;
	uint64_t ev_masks[odp_global_data.n_rx_thr][N_EV_MASKS];
	int i;
	uint32_t th_id;

	for (i = 0; i < 4; ++i) {
		for (th_id = 0; th_id < odp_global_data.n_rx_thr; ++th_id) {
			ifce->ev_masks[i] = 0ULL;
			ev_masks[th_id][i] = 0ULL;
		}
	}

	if(rr_policy < 0) {
		const uint64_t full_mask = 0xffffffffffffffffULL;

		/* Each thread has a contiguous 1 / odp_global_data.n_rx_thr
		 * nth of the thread pool */
		for (th_id = 0; th_id < odp_global_data.n_rx_thr; ++th_id) {
			int min_port = th_id * nrx_per_th + rx_config->min_port;
			int max_port = (th_id + 1) * nrx_per_th +
				rx_config->min_port - 1;
			if (max_port > rx_config->max_port)
				max_port = rx_config->max_port;
			for (i = 0; i < 4; ++i) {
				if (min_port >= (i + 1) * 64 || max_port < i * 64) {
					ev_masks[th_id][i] = 0ULL;
					continue;
				}
				uint8_t local_min = MAX(i * 64, min_port) - (i * 64);
				uint8_t local_max =
					MIN((i + 1) * 64 - 1, max_port) - (i * 64);

				ev_masks[th_id][i] =
					(/* Trim the upper bits */
					 (
					  /* Trim the lower bits */
					  full_mask >> (local_min)
					  )
					 /* Realign back + trim the top */
					 << (local_min + 63 - local_max)
					 ) /* Realign again */ >> (63 - local_max);

				if (ev_masks[th_id][i] != 0)
					ifce->ev_masks[i] |= ev_masks[th_id][i];
			}
		}
	} else {
		/* Each thread picks rr_policy Rx every odp_global_data.n_rx_thr */
		for (int port = rx_config->min_port, th_id = 0;
		     port <= rx_config->max_port; ++port, ++th_id){
			int th = ((th_id + rr_offset) / rr_policy) % odp_global_data.n_rx_thr;

			const unsigned word = port / (8 * sizeof(ev_masks[0][0]));
			const unsigned offset = port % ( 8 * sizeof(ev_masks[0][0]));
			ev_masks[th][word] |= 0x1ULL << offset;
		}
		for (i = 0; i < 4; ++i) {
			for (th_id = 0; th_id < odp_global_data.n_rx_thr; ++th_id) {
				ifce->ev_masks[i] |= ev_masks[th_id][i];
			}
		}
	}

	/* Setup buffer ring */
	int ring_size = 2 * n_rx;
	if (ring_size < MIN_RING_SIZE)
		ring_size = MIN_RING_SIZE;
	void * addr = malloc(ring_size * sizeof(odp_buffer_hdr_t*));
	if (!addr) {
		ODP_ERR("Failed to allocate Ring buffer");
		return -1;
	}

	odp_buffer_ring_init(&ifce->ring, addr, ring_size);
	rx_config->ring = &ifce->ring;

	/* Copy config to Thread data */
	memcpy(&ifce->rx_config, rx_config, sizeof(*rx_config));
	ifce->pool_id = pool_to_id(rx_config->pool);

	for (i = rx_config->min_port; i <= rx_config->max_port; ++i)
		_configure_rx(rx_config, i);

	/* Push Context to handling threads */
	odp_rwlock_write_lock(&rx_hdl.lock);
	{
		INVALIDATE(&rx_hdl);
		for (i = rx_config->min_port; i <= rx_config->max_port; ++i)
			rx_hdl.tag[i].pktio_id = rx_config->pktio_id;

		/* Allocate one packet to put all the broken ones
		 * coming from the NoC */
		if (rx_hdl.drop_pkt == ODP_PACKET_INVALID) {
			odp_packet_hdr_t *hdr;

			rx_hdl.drop_pkt = _odp_packet_alloc(rx_config->pool);
			if (rx_hdl.drop_pkt == ODP_PACKET_INVALID) {
				ODP_ERR("failed to allocate a packet\n");
				for ( ; n_rx >= 0; --n_rx) {
					mppa_noc_dnoc_rx_free(dma_if,
							      first_rx + n_rx);
				}
				return -1;
			}
			hdr = odp_packet_hdr(rx_hdl.drop_pkt);
			rx_hdl.drop_pkt_ptr = hdr->buf_hdr.addr;
			rx_hdl.drop_pkt_len = hdr->frame_len;
		}

		for (uint32_t i = 0; i < odp_global_data.n_rx_thr; ++i) {
			rx_th_t *th = &rx_hdl.th[i];

			th->pools[ifce->pool_id].n_rx += nrx_per_th;
			memset(&th->ifce[rx_config->pktio_id], sizeof(rx_ifce_th_t), 0);

			for (int j = 0; j < 4; ++j) {
				th->ev_masks[j] |= ev_masks[i][j];
				if (ev_masks[i][j]) {
					if (j < th->min_mask)
						th->min_mask = j;
					if (j > th->max_mask)
						th->max_mask = j;
				}
			}
		}

		ifce->status = RX_IFCE_UP;
		rx_hdl.if_opened++;
		odp_atomic_add_u64(&rx_hdl.update_id, 1ULL);
	}
	odp_rwlock_write_unlock(&rx_hdl.lock);
	return first_rx;
}

int rx_thread_link_close(uint8_t pktio_id)
{
	int i;
	rx_ifce_t *ifce =
		&rx_hdl.ifce[pktio_id];

	odp_rwlock_write_lock(&rx_hdl.lock);
	{
		INVALIDATE(&rx_hdl);

		if (ifce->status == RX_IFCE_DOWN) {
			odp_rwlock_write_unlock(&rx_hdl.lock);
			return 0;
		}
		for (i = ifce->rx_config.min_port;
		     i <= ifce->rx_config.max_port; ++i)
			rx_hdl.tag[i].pktio_id = -1;

		int n_ports = ifce->rx_config.max_port -
			ifce->rx_config.min_port + 1;
		const unsigned nrx_per_th = n_ports / odp_global_data.n_rx_thr;

		for (int i = ifce->rx_config.min_port;
		     i <= ifce->rx_config.max_port; ++i)
			_close_rx(&ifce->rx_config, i);

		ifce->rx_config.pool = ODP_POOL_INVALID;
		ifce->rx_config.min_port = -1;
		ifce->rx_config.max_port = -1;

		for (uint32_t i = 0; i < odp_global_data.n_rx_thr; ++i) {
			rx_th_t *th = &rx_hdl.th[i];

			th->pools[ifce->pool_id].n_rx -= nrx_per_th;

			th->min_mask = (uint8_t) -1;
			th->max_mask = 0;

			for (int j = 0; j < 4; ++j) {
				th->ev_masks[j] &= ~ifce->ev_masks[j];
				if (th->ev_masks[j]) {
					if (j < th->min_mask)
						th->min_mask = j;
					if (j > th->max_mask)
						th->max_mask = j;
				}
			}
		}

		odp_atomic_add_u64(&rx_hdl.update_id, 1ULL);

		{
			/** free all the buffers */
			odp_buffer_hdr_t * buffers[10];
			int nbufs;
			while ((nbufs = odp_buffer_ring_get_multi(&ifce->ring,
								  buffers, 10, 0,
								  NULL)) > 0) {
				odp_buffer_free_multi((odp_buffer_t*)buffers,
						      nbufs);
			}
			free(ifce->ring.buf_ptrs);

		}
		ifce->status = RX_IFCE_DOWN;
		rx_hdl.if_opened--;
		/* No more interface open. */
		if (rx_hdl.if_opened == 0 &&
		    rx_hdl.drop_pkt != ODP_PACKET_INVALID) {
			odp_packet_free_multi(&rx_hdl.drop_pkt, 1);
			rx_hdl.drop_pkt = ODP_PACKET_INVALID;
		}
	}
	odp_rwlock_write_unlock(&rx_hdl.lock);



	return 0;
}
static int g_rx_thread_init = 0;

int rx_thread_init(void)
{
	if (g_rx_thread_init)
		return 0;

	INVALIDATE(&rx_hdl);

	odp_rwlock_init(&rx_hdl.lock);
	odp_atomic_init_u64(&rx_hdl.update_id, 0ULL);
	rx_hdl.destroy = 0;
	__k1_wmb();
	for (uint32_t i = 0; i < odp_global_data.n_rx_thr; ++i) {
		/* Start threads */

		unsigned pe_id = BSP_NB_PE_P - 2 * i - 1;
#ifdef K1_NODEOS
		odp_cpumask_t thd_mask;
		pthread_attr_t attr;
		odp_cpumask_zero(&thd_mask);
		odp_cpumask_set(&thd_mask, pe_id);

		pthread_attr_init(&attr);
		pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t),
					    &thd_mask.set);

		if (pthread_create(&rx_hdl.th[i].thr, &attr,
				   _rx_thread_start,
				   (void *)(unsigned long)(i)))
			ODP_ABORT("Thread failed");
#else
		if (utask_start_pe(&rx_hdl.th[i].task, _rx_thread_start,
				   (void *)(unsigned long)(i),
				   pe_id))
			ODP_ABORT("Thread failed");
#endif
	}

	g_rx_thread_init = 1;

	return 0;
}

int rx_thread_destroy(void)
{
	/* If it's already destroyed, don't bother */
	if (g_rx_thread_init == 0)
		return 0;

	odp_rwlock_write_lock(&rx_hdl.lock);
	INVALIDATE(&rx_hdl);

	for (int ifce = 0; ifce < MAX_RX_IF; ++ifce) {
		if (rx_hdl.ifce[ifce].status != RX_IFCE_DOWN) {
			odp_rwlock_write_unlock(&rx_hdl.lock);
			return -1;
		}
	}

	rx_hdl.destroy = 1;
	odp_atomic_add_u64(&rx_hdl.update_id, 1ULL);
	odp_rwlock_write_unlock(&rx_hdl.lock);

	for (uint32_t i = 0; i < odp_global_data.n_rx_thr; ++i) {
#ifdef K1_NODEOS
		pthread_join(rx_hdl.th[i].thr, NULL);
#else
		utask_join(rx_hdl.th[i].task, NULL);
#endif
	}
	g_rx_thread_init = 0;
	return 0;
}

int rx_thread_fetch_stats(uint8_t pktio_id, uint64_t *dropped,
			  uint64_t *oom)
{
	for (uint32_t i = 0; i < odp_global_data.n_rx_thr; ++i) {
		*oom += LOAD_U64(rx_hdl.th[i].ifce[pktio_id].oom_pkts);
		*dropped += LOAD_U64(rx_hdl.th[i].ifce[pktio_id].dropped_pkts);
	}

	return 0;
}
