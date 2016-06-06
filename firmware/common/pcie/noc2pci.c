#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <mppa_noc.h>

#include "internal/pcie.h"
#include "internal/netdev.h"
#include "internal/noc2pci.h"
#include "pcie.h"

void mppa_pcie_noc_rx_buffer_consumed(uint64_t data)
{
	mppa_pcie_noc_rx_buf_t *buf;
	uint32_t left;

	buf = (mppa_pcie_noc_rx_buf_t *) (uintptr_t) data ;

	buf->pkt_count--;
	/* All packets from this buffer have been transfered,
	 * add it again to the free list */
	if (buf->pkt_count == 0)
		buffer_ring_push_multi(&g_free_buf_pool, &buf, 1, &left);
}


static uint64_t pkt_count[MPPA_PCIE_ETH_IF_MAX] = {0};
static void poll_noc_rx_buffer(int pcie_eth_if, uint32_t c2h_q)
{
	mppa_pcie_noc_rx_buf_t *bufs[MPPA_PCIE_MULTIBUF_BURST], *buf;
	uint32_t left, pkt_size;
	int ret = 0, buf_idx;
	void * pkt_addr;
	union mpodp_pkt_hdr_info info;
	struct mpodp_if_config *cfg = netdev_get_eth_if_config(pcie_eth_if);
	struct mpodp_c2h_entry pkt, free_pkt;
	int nb_bufs;

	if (netdev_c2h_is_full(cfg, c2h_q)) {
		dbg_printf("PCIe eth tx is full !!!\n");
		return;
	}

	nb_bufs = buffer_ring_get_multi(&g_full_buf_pool[pcie_eth_if][c2h_q], bufs,
					MPPA_PCIE_MULTIBUF_BURST, &left);
	if (nb_bufs == 0)
		return;
	assert(ret <= MPPA_PCIE_MULTIBUF_COUNT);

	dbg_printf("%d buffer ready to be sent\n", ret);
	for(buf_idx = 0; buf_idx < nb_bufs; buf_idx++) {
		buf = bufs[buf_idx];
		buf->pkt_count = 0;

		/* Packet size is added as a header to the packet */
		pkt_addr = buf->buf_addr;

		while (1) {
			/* Read header from packet */

			info.dword = __builtin_k1_ldu(pkt_addr + offsetof(struct mpodp_pkt_hdr, info));
			pkt_size = info._.pkt_size;
			pkt_addr += sizeof(struct mpodp_pkt_hdr);
			buf->pkt_count++;

			dbg_printf("packet at addr %p, size %ld\n", pkt_addr, pkt_size);

			/* Send one packet of the buffer and add buf as padding
			 * data to handle consumed packets */
			pkt.len = pkt_size;
			pkt.status = 0;
			pkt.pkt_addr = (unsigned long)pkt_addr;
			pkt.data = (unsigned long)buf;
			do {
				ret = netdev_c2h_enqueue_data(cfg, c2h_q, &pkt, &free_pkt);
			} while (ret < 0);

			if (free_pkt.data != 0)
				mppa_pcie_noc_rx_buffer_consumed(free_pkt.data);

			// jump to next packet, rounded to sizeof(uin64_t)
			pkt_addr += ( ( pkt_size + sizeof(uint64_t) - 1 ) / sizeof(uint64_t) ) *
				sizeof(uint64_t);

			if (info._.hash_key & END_OF_PACKETS)
				break;
		}

		pkt_count[pcie_eth_if]++;
		dbg_printf("%d packets handled, total %llu\n", buf->pkt_count, pkt_count[pcie_eth_if]);
	}
}

/**
 * Sender RM function which take buffer from clusters and send them
 * to host through PCIe.
 */
static void mppa_pcie_pcie_tx_sender()
{
	unsigned int i, j;

	while(1) {
		for (i = 0; i < eth_control.if_count; i++)
			for (j = 0; j < eth_control.configs[i].n_rxqs; ++j)
				poll_noc_rx_buffer(i, j);
	}
}

static uint64_t g_pcie_tx_stack[RX_RM_STACK_SIZE];

void pcie_start_tx_rm()
{
		/* Init with scratchpad size */
		_K1_PE_STACK_ADDRESS[PCIE_TX_RM] = &g_pcie_tx_stack[RX_RM_STACK_SIZE - 16];
		_K1_PE_START_ADDRESS[PCIE_TX_RM] = &mppa_pcie_pcie_tx_sender;
		_K1_PE_ARGS_ADDRESS[PCIE_TX_RM] = 0;

		__builtin_k1_dinval();
		__builtin_k1_wpurge();
		__builtin_k1_fence();

		dbg_printf("Powering pcie tx RM %d\n", PCIE_TX_RM);
		__k1_poweron(PCIE_TX_RM);
}



static int pcie_configure_rx(rx_iface_t *iface, int dma_if, int rx_id)
{
	mppa_pcie_noc_rx_buf_t *buf;
	uint32_t left;
	mppa_noc_dnoc_rx_configuration_t conf = MPPA_NOC_DNOC_RX_CONFIGURATION_INIT;
	int ret;
	while ( buffer_ring_get_multi(&g_free_buf_pool, &buf, 1, &left) == -1 );

	iface->rx_cfgs[rx_id].mapped_buf = buf;

	conf.buffer_base = (uintptr_t) buf->buf_addr;
	conf.buffer_size = MPPA_PCIE_MULTIBUF_SIZE;
	conf.current_offset = 0;
	conf.event_counter = 0;
	conf.item_counter = 1;
	conf.item_reload = 1;
	conf.reload_mode = MPPA_NOC_RX_RELOAD_MODE_DECR_NOTIF_NO_RELOAD_IDLE;
	conf.activation = 0x3;
	conf.counter_id = 0;

	ret = mppa_noc_dnoc_rx_configure(dma_if, rx_id, conf);
	if (ret)
		return -1;

	ret = mppa_dnoc[dma_if]->rx_queues[rx_id].
		get_drop_pkt_nb_and_activate.reg;
	mppa_noc_enable_event(dma_if,
			      MPPA_NOC_INTERRUPT_LINE_DNOC_RX,
			      rx_id, (1 << BSP_NB_PE_P) - 1);

	return 0;
}


int pcie_setup_rx(int if_id, unsigned int rx_id, unsigned int pcie_eth_if,
		  unsigned int c2h_q,
		  tx_credit_t *tx_credit, odp_rpc_answer_t *answer)
{
	int rx_thread_num = if_id / RX_THREAD_COUNT;
	int th_iface_id = if_id % IF_PER_THREAD;
	int rx_mask_off;
	int bit_id = 0;
	rx_iface_t *iface;

	rx_mask_off = rx_id / (sizeof(iface->ev_mask[0]) * 8);
	bit_id =  rx_id % (sizeof(iface->ev_mask[0]) * 8);
	iface = &g_rx_threads[rx_thread_num].iface[th_iface_id];

	if (pcie_configure_rx(iface, if_id, rx_id)) {
		PCIE_RPC_ERR_MSG(answer, "Failed to configure noc rx\n");
		return -1;
	}

	iface->ev_mask[rx_mask_off] |= (1ULL << bit_id);
	iface->rx_cfgs[rx_id].pcie_eth_if = pcie_eth_if;
	iface->rx_cfgs[rx_id].c2h_q = c2h_q;
	iface->rx_cfgs[rx_id].broken = 0;
	iface->rx_cfgs[rx_id].tx_credit = tx_credit;

	__k1_mb();

	return 0;
}
