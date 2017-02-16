#include <stdio.h>
#include <errno.h>
#include <mppa_noc.h>
#include <HAL/hal/core/optimize.h>
#include <HAL/hal/core/power.h>

#include "internal/pcie.h"
#include "internal/netdev.h"
#include "internal/noc2pci.h"

#include "internal/debug.h"

_ODP_STATIC_ASSERT( (MPPA_PCIE_NOC_RX_NB % CREDIT_CHUNK) == 0, "CREDIT_CHUNK__ERROR" );

rx_thread_t g_rx_threads[RX_THREAD_COUNT];

static int reload_rx(rx_iface_t *iface, int rx_id)
{
	mppa_pcie_noc_rx_buf_t *new_buf;
	mppa_pcie_noc_rx_buf_t *old_buf;
	int ret;
	uint16_t events;
	uint16_t pcie_eth_if = iface->rx_cfgs[rx_id].pcie_eth_if;
	uint16_t c2h_q = iface->rx_cfgs[rx_id].c2h_q;
	typeof(mppa_dnoc[iface->iface_id]->rx_queues[0]) * const rx_queue =
		&mppa_dnoc[iface->iface_id]->rx_queues[rx_id];
	mppa_pcie_link_cluster_status_t *clus = iface->rx_cfgs[rx_id].cluster;

	if ( rx_id != clus->c2p_next_tag )
		return -1;

	if ( iface->rx_cfgs[rx_id].broken == 0 ) {
		ret = buffer_ring_get_multi(&g_free_buf_pool, &new_buf, 1, NULL);
		if (ret != 1) {
			dbg_printf("No more free buffer available\n");
			return -1;
		}
		rx_queue->buffer_base.dword = (uintptr_t) new_buf->buf_addr;

		old_buf = iface->rx_cfgs[rx_id].mapped_buf;
		iface->rx_cfgs[rx_id].mapped_buf = new_buf;
		dbg_printf("Adding buf to eth if %d\n", pcie_eth_if);

		/* Fill the pkt details */
		{
			union mpodp_pkt_hdr_info info;
			void * pkt_addr = old_buf->buf_addr;
			old_buf->pkt_count = 0;
			while(1){
				info.dword = __builtin_k1_ldu(pkt_addr + offsetof(struct mpodp_pkt_hdr, info));
				pkt_addr += sizeof(struct mpodp_pkt_hdr);

				old_buf->pkts[old_buf->pkt_count].status = 0;
				old_buf->pkts[old_buf->pkt_count].pkt_addr = (unsigned long)pkt_addr;

				old_buf->pkts[old_buf->pkt_count].len = info._.pkt_size;
				old_buf->pkts[old_buf->pkt_count].data = 0;
				old_buf->pkt_count++;

				if (info._.hash_key & END_OF_PACKETS) {
					old_buf->pkts[old_buf->pkt_count - 1].data = (unsigned long)old_buf;
					break;
				}
				pkt_addr += ( ( info._.pkt_size + sizeof(uint64_t) - 1 ) / sizeof(uint64_t) ) *
					sizeof(uint64_t);
			}
		}

		/* Add previous buffer to full list */
		buffer_ring_push_multi(&g_full_buf_pool[pcie_eth_if][c2h_q], &old_buf, 1, NULL);

		dbg_printf("Reloading rx %d of if %d with buffer %p\n",
				rx_id, iface->iface_id, new_buf);
	}
	else {
		iface->rx_cfgs[rx_id].broken = 0;
	}

	events = mppa_noc_dnoc_rx_lac_event_counter(iface->iface_id, rx_id);
	if (!events) {
		err_printf("Invalid count of events on rx %d\n", rx_id);
		return -1;
	}

	/* Rearm the DMA Rx and check for dropped packets */
	rx_queue->current_offset.reg = 0ULL;
	rx_queue->buffer_size.dword = MPPA_PCIE_MULTIBUF_SIZE;

	int dropped = rx_queue->
		get_drop_pkt_nb_and_activate.reg;

	if (dropped) {
		/* Really force those values.
		 * Item counter must be 2 in this case. */
		int j;
		/* WARNING */
		for (j = 0; j < 16; ++j)
			rx_queue->item_counter.reg = 2;
		for (j = 0; j < 16; ++j)
			rx_queue->activation.reg = 0x1;
		iface->rx_cfgs[rx_id].broken = 1;
		err_printf("Broken Rx tag should not happen!!!\n");
	}

	clus->c2p_next_tag = ( ( clus->c2p_next_tag + 1 ) > clus->c2p_max_rx ) ?
		clus->c2p_min_rx : clus->c2p_next_tag + 1;

	if ( ( ( clus->c2p_next_tag - clus->c2p_min_rx ) % CREDIT_CHUNK ) == 0 ) {
		const unsigned cnoc_tx = pcie_status.link[pcie_eth_if].cnoc_tx;

		clus->c2p_credit += CREDIT_CHUNK;
		ret = mppa_noc_cnoc_tx_configure(iface->iface_id,
						 cnoc_tx,
						 clus->c2p_config,
						 clus->c2p_header);
		assert(ret == MPPA_NOC_RET_SUCCESS);
		mppa_noc_cnoc_tx_push(iface->iface_id, cnoc_tx, clus->c2p_credit);
		dbg_printf("Send %llu credits to %d\n", clus->c2p_credit,
			   clus - &pcie_status.link[pcie_eth_if].cluster[0]);
	}


	return 0;
}


