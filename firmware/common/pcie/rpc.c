#include <stdio.h>
#include <string.h>
#include <mppa_noc.h>
#include <mppa_routing.h>
#include <HAL/hal/hal.h>

#include "internal/pcie.h"
#include "internal/netdev.h"
#include "internal/noc2pci.h"
#include "netdev.h"

struct mppa_pcie_eth_dnoc_tx_cfg g_mppa_pcie_tx_cfg[BSP_NB_IOCLUSTER_MAX][BSP_DNOC_TX_PACKETSHAPER_NB_MAX] = {{{0}}};

/**
 * Pool of buffer available for rx 
 */
buffer_ring_t g_free_buf_pool;

/**
 * Buffer ready to be sent to host
 */
buffer_ring_t g_full_buf_pool[MPODP_MAX_IF_COUNT][MPODP_MAX_RX_QUEUES];

static int netdev_initialized = 0;

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

	rret = mppa_routing_get_dnoc_unicast_route(mppa_rpc_odp_get_cluster_id(iface_id),
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

static inline int pcie_add_forward(struct mppa_pcie_eth_dnoc_tx_cfg *dnoc_tx_cfg,
				   mppa_rpc_odp_answer_t *answer)
{
	struct mpodp_if_config * cfg =
		netdev_get_eth_if_config(dnoc_tx_cfg->pcie_eth_if);
	struct mpodp_h2c_entry entry;

	entry.pkt_addr = (uint32_t)dnoc_tx_cfg->fifo_addr;

	if (netdev_h2c_enqueue_buffer(cfg, dnoc_tx_cfg->h2c_q, &entry)) {
		PCIE_RPC_ERR_MSG(answer,
				 "Failed to register cluster to pcie interface %d\n",
				 dnoc_tx_cfg->pcie_eth_if);
		return -1;
	}
	return 0;
}

static int8_t cnoc_tx_tags[MPPA_CNOC_COUNT] = { [0 ... MPPA_CNOC_COUNT-1] = -1 };
static inline int pcie_get_cnoc_tx_tag(int if_id)
{
	mppa_noc_ret_t ret;
	if ( cnoc_tx_tags[if_id] == -1 ) {
		unsigned tag;
		ret = mppa_noc_cnoc_tx_alloc_auto(if_id, &tag, MPPA_NOC_BLOCKING);
		assert(ret == MPPA_NOC_RET_SUCCESS);
		cnoc_tx_tags[if_id] = tag;
	}
	return cnoc_tx_tags[if_id];
}

static void pcie_open(unsigned remoteClus, mppa_rpc_odp_t * msg,
		      mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_pcie_open_t open_cmd = {.inl_data = msg->inl_data};
	struct mppa_pcie_eth_dnoc_tx_cfg *tx_cfg;
	int if_id = remoteClus % MPPA_PCIE_USABLE_DNOC_IF;
	unsigned int tx_id;

	dbg_printf("Received request to open PCIe\n");
	if (!netdev_initialized) {
		if (netdev_start()){
			PCIE_RPC_ERR_MSG(answer, "Failed to initialize netdevs\n");
			return;
		}
		netdev_initialized = 1;
	}
	int ret = pcie_setup_tx(if_id, &tx_id, remoteClus,
							open_cmd.min_rx, open_cmd.max_rx);
	if (ret) {
		PCIE_RPC_ERR_MSG(answer, "Failed to setup tx on if %d\n", if_id);
		return;
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
		PCIE_RPC_ERR_MSG(answer, "Failed to allocate %d contiguous Rx ports\n",
				 MPPA_PCIE_NOC_RX_NB);
		return;
	}

	unsigned int min_tx_tag = first_rx;
	unsigned int max_tx_tag = first_rx + MPPA_PCIE_NOC_RX_NB - 1;
	unsigned rx_id;
	mppa_routing_ret_t rret;
	unsigned c2h_q = pcie_cluster_to_c2h_q(open_cmd.pcie_eth_if_id, remoteClus);

	tx_credit_t *tx_credit = calloc(1, sizeof(tx_credit_t));
	tx_credit->cluster = remoteClus;
	tx_credit->min_tx_tag = min_tx_tag;
	tx_credit->max_tx_tag = max_tx_tag;
	tx_credit->next_tag = min_tx_tag;
	tx_credit->cnoc_tx = pcie_get_cnoc_tx_tag(if_id);

	tx_credit->credit = MPPA_PCIE_NOC_RX_NB;
	tx_credit->header._.tag = tx_credit->remote_cnoc_rx = open_cmd.cnoc_rx;
	rret = mppa_routing_get_cnoc_unicast_route(mppa_rpc_odp_get_cluster_id(if_id),
						   remoteClus, &tx_credit->config,
						   &tx_credit->header);
	assert(rret == 0);

	ret = mppa_noc_cnoc_tx_configure(if_id,
			tx_credit->cnoc_tx,
			tx_credit->config,
			tx_credit->header);
	assert(ret == MPPA_NOC_RET_SUCCESS);
	mppa_noc_cnoc_tx_push(if_id, tx_credit->cnoc_tx, tx_credit->credit);

	for ( rx_id = min_tx_tag; rx_id <= max_tx_tag; ++rx_id ) {
		ret = pcie_setup_rx(if_id, rx_id, open_cmd.pcie_eth_if_id,
				    c2h_q, tx_credit, answer);
		if (ret)
			return;
	}

	dbg_printf("if %d RXs [%u..%u] allocated for cluster %d\n",
			   if_id, min_tx_tag, max_tx_tag, remoteClus);
	tx_cfg = &g_mppa_pcie_tx_cfg[if_id][tx_id];
	tx_cfg->opened = 1;
	tx_cfg->cluster = remoteClus;
	tx_cfg->fifo_addr = &mppa_dnoc[if_id]->tx_ports[tx_id].push_data;
	tx_cfg->pcie_eth_if = open_cmd.pcie_eth_if_id;
	tx_cfg->mtu = open_cmd.pkt_size;
	tx_cfg->h2c_q = pcie_cluster_to_h2c_q(open_cmd.pcie_eth_if_id,
					      remoteClus);

	ret = pcie_add_forward(tx_cfg, answer);
	if (ret)
		return;

	answer->ack.cmd.pcie_open.min_tx_tag = min_tx_tag; /* RX ID ! */
	answer->ack.cmd.pcie_open.max_tx_tag = max_tx_tag; /* RX ID ! */
	answer->ack.cmd.pcie_open.tx_if = mppa_rpc_odp_get_cluster_id(if_id);
	/* FIXME, we send the same MTU as the one received */
	answer->ack.cmd.pcie_open.mtu = open_cmd.pkt_size;
	memcpy(answer->ack.cmd.pcie_open.mac,
	       eth_ctrl->configs[open_cmd.pcie_eth_if_id].mac_addr,
	       MAC_ADDR_LEN);

	return;
}

static void pcie_close(__attribute__((unused)) unsigned remoteClus,
		       __attribute__((unused)) mppa_rpc_odp_t * msg,
		       __attribute__((unused)) mppa_rpc_odp_answer_t *answer)
{
	return;
}

static int pcie_rpc_handler(unsigned remoteClus, mppa_rpc_odp_t *msg, uint8_t *payload)
{
	mppa_rpc_odp_answer_t answer = MPPA_RPC_ODP_ANSWER_INITIALIZER(msg);

	if (msg->pkt_class != MPPA_RPC_ODP_CLASS_PCIE)
		return -MPPA_RPC_ODP_ERR_INTERNAL_ERROR;
	if (msg->cos_version != MPPA_RPC_ODP_PCIE_VERSION)
		return -MPPA_RPC_ODP_ERR_VERSION_MISMATCH;

	(void)payload;
	switch (msg->pkt_subtype){
	case MPPA_RPC_ODP_CMD_PCIE_OPEN:
		pcie_open(remoteClus, msg, &answer);
		break;
	case MPPA_RPC_ODP_CMD_PCIE_CLOS:
		pcie_close(remoteClus, msg, &answer);
		break;
	default:
		return -MPPA_RPC_ODP_ERR_BAD_SUBTYPE;
	}

	mppa_rpc_odp_server_ack(&answer);
	return -MPPA_RPC_ODP_ERR_NONE;
}

void  __attribute__ ((constructor)) __pcie_rpc_constructor()
{
#if defined(MAGIC_SCALL)
	return;
#endif
	__rpc_handlers[MPPA_RPC_ODP_CLASS_PCIE] = pcie_rpc_handler;
}
