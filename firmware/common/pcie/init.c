#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include <mppa/osconfig.h>
#include <HAL/hal/hal.h>
#include <mppa_noc.h>

#include "pcie_internal.h"
#include "netdev.h"

#define DDR_BASE_ADDR			0x80000000
#define DIRECTORY_SIZE			(32 * 1024 * 1024)
#define DDR_BUFFER_BASE_ADDR		(DDR_BASE_ADDR + DIRECTORY_SIZE)

#define MAX_DNOC_TX_PER_PCIE_ETH_IF	16

#define RING_BUFFER_ENTRIES	16

#define BUF_POOL_COUNT	(1 + MPPA_PCIE_ETH_MAX_INTERFACE_COUNT)

/**
 * PCIe ethernet interface config
 */
struct mppa_pcie_g_eth_if_cfg {
	struct mppa_pcie_eth_ring_buff_desc *rx;
};

static struct mppa_pcie_g_eth_if_cfg g_eth_if_cfg[MPPA_PCIE_ETH_IF_MAX] = {{NULL}};
static void *g_pkt_base_addr = (void *) DDR_BUFFER_BASE_ADDR;


static int pcie_init_buff_pools()
{
	mppa_pcie_noc_rx_buf_t **buf_pool;
	mppa_pcie_noc_rx_buf_t *bufs[MPPA_PCIE_MULTIBUF_COUNT];
	int i;
	uint32_t buf_left;

	buf_pool = calloc(MPPA_PCIE_MULTIBUF_COUNT * BUF_POOL_COUNT
					  , sizeof(mppa_pcie_noc_rx_buf_t *));
	if (!buf_pool) {
		fprintf(stderr, "Failed to alloc pool descriptor\n");
		return 1;
	}
	buffer_ring_init(&g_free_buf_pool, buf_pool, MPPA_PCIE_MULTIBUF_COUNT);

	for (i = 0; i < MPPA_PCIE_ETH_MAX_INTERFACE_COUNT; i++) {
		buf_pool += MPPA_PCIE_MULTIBUF_COUNT;
		buffer_ring_init(&g_full_buf_pool[i], buf_pool, MPPA_PCIE_MULTIBUF_COUNT);
	}

	for (i = 0; i < MPPA_PCIE_MULTIBUF_COUNT; i++) {
		bufs[i] = (mppa_pcie_noc_rx_buf_t *) g_pkt_base_addr;
		g_pkt_base_addr += sizeof(mppa_pcie_noc_rx_buf_t);

		bufs[i]->buf_addr = (void *) g_pkt_base_addr;
		bufs[i]->pkt_count = 0;
		g_pkt_base_addr += MPPA_PCIE_MULTIBUF_SIZE;
	}

	buffer_ring_push_multi(&g_free_buf_pool, bufs, MPPA_PCIE_MULTIBUF_COUNT, &buf_left);
	dbg_printf("Allocation done\n");
	return 0;
}

int pcie_init(int if_count)
{
#if defined(MAGIC_SCALL)
	return 0;
#endif
	if (if_count > MPPA_PCIE_ETH_IF_MAX)
		return 1;

	eth_if_cfg_t if_cfgs[if_count];
	for (int i = 0; i < if_count; ++i){
		if_cfgs[i].mtu = MPPA_PCIE_ETH_DEFAULT_MTU;
		if_cfgs[i].n_c2h_entries = RING_BUFFER_ENTRIES;
		if_cfgs[i].n_h2c_entries = RING_BUFFER_ENTRIES;
		if_cfgs[i].flags = MPPA_PCIE_ETH_CONFIG_RING_AUTOLOOP;
		if_cfgs[i].if_id = i;
		memcpy(if_cfgs[i].mac_addr, "\x02\xde\xad\xbe\xef", 5);
		if_cfgs[i].mac_addr[MAC_ADDR_LEN - 1] = i + ((odp_rpc_get_cluster_id(0) - 128) << 1);
	}

	netdev_init(if_count, if_cfgs);
	for (int i = 0; i < if_count; ++i){
		g_eth_if_cfg[i].rx = (void*)(unsigned long)eth_control.configs[i].c2h_ring_buf_desc_addr;
	}

	netdev_start();

	for (int i = 0; i < BSP_NB_DMA_IO_MAX; i++) {
		mppa_noc_interrupt_line_disable(i, MPPA_NOC_INTERRUPT_LINE_DNOC_TX);
		mppa_noc_interrupt_line_disable(i, MPPA_NOC_INTERRUPT_LINE_DNOC_RX);
	}

	pcie_init_buff_pools();
	pcie_start_tx_rm();
	pcie_start_rx_rm();

	return 0;
}