static void mppa_pcie_noc_poll_masks(rx_iface_t *iface)
{
	int i;
	int dma_if = iface->iface_id;
	mppa_noc_dnoc_rx_bitmask_t bitmask;

	bitmask = mppa_noc_dnoc_rx_get_events_bitmask(dma_if);

	for (i = 0; i < 3; ++i) {
		bitmask.bitmask[i] &= iface->ev_mask[i];
		while(bitmask.bitmask[i]) {
			const int mask_bit = __k1_ctzdl(bitmask.bitmask[i]);
			int rx_id = mask_bit + i * 8 * sizeof(bitmask.bitmask[i]);
			bitmask.bitmask[i] &= ~(1ULL << mask_bit);

			reload_rx(iface, rx_id);
		}
	}
}

static volatile int rx_rm_ready[RX_RM_COUNT] = {0};

static void
mppa_pcie_rx_rm_func()
{
	int rm_id = __k1_get_cpu_id();
	rm_id += ((__k1_get_cluster_id() % 64) / 32 * 4);

	rx_thread_t *thread = &g_rx_threads[rm_id - RX_RM_START];
	int iface;

	dbg_printf("RM %d with thread id %d started\n", rm_id, rm_id - RX_RM_START);

	rx_rm_ready[rm_id - RX_RM_START] = 1;
	__k1_wmb();
	while (1) {
		for (iface = 0; iface < IF_PER_THREAD; iface++) {
			mppa_pcie_noc_poll_masks(&thread->iface[iface]);
		}
	}
}

static uint64_t g_stacks[RX_RM_COUNT][RX_RM_STACK_SIZE];

void
pcie_start_rx_rm()
{
	unsigned int rm_num, if_start = 0;
	unsigned int i;

	rx_thread_t *thread;
	for (rm_num = RX_RM_START; rm_num < RX_RM_START + RX_RM_COUNT; rm_num++ ){
		thread = &g_rx_threads[rm_num - RX_RM_START];

		int thread_id = rm_num - RX_RM_START;

		for( i = 0; i < IF_PER_THREAD; i++) {
			thread->iface[i].iface_id = if_start++;
		}

		/* Init with scratchpad size */
		_K1_PE_STACK_ADDRESS[rm_num] = &g_stacks[thread_id][RX_RM_STACK_SIZE - 16];
		_K1_PE_START_ADDRESS[rm_num] = &mppa_pcie_rx_rm_func;
		_K1_PE_ARGS_ADDRESS[rm_num] = 0;

		__builtin_k1_dinval();
		__builtin_k1_wpurge();
		__builtin_k1_fence();

		dbg_printf("Powering RM %d\n", rm_num);
		__k1_poweron(rm_num);
	}

	for (rm_num = RX_RM_START; rm_num < RX_RM_START + RX_RM_COUNT; rm_num++ ){
		while(!rx_rm_ready[rm_num - RX_RM_START]){
			__k1_mb();
		}
		dbg_printf("RM %d started\n", rm_num);
	}
}
