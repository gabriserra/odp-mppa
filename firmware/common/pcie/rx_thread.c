#include <stdio.h>
#include <errno.h>
#include <mppa_noc.h>

#include "internal/pcie.h"
#include "internal/netdev.h"
#include "noc2pci.h"

#include "internal/debug.h"

_ODP_STATIC_ASSERT( (MPPA_PCIE_NOC_RX_NB % CREDIT_CHUNK) == 0, "CREDIT_CHUNK__ERROR" );

rx_thread_t g_rx_threads[RX_THREAD_COUNT];

static int reload_rx(rx_iface_t *iface, int rx_id)
{
	mppa_pcie_noc_rx_buf_t *new_buf;
	mppa_pcie_noc_rx_buf_t *old_buf;
	uint32_t left;
	int ret;
	uint16_t events;
	uint16_t pcie_eth_if = iface->rx_cfgs[rx_id].pcie_eth_if;
	typeof(mppa_dnoc[iface->iface_id]->rx_queues[0]) * const rx_queue =
		&mppa_dnoc[iface->iface_id]->rx_queues[rx_id];
	tx_credit_t *tx_credit = iface->rx_cfgs[rx_id].tx_credit;

	if ( rx_id != tx_credit->next_tag )
		return -1;

	if ( iface->rx_cfgs[rx_id].broken == 0 ) {
		ret = buffer_ring_get_multi(&g_free_buf_pool, &new_buf, 1, &left);
		if (ret != 1) {
			dbg_printf("No more free buffer available\n");
			return -1;
		}
		rx_queue->buffer_base.dword = (uintptr_t) new_buf->buf_addr;

		old_buf = iface->rx_cfgs[rx_id].mapped_buf;
		iface->rx_cfgs[rx_id].mapped_buf = new_buf;
		dbg_printf("Adding buf to eth if %d\n", pcie_eth_if);
		/* Add previous buffer to full list */
		buffer_ring_push_multi(&g_full_buf_pool[pcie_eth_if], &old_buf, 1, &left);

		dbg_printf("Reloading rx %d of if %d with buffer %p\n",
				rx_id, iface->iface_id, new_buf);
	}
	else {
		iface->rx_cfgs[rx_id].broken = 0;
	}

	events = mppa_noc_dnoc_rx_lac_event_counter(iface->iface_id, rx_id);
	if (events != 1) {
		err_printf("Invalid count of events on rx %d: %d\n", rx_id, events);
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

	if ( ( ( rx_id - tx_credit->min_tx_tag ) % CREDIT_CHUNK ) == (CREDIT_CHUNK - 1 ) ) {
		tx_credit->credit += CREDIT_CHUNK;
		ret = mppa_noc_cnoc_tx_configure(iface->iface_id,
				tx_credit->cnoc_tx,
				tx_credit->config,
				tx_credit->header);
		assert(ret == MPPA_NOC_RET_SUCCESS);
		mppa_noc_cnoc_tx_push(iface->iface_id, tx_credit->cnoc_tx, tx_credit->credit);
		dbg_printf("Send %llu credits to %d\n", tx_credit->credit, tx_credit->cluster);
	}


	tx_credit->next_tag = ( ( tx_credit->next_tag + 1 ) > tx_credit->max_tx_tag ) ?
		tx_credit->min_tx_tag : tx_credit->next_tag + 1;

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
			bitmask.bitmask[i] &= ~(1 << mask_bit);

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
