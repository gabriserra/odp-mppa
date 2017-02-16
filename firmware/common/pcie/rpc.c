#include <stdio.h>
#include <string.h>
#include <mppa_noc.h>
#include <mppa_routing.h>
#include <odp/rpc/api.h>

#include "internal/pcie.h"
#include "internal/netdev.h"
#include "internal/noc2pci.h"
#include "netdev.h"

/**
 * Pool of buffer available for rx 
 */
buffer_ring_t g_free_buf_pool;

/**
 * Buffer ready to be sent to host
 */
buffer_ring_t g_full_buf_pool[MPODP_MAX_IF_COUNT][MPODP_MAX_RX_QUEUES];

static inline int get_ddr_dma_id(unsigned cluster_id){
	return cluster_id % MPPA_PCIE_USABLE_DNOC_IF;
}

static void pcie_open(unsigned remoteClus, mppa_rpc_odp_t * msg,
		      mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_pcie_open_t open_cmd = {.inl_data = msg->inl_data};
	const int nocIf = get_ddr_dma_id(remoteClus);
	const int externalAddress = mppa_rpc_odp_get_cluster_id(nocIf);
	const int pcie_if = open_cmd.pcie_eth_if_id;
	const struct mpodp_if_config * cfg = netdev_get_eth_if_config(pcie_if);

	if (open_cmd.pkt_size < cfg->mtu) {
		PCIE_RPC_ERR_MSG(answer, "Cluster MTU %d is smaller than PCI MTU %d\n",
				 open_cmd.pkt_size, cfg->mtu);
		return;
	}

	if (pcietool_open_cluster(remoteClus, pcie_if, answer))
		return;
	mppa_pcie_link_cluster_status_t *clus =
		&pcie_status.link[pcie_if].cluster[remoteClus];
	clus->tx_enabled = 1;
	clus->rx_enabled = 1;

	if (pcietool_setup_clus2pcie(remoteClus, pcie_if, nocIf,
				     open_cmd.cnoc_rx, open_cmd.verbose, answer))
		goto err;
	if (pcietool_setup_pcie2clus(remoteClus, pcie_if, nocIf,
				     externalAddress,
				     open_cmd.min_rx, open_cmd.max_rx,
				     answer))
		goto err;

	if (pcietool_enable_cluster(remoteClus, pcie_if, answer))
		goto err;

	pcie_status.link[open_cmd.pcie_eth_if_id].cluster[remoteClus].opened = 1;

	GET_ACK(pcie, answer)->cmd.pcie_open.min_tx_tag = clus->c2p_min_rx;
	GET_ACK(pcie, answer)->cmd.pcie_open.max_tx_tag = clus->c2p_max_rx;
	GET_ACK(pcie, answer)->cmd.pcie_open.tx_if = mppa_rpc_odp_get_cluster_id(clus->c2p_nocIf);

	/* FIXME, we send the same MTU as the one received */
	GET_ACK(pcie, answer)->cmd.pcie_open.mtu = cfg->mtu;
	memcpy(GET_ACK(pcie, answer)->cmd.pcie_open.mac,
	       eth_ctrl->configs[open_cmd.pcie_eth_if_id].mac_addr,
	       MAC_ADDR_LEN);

	return;

 err:
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
	register_rpc_service(MPPA_RPC_ODP_CLASS_PCIE, pcie_rpc_handler);
	mppa_rpc_odp_register_print_helper(MPPA_RPC_ODP_CLASS_PCIE, mppa_odp_rpc_pcie_print_msg);
}
