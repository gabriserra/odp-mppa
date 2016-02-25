#include <stdio.h>
#include <string.h>
#include <mppa_noc.h>
#include <mppa_routing.h>
#include <HAL/hal/hal.h>

#include "pcie_internal.h"
#include "netdev.h"

struct mppa_pcie_eth_dnoc_tx_cfg g_mppa_pcie_tx_cfg[BSP_NB_IOCLUSTER_MAX][BSP_DNOC_TX_PACKETSHAPER_NB_MAX] = {{{0}}};

/**
 * Pool of buffer available for rx 
 */
buffer_ring_t g_free_buf_pool;

/**
 * Buffer ready to be sent to host
 */
buffer_ring_t g_full_buf_pool[MPPA_PCIE_ETH_MAX_INTERFACE_COUNT];


static int pcie_setup_tx(unsigned int iface_id, unsigned int *tx_id,
						 unsigned int cluster_id, unsigned int min_rx,
						 unsigned int max_rx)
{
	mppa_noc_ret_t nret;
	mppa_routing_ret_t rret;
	mppa_dnoc_header_t header;
	mppa_dnoc_channel_config_t config;

	/* Configure the TX for PCIe */
	nret = mppa_noc_dnoc_tx_alloc_auto(iface_id, tx_id, MPPA_NOC_NON_BLOCKING);
	if (nret) {
		dbg_printf("Tx alloc failed\n");
		return 1;
	}

	MPPA_NOC_DNOC_TX_CONFIG_INITIALIZER_DEFAULT(config, 0);

	rret = mppa_routing_get_dnoc_unicast_route(odp_rpc_get_cluster_id(iface_id),
											   cluster_id, &config, &header);
	if (rret) {
		dbg_printf("Routing failed\n");
		return 1;
	}

	header._.multicast = 0;
	header._.tag = min_rx;
	header._.valid = 1;

	nret = mppa_noc_dnoc_tx_configure(iface_id, *tx_id, header, config);
	if (nret) {
		dbg_printf("Tx configure failed\n");
		return 1;
	}

	volatile mppa_dnoc_min_max_task_id_t *context =
		&mppa_dnoc[iface_id]->tx_chan_route[*tx_id].min_max_task_id[0];

	context->_.current_task_id = min_rx;
	context->_.min_task_id = min_rx;
	context->_.max_task_id = max_rx;
	context->_.min_max_task_id_en = 1;

	return 0;
}

static inline int pcie_add_forward(unsigned int pcie_eth_if_id,
								   struct mppa_pcie_eth_dnoc_tx_cfg *dnoc_tx_cfg)
{
	struct mppa_pcie_eth_if_config * cfg = netdev_get_eth_if_config(pcie_eth_if_id);
	struct mppa_pcie_eth_h2c_ring_buff_entry entry;

	entry.len = dnoc_tx_cfg->mtu;
	entry.pkt_addr = (uint32_t)dnoc_tx_cfg->fifo_addr;
	entry.flags = MPPA_PCIE_ETH_NEED_PKT_HDR;

	return netdev_h2c_enqueue_buffer(cfg, &entry);
}

