#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include <mppa/osconfig.h>
#include <mppa_noc.h>

#include "pcie.h"
#include "internal/pcie.h"
#include "netdev.h"
#include "internal/netdev.h"

#ifndef DDR_BASE_ADDR
#define DDR_BASE_ADDR			0x80000000
#endif

#define DIRECTORY_SIZE			(32 * 1024 * 1024)
#define DDR_BUFFER_BASE_ADDR		(DDR_BASE_ADDR + DIRECTORY_SIZE)

#define MAX_DNOC_TX_PER_PCIE_ETH_IF	16

#define H2C_RING_BUFFER_ENTRIES	17
#define C2H_RING_BUFFER_ENTRIES	256

#define BUF_POOL_COUNT	(1 + MPODP_MAX_IF_COUNT* MPODP_MAX_RX_QUEUES)

/**
 * PCIe ethernet interface config
 */
struct mppa_pcie_g_eth_if_cfg {
	struct mppa_pcie_eth_ring_buff_desc *rx;
};

static void *g_pkt_base_addr = (void *) DDR_BUFFER_BASE_ADDR;


static int pcie_init_buff_pools()
{
	mppa_pcie_noc_rx_buf_t **buf_pool;
	mppa_pcie_noc_rx_buf_t *bufs[MPPA_PCIE_MULTIBUF_BURST];
	unsigned i, j;
	uint32_t buf_left;
	unsigned n_pools = 0;
	unsigned n_mbufs = 0;

	for (i = 0; i < eth_ctrl->if_count; ++i) {
		n_pools += eth_ctrl->configs[i].n_rxqs;
		/* Buffer for all Rxs + spares */
		n_mbufs += 2 * BSP_NB_CLUSTER_MAX * MPPA_PCIE_NOC_RX_NB;
		/* Buffer between Noc2PCI Thr and Host */
		for (j = 0; j < eth_ctrl->configs[i].n_rxqs; ++j) {
			struct mpodp_ring_buff_desc *bDesc =
				(struct mpodp_ring_buff_desc*)(unsigned long)
				eth_ctrl->configs[i].c2h_addr[j];
			n_mbufs += bDesc->count;
		}
		/* Buffer between Rx thr and NoC2PCI Thr *
		 * To be divided among the different full rxq */
		n_mbufs += 2 * MPPA_PCIE_PKT_BURSTINESS * BSP_NB_CLUSTER_MAX;
	}

	buf_pool = calloc(n_mbufs + 1,
			  sizeof(mppa_pcie_noc_rx_buf_t *));
	if (!buf_pool) {
		fprintf(stderr, "Failed to alloc pool descriptor\n");
		return 1;
	}
	buffer_ring_init(&g_free_buf_pool, buf_pool, n_mbufs + 1);
	dbg_printf("Free buf pool: %d elnts\n", n_mbufs);
	buf_pool += n_mbufs;

	for (i = 0; i < eth_ctrl->if_count; i++) {
		/* Divide 2 * MPPA_PCIE_PKT_BURSTINESS per clusters among the rxqs */
		int n_bufs = (2 * MPPA_PCIE_PKT_BURSTINESS * BSP_NB_CLUSTER_MAX + eth_ctrl->configs[i].n_rxqs - 1) /
			eth_ctrl->configs[i].n_rxqs;
		for (j = 0; j < eth_ctrl->configs[i].n_rxqs; ++j) {
			buf_pool = calloc(n_bufs,
					  sizeof(mppa_pcie_noc_rx_buf_t *));
			buffer_ring_init(&g_full_buf_pool[i][j], buf_pool, n_bufs);
			dbg_printf("Full buf pool[%d][%d]: %d elnts\n", i, j, n_bufs);
		}
	}

	for (i = 0; i < n_mbufs; i+= j) {
		for (j = 0; j < MPPA_PCIE_MULTIBUF_BURST && i + j < n_mbufs; ++j){
			bufs[j] = (mppa_pcie_noc_rx_buf_t *) g_pkt_base_addr;
			g_pkt_base_addr += sizeof(mppa_pcie_noc_rx_buf_t);

			bufs[j]->buf_addr = (void *) g_pkt_base_addr;
			bufs[j]->pkt_count = 0;
			g_pkt_base_addr += MPPA_PCIE_MULTIBUF_SIZE;
		}
		buffer_ring_push_multi(&g_free_buf_pool, bufs, j, &buf_left);
	}

	dbg_printf("Allocation done %luB\n",
		   (unsigned long)(g_pkt_base_addr) - DDR_BUFFER_BASE_ADDR);
	return 0;
}

int pcie_start()
{
#if defined(MAGIC_SCALL)
	return 0;
#endif
	if (!eth_ctrl->if_count)
		return -1;

	for (int i = 0; i < BSP_NB_DMA_IO_MAX; i++) {
		mppa_noc_interrupt_line_disable(i, MPPA_NOC_INTERRUPT_LINE_DNOC_TX);
		mppa_noc_interrupt_line_disable(i, MPPA_NOC_INTERRUPT_LINE_DNOC_RX);
	}

	pcie_init_buff_pools();
	pcie_start_tx_rm();
	pcie_start_rx_rm();
	return 0;
}

int pcie_init(int if_count, int mtu)
{
#if defined(MAGIC_SCALL)
	return 0;
#endif

	netdev_init();

	if (if_count > MPPA_PCIE_ETH_IF_MAX)
		return 1;

	eth_if_cfg_t if_cfgs[if_count];
	for (int i = 0; i < if_count; ++i){
		if_cfgs[i].mtu = mtu ?: MPODP_DEFAULT_MTU;
		if_cfgs[i].n_c2h_entries = C2H_RING_BUFFER_ENTRIES;
		if_cfgs[i].n_h2c_entries = H2C_RING_BUFFER_ENTRIES;
		if_cfgs[i].flags = 0;
		if_cfgs[i].if_id = i;
		if_cfgs[i].n_c2h_q = if_cfgs[i].n_h2c_q = 1;
		memcpy(if_cfgs[i].mac_addr, "\x02\xde\xad\xbe\xef", 5);
		if_cfgs[i].mac_addr[MAC_ADDR_LEN - 1] = i + ((mppa_rpc_odp_get_cluster_id(0) - 128) << 1);
	}

	netdev_configure(if_count, if_cfgs);
	__k1_mb();

	return pcie_start();
}

