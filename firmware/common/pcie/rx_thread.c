#include <stdio.h>
#include <errno.h>
#include <mppa_noc.h>

#include "pcie_internal.h"
#include "netdev.h"
#include "noc2pci.h"

rx_thread_t g_rx_threads[RX_THREAD_COUNT];

static int reload_rx(rx_iface_t *iface, int rx_id)
{
	mppa_pcie_noc_rx_buf_t *new_buf;
	mppa_pcie_noc_rx_buf_t *old_buf;
	uint32_t left;
	uint16_t events;
	uint16_t pcie_eth_if = iface->rx_cfgs[rx_id].pcie_eth_if;

	int ret = buffer_ring_get_multi(&g_free_buf_pool, &new_buf, 1, &left);
	if (ret != 1) {
		err_printf("No more free buffer available\n");
		return -1;
	}

	events = mppa_noc_dnoc_rx_lac_event_counter(iface->iface_id, rx_id);
	if (!events) {
		err_printf("Invalid count of events on rx %d\n", rx_id);
		exit(1);
	}

	typeof(mppa_dnoc[iface->iface_id]->rx_queues[0]) * const rx_queue =
		&mppa_dnoc[iface->iface_id]->rx_queues[rx_id];

	rx_queue->buffer_base.dword = (uintptr_t) new_buf->buf_addr;

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
	}

	old_buf = iface->rx_cfgs[rx_id].mapped_buf;
	iface->rx_cfgs[rx_id].mapped_buf = new_buf;
	dbg_printf("Adding buf to eth if %d\n", pcie_eth_if);
	/* Add previous buffer to full list */
	buffer_ring_push_multi(&g_full_buf_pool[pcie_eth_if], &old_buf, 1, &left);

	dbg_printf("Reloading rx %d of if %d with buffer %p\n",
		   rx_id, iface->iface_id, new_buf);

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

static void
mppa_pcie_rx_rm_func()
{
	rx_thread_t *thread = &g_rx_threads[__k1_get_cpu_id() - RX_RM_START];
	int iface;

	dbg_printf("RM %d with thread id %d started\n", __k1_get_cpu_id(), thread->th_id);

	thread->ready = 1;
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

		thread->th_id = rm_num - RX_RM_START;

		for( i = 0; i < IF_PER_THREAD; i++) {
			thread->iface[i].iface_id = if_start++;
		}

		/* Init with scratchpad size */
		_K1_PE_STACK_ADDRESS[rm_num] = &g_stacks[thread->th_id][RX_RM_STACK_SIZE - 16];
		_K1_PE_START_ADDRESS[rm_num] = &mppa_pcie_rx_rm_func;
		_K1_PE_ARGS_ADDRESS[rm_num] = 0;

		__builtin_k1_dinval();
		__builtin_k1_wpurge();
		__builtin_k1_fence();

		dbg_printf("Powering RM %d\n", rm_num);
		__k1_poweron(rm_num);
	}

	for (rm_num = RX_RM_START; rm_num < RX_RM_START + RX_RM_COUNT; rm_num++ ){
		while(!g_rx_threads[rm_num - RX_RM_START].ready);
		dbg_printf("RM %d started\n", rm_num);
	}
}