static odp_rpc_cmd_ack_t pcie_open(unsigned remoteClus, odp_rpc_t * msg)
{
	odp_rpc_cmd_pcie_open_t open_cmd = {.inl_data = msg->inl_data};
	odp_rpc_cmd_ack_t ack = ODP_RPC_CMD_ACK_INITIALIZER;
	struct mppa_pcie_eth_dnoc_tx_cfg *tx_cfg;
	int if_id = remoteClus % MPPA_PCIE_USABLE_DNOC_IF;
	unsigned int tx_id;

	dbg_printf("Received request to open PCIe\n");
	int ret = pcie_setup_tx(if_id, &tx_id, remoteClus,
							open_cmd.min_rx, open_cmd.max_rx);
	if (ret) {
		fprintf(stderr, "[PCIe] Error: Failed to setup tx on if %d\n", if_id);
		return ack;
	}

	/*
	 * Allocate contiguous RX ports
	 */
	int n_rx, first_rx;

	for (first_rx = 0; first_rx <  MPPA_DNOC_RX_QUEUES_NUMBER - MPPA_PCIE_NOC_RX_NB;
	     ++first_rx) {
		for (n_rx = 0; n_rx < MPPA_PCIE_NOC_RX_NB; ++n_rx) {
			mppa_noc_ret_t ret;
			ret = mppa_noc_dnoc_rx_alloc(if_id,
						     first_rx + n_rx);
			if (ret != MPPA_NOC_RET_SUCCESS)
				break;
		}
		if (n_rx < MPPA_PCIE_NOC_RX_NB) {
			n_rx--;
			for ( ; n_rx >= 0; --n_rx) {
				mppa_noc_dnoc_rx_free(if_id,
						      first_rx + n_rx);
			}
		} else {
			break;
		}
	}
	if (n_rx < MPPA_PCIE_NOC_RX_NB) {
		err_printf("failed to allocate %d contiguous Rx ports\n", MPPA_PCIE_NOC_RX_NB);
		exit(1);
	}
	unsigned int min_tx_tag = first_rx;
	unsigned int max_tx_tag = first_rx + MPPA_PCIE_NOC_RX_NB - 1;
	for ( unsigned rx_id = min_tx_tag; rx_id <= max_tx_tag; ++rx_id ) {
		ret = pcie_setup_rx(if_id, rx_id, open_cmd.pcie_eth_if_id);
		if (ret)
			return ack;
	}

	dbg_printf("if %d RXs [%u..%u] allocated for cluster %d\n",
			   if_id, min_tx_tag, max_tx_tag, remoteClus);
	tx_cfg = &g_mppa_pcie_tx_cfg[if_id][tx_id];
	tx_cfg->opened = 1;
	tx_cfg->cluster = remoteClus;
	tx_cfg->fifo_addr = &mppa_dnoc[if_id]->tx_ports[tx_id].push_data;
	tx_cfg->pcie_eth_if = open_cmd.pcie_eth_if_id;
	tx_cfg->mtu = open_cmd.pkt_size;

	ret = pcie_add_forward(open_cmd.pcie_eth_if_id, tx_cfg);
	if (ret)
		return ack;

	ack.cmd.pcie_open.min_tx_tag = min_tx_tag; /* RX ID ! */
	ack.cmd.pcie_open.max_tx_tag = max_tx_tag; /* RX ID ! */
	ack.cmd.pcie_open.tx_if = odp_rpc_get_cluster_id(if_id);
	/* FIXME, we send the same MTU as the one received */
	ack.cmd.pcie_open.mtu = open_cmd.pkt_size;
	memcpy(ack.cmd.pcie_open.mac,
		   eth_control.configs[open_cmd.pcie_eth_if_id].mac_addr,
		   MAC_ADDR_LEN);
	ack.status = 0;

	return ack;
}

static odp_rpc_cmd_ack_t pcie_close(__attribute__((unused)) unsigned remoteClus,
									__attribute__((unused)) odp_rpc_t * msg)
{
	odp_rpc_cmd_ack_t ack = ODP_RPC_CMD_ACK_INITIALIZER;
	ack.status = 0;

	return ack;
}

static int pcie_rpc_handler(unsigned remoteClus, odp_rpc_t *msg, uint8_t *payload)
{
	odp_rpc_cmd_ack_t ack = ODP_RPC_CMD_ACK_INITIALIZER;

	(void)payload;
	switch (msg->pkt_type){
	case ODP_RPC_CMD_PCIE_OPEN:
		ack = pcie_open(remoteClus, msg);
		break;
	case ODP_RPC_CMD_PCIE_CLOS:
		ack = pcie_close(remoteClus, msg);
		break;
	default:
		return -1;
	}
	odp_rpc_server_ack(msg, ack);
	return 0;
}

void  __attribute__ ((constructor)) __pcie_rpc_constructor()
{
#if defined(MAGIC_SCALL)
	return;
#endif
	if(__n_rpc_handlers < MAX_RPC_HANDLERS) {
		__rpc_handlers[__n_rpc_handlers++] = pcie_rpc_handler;
	} else {
		fprintf(stderr, "Failed to register PCIE RPC handlers\n");
		exit(EXIT_FAILURE);
	}
}
