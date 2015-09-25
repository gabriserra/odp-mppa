/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp_packet_io_internal.h>
#include <odp/thread.h>
#include <odp/cpumask.h>
#include "HAL/hal/hal.h"
#include <odp/errno.h>
#include <errno.h>
#include <mppa_noc.h>

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

typedef struct {
	odp_packet_t pkt;
	uint8_t broken;
	uint8_t pktio_id;
} rx_tag_t;

/** Per If data */
typedef struct {
	odp_buffer_hdr_t  *head;
	odp_buffer_hdr_t **tail;
	int count;
} rx_buffer_list_t;

typedef struct {
	rx_buffer_list_t hdr_list;
	uint64_t dropped_pkts;
	uint64_t oom_pkts;
} rx_ifce_th_t;

typedef struct {
	uint64_t ev_masks[N_EV_MASKS];    /**< Mask to isolate events that
					   * belong to us */
	uint8_t pool_id;
	rx_config_t rx_config;
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
} rx_th_t;

typedef struct rx_thread {
	odp_atomic_u64_t update_id;
	odp_rwlock_t lock;		/**< entry RW lock */

	odp_packet_t drop_pkt;          /**< ODP Packet used to temporary store
					 *   dropped data */
	uint8_t *drop_pkt_ptr;          /**< Pointer to drop_pkt buffer */
	uint32_t drop_pkt_len;          /**< Size of drop_pkt buffer in bytes */

	rx_tag_t tag[N_RX];        /**<  */
	rx_ifce_t ifce[MAX_RX_IF];
	rx_th_t th[N_RX_THR];
} rx_thread_t;

static rx_thread_t rx_hdl;

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
	uint32_t len;
	uint8_t * base_addr = packet_map(pkt_hdr, 0, &len);
	mppa_noc_dnoc_rx_configuration_t conf = {
		.buffer_base = (unsigned long)base_addr - rx_config->header_sz,
		.buffer_size = len + rx_config->header_sz,
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

static int _reload_rx(int th_id, int rx_id)
{
	const int dma_if = 0;
	const int pktio_id = rx_hdl.tag[rx_id].pktio_id;
	rx_ifce_th_t *if_th = &rx_hdl.th[th_id].ifce[pktio_id];
	const rx_config_t * rx_config = &rx_hdl.ifce[pktio_id].rx_config;
	rx_pool_t * rx_pool = &rx_hdl.th[th_id].
		pools[rx_hdl.ifce[pktio_id].pool_id];

	mppa_dnoc[dma_if]->rx_queues[rx_id].event_lac.hword;

	if (odp_unlikely(!rx_pool->n_spares)) {
		/* Alloc */
		pool_entry_t * p_entry = (pool_entry_t*) rx_config->pool;
		struct pool_entry_s *entry = &p_entry->s;

		rx_pool->n_spares =
			get_buf_multi(entry,
				      (odp_buffer_hdr_t **)rx_pool->spares,
				      MIN(rx_pool->n_rx, PKT_BURST_SZ));
	}

	odp_packet_t pkt = rx_hdl.tag[rx_id].pkt;
	odp_packet_t newpkt = ODP_PACKET_INVALID;

	if (odp_unlikely(pkt == ODP_PACKET_INVALID)){
		if (rx_hdl.tag[rx_id].broken) {
			rx_hdl.tag[rx_id].broken = false;
			if_th->dropped_pkts++;
		} else {
			if_th->oom_pkts++;
		}
	}

	typeof(mppa_dnoc[dma_if]->rx_queues[0]) * const rx_queue =
		&mppa_dnoc[dma_if]->rx_queues[rx_id];

	if (odp_unlikely(!rx_pool->n_spares)) {
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

		rx_queue->buffer_size.dword = pkt_hdr->frame_len +
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

		if (pkt != ODP_PACKET_INVALID) {
			/* If we pulled a packet, it has to be destroyed.
			 * Mark it as parsed with frame_len error */
			rx_pool->spares[rx_pool->n_spares++] = pkt;
			pkt = ODP_PACKET_INVALID;
		}
		return 0;
	} else {
		rx_hdl.tag[rx_id].pkt = newpkt;

		if (odp_likely(pkt != ODP_PACKET_INVALID)) {
			rx_buffer_list_t * hdr_list = &if_th->hdr_list;

			*(hdr_list->tail) = (odp_buffer_hdr_t *)pkt;
			hdr_list->tail = &((odp_buffer_hdr_t *)pkt)->next;
			return 1 << pktio_id;
		}
		return 0;
	}
}

static void _poll_masks(int th_id)
{
	int i;
	uint64_t mask;

	const int dma_if = 0;
	const rx_th_t * const th = &rx_hdl.th[th_id];
	const int min_mask =  th->min_mask;
	const int max_mask =  th->max_mask;
	for (int iter = 0; iter < N_ITER_LOCKED; ++iter) {
		int if_mask = 0;

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
				if_mask |=  _reload_rx(th_id, rx_id);
			}
		}

		while (if_mask) {
			i = __builtin_k1_ctz(if_mask);
			if_mask ^= (1 << i);

			queue_entry_t *qentry;
			rx_buffer_list_t * hdr_list = &rx_hdl.th[th_id].ifce[i].hdr_list;

			if (hdr_list->tail == &hdr_list->head)
				continue;
			qentry = queue_to_qentry(rx_hdl.ifce[i].rx_config.queue);

			odp_buffer_hdr_t * tail = (odp_buffer_hdr_t*)
				((uint8_t*)hdr_list->tail - ODP_OFFSETOF(odp_buffer_hdr_t, next));
			tail->next = NULL;
			queue_enq_list(qentry, hdr_list->head, tail);

			hdr_list->tail = &hdr_list->head;
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
	while (1) {
		odp_rwlock_read_lock(&rx_hdl.lock);
		uint64_t update_id = odp_atomic_load_u64(&rx_hdl.update_id);
		if (update_id != last_update) {
			INVALIDATE(&rx_hdl);
			last_update = update_id;
		}
		_poll_masks(th_id);
		odp_rwlock_read_unlock(&rx_hdl.lock);
	}
	return NULL;
}

int rx_thread_link_open(rx_config_t *rx_config, int n_ports)
{
	const int dma_if = 0;
	if (n_ports > MAX_RX) {
		ODP_ERR("asking for too many Rx port");
		return -1;
	}

	rx_ifce_t *ifce =
		&rx_hdl.ifce[rx_config->pktio_id];
	char loopq_name[ODP_QUEUE_NAME_LEN];

	snprintf(loopq_name, sizeof(loopq_name), "%d-pktio_rx",
		 rx_config->pktio_id);
	rx_config->queue = odp_queue_create(loopq_name, ODP_QUEUE_TYPE_POLL,
					    NULL);
	if (rx_config->queue == ODP_QUEUE_INVALID) {
		ODP_ERR("ODP rx init failed to alloc a queue");
		return -1;
	}

	/*
	 * Allocate contiguous RX ports
	 */
	int n_rx, first_rx;

	for (first_rx = 0; first_rx <  MPPA_DNOC_RX_QUEUES_NUMBER - n_ports;
	     ++first_rx) {
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
		ODP_ASSERT(n_rx == 0);
		ODP_ERR("failed to allocate %d contiguous Rx ports\n", n_ports);
		return -1;
	}

	rx_config->min_port = first_rx;
	rx_config->max_port = first_rx + n_rx - 1;
	rx_config->pkt_offset = ((pool_entry_t *)rx_config->pool)->s.headroom -
		rx_config->header_sz;
	/*
	 * Compute event mask to detect events on our own tags later
	 */
	const uint64_t full_mask = 0xffffffffffffffffULL;
	const unsigned nrx_per_th = n_ports / N_RX_THR;
	uint64_t ev_masks[N_RX_THR][N_EV_MASKS];
	int i;

	for (i = 0; i < 4; ++i)
		ifce->ev_masks[i] = 0ULL;

	for (int th_id = 0; th_id < N_RX_THR; ++th_id) {
		int min_port = th_id * nrx_per_th + rx_config->min_port;
		int max_port = (th_id + 1) * nrx_per_th +
			rx_config->min_port - 1;

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

	/* Copy config to Thread data */
	memcpy(&ifce->rx_config, rx_config, sizeof(*rx_config));
	ifce->pool_id = pool_to_id(rx_config->pool);

	for (i = rx_config->min_port; i <= rx_config->max_port; ++i)
		_configure_rx(rx_config, i);

	/* Push Context to handling threads */
	odp_rwlock_write_lock(&rx_hdl.lock);
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

	for (int i = 0; i < N_RX_THR; ++i) {
		rx_th_t *th = &rx_hdl.th[i];

		th->pools[ifce->pool_id].n_rx += nrx_per_th;

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

	odp_atomic_add_u64(&rx_hdl.update_id, 1ULL);

	odp_rwlock_write_unlock(&rx_hdl.lock);
	return first_rx;
}

int rx_thread_link_close(uint8_t pktio_id)
{
	int i;
	rx_ifce_t *ifce =
		&rx_hdl.ifce[pktio_id];

	odp_rwlock_write_lock(&rx_hdl.lock);
	INVALIDATE(ifce);
	odp_queue_destroy(ifce->rx_config.queue);

	for (i = ifce->rx_config.min_port;
	     i <= ifce->rx_config.max_port; ++i)
		rx_hdl.tag[i].pktio_id = -1;

	int n_ports = ifce->rx_config.max_port -
		ifce->rx_config.min_port + 1;
	const unsigned nrx_per_th = n_ports / N_RX_THR;

	ifce->rx_config.pool = ODP_POOL_INVALID;
	ifce->rx_config.min_port = -1;
	ifce->rx_config.max_port = -1;

	for (int i = 0; i < N_RX_THR; ++i) {
		rx_th_t *th = &rx_hdl.th[i];

		th->pools[ifce->pool_id].n_rx -= nrx_per_th;

		for (int j = 0; j < 4; ++j) {
			th->ev_masks[j] &= ~ifce->ev_masks[j];
			if (!th->ev_masks[j]) {
				if (j == th->min_mask)
					th->min_mask = j + 1;
				if (j == th->max_mask)
					th->max_mask = j - 1;
			}
		}
	}

	odp_atomic_add_u64(&rx_hdl.update_id, 1ULL);

	odp_rwlock_write_unlock(&rx_hdl.lock);

	return 0;
}

int rx_thread_init(void)
{
	odp_rwlock_init(&rx_hdl.lock);
	odp_atomic_init_u64(&rx_hdl.update_id, 0ULL);

	for (int i = 0; i < N_RX_THR; ++i) {
		/* Start threads */

#ifdef K1_NODEOS
		odp_cpumask_t thd_mask;
		pthread_attr_t attr;
		pthread_t thr;

		odp_cpumask_zero(&thd_mask);
		odp_cpumask_set(&thd_mask, BSP_NB_PE_P - i - 1);

		pthread_attr_init(&attr);
		pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t),
					    &thd_mask.set);

		if (pthread_create(&thr, &attr,
				   _rx_thread_start,
				   (void *)(unsigned long)(i)))
			ODP_ABORT("Thread failed");
#else
		utask_t task;

		if (utask_start_pe(&task, _rx_thread_start,
				   (void *)(unsigned long)(i),
				   BSP_NB_PE_P - i - 1))
			ODP_ABORT("Thread failed");
#endif
	}
	return 0;
}
