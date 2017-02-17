#include <stdio.h>
#include <string.h>
#include <mppa_noc.h>
#include <mppa_routing.h>

#include "internal/pcie.h"
#include "internal/netdev.h"
#include "internal/noc2pci.h"
#include "netdev.h"

static int netdev_initialized = 0;

int pcietool_open_cluster(unsigned remoteClus __attribute__((unused)),
			  int if_id, mppa_rpc_odp_answer_t *answer)
{
	if (if_id > MPODP_MAX_IF_COUNT){
		PCIE_RPC_ERR_MSG(answer,
				 "Trying to open interface with id %d greater than allowed maximum (%d)\n",
				 if_id, MPODP_MAX_IF_COUNT - 1);
		return -1;
	}
	if (!netdev_initialized) {
		if (netdev_start()){
			PCIE_RPC_ERR_MSG(answer, "Failed to initialize netdevs\n");
			return -1;
		}
		netdev_initialized = 1;
	}
	if (!pcie_status.link[if_id].enabled) {
		int ret =
			mppa_noc_cnoc_tx_alloc_auto(if_id,
						    (unsigned*)&pcie_status.link[if_id].cnoc_tx,
						    MPPA_NOC_NON_BLOCKING);
		if (ret != MPPA_NOC_RET_SUCCESS) {
			PCIE_RPC_ERR_MSG(answer, "Failed to allocate a CNoC Tx\n");
			return -1;
		}
		pcie_status.link[if_id].enabled = 1;
	}

	return 0;
}

int pcietool_setup_pcie2clus(unsigned remoteClus, int if_id,
			     int nocIf, int externalAddress,
			     int min_rx, int max_rx,
			     mppa_rpc_odp_answer_t *answer)
{
	mppa_noc_ret_t nret;
	mppa_routing_ret_t rret;
	mppa_dnoc_header_t header;
	mppa_dnoc_channel_config_t config;
	unsigned nocTx;

	if (!pcie_status.link[if_id].cluster[remoteClus].rx_enabled)
		return 0;

	/* Configure the TX for PCIe */
	nret = mppa_noc_dnoc_tx_alloc_auto(nocIf, &nocTx, MPPA_NOC_NON_BLOCKING);
	if (nret) {
		PCIE_RPC_ERR_MSG(answer, "Tx allocation failed\n");
		return -1;
	}

	MPPA_NOC_DNOC_TX_CONFIG_INITIALIZER_DEFAULT(config, 0);

	rret = mppa_routing_get_dnoc_unicast_route(externalAddress,
						   mppa_rpc_odp_undensify_cluster_id(remoteClus),
						   &config, &header);
	if (rret) {
		PCIE_RPC_ERR_MSG(answer, "Routing failed\n");
		return -1;
	}

	header._.multicast = 0;
	header._.tag = min_rx;
	header._.valid = 1;

	nret = mppa_noc_dnoc_tx_configure(nocIf, nocTx, header, config);
	if (nret) {
		PCIE_RPC_ERR_MSG(answer, "Tx configure failed\n");
		return -1;
	}

	pcie_status.link[if_id].cluster[remoteClus].p2c_nocIf = nocIf;
	pcie_status.link[if_id].cluster[remoteClus].p2c_txId = nocTx;
	pcie_status.link[if_id].cluster[remoteClus].p2c_min_rx = min_rx;
	pcie_status.link[if_id].cluster[remoteClus].p2c_max_rx = max_rx;
	pcie_status.link[if_id].cluster[remoteClus].p2c_q =
		pcie_cluster_to_h2c_q(if_id, remoteClus);
	volatile mppa_dnoc_min_max_task_id_t *context =
		&mppa_dnoc[nocIf]->tx_chan_route[nocTx].min_max_task_id[0];

	context->_.current_task_id = min_rx;
	context->_.min_task_id = min_rx;
	context->_.max_task_id = max_rx;
	context->_.min_max_task_id_en = 1;

	return 0;
}

static int allocate_dnoc_rx_ports(int if_id, int num_ports)
{
	/*
	 * Allocate contiguous RX ports
	 */
	int n_rx, first_rx;

	for (first_rx = 0; first_rx <  MPPA_DNOC_RX_QUEUES_NUMBER - num_ports;
	     ++first_rx) {
		for (n_rx = 0; n_rx < num_ports; ++n_rx) {
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

	if (n_rx < num_ports) {
		return -1;
	}
	return first_rx;
}

int pcietool_setup_clus2pcie(unsigned remoteClus, int if_id, int nocIf,
			     int remote_cnoc_rx, int verbose,
			     mppa_rpc_odp_answer_t *answer)
{
	int first_rx;
	const unsigned c2h_q = pcie_cluster_to_c2h_q(if_id, remoteClus);
	mppa_pcie_link_cluster_status_t *clus_status =
		&pcie_status.link[if_id].cluster[remoteClus];
	const unsigned cnoc_tx = pcie_status.link[if_id].cnoc_tx;
	int ret;

	if (!pcie_status.link[if_id].cluster[remoteClus].tx_enabled)
		return 0;

	/* Get Rx ports */
	first_rx = allocate_dnoc_rx_ports(nocIf, MPPA_PCIE_NOC_RX_NB);
	if (first_rx < 0) {
		PCIE_RPC_ERR_MSG(answer, "Failed to allocate %d contiguous Rx ports\n",
				 MPPA_PCIE_NOC_RX_NB);
		return - 1;
	}

	clus_status->c2p_nocIf = nocIf;
	clus_status->c2p_min_rx = first_rx;
	clus_status->c2p_max_rx = first_rx + MPPA_PCIE_NOC_RX_NB - 1;
	clus_status->c2p_next_tag = first_rx;
	clus_status->c2p_credit = MPPA_PCIE_NOC_RX_NB;
	clus_status->c2p_remote_cnoc_rx = remote_cnoc_rx;
	clus_status->c2p_q = c2h_q;


	ret = mppa_routing_get_cnoc_unicast_route(mppa_rpc_odp_get_cluster_id(nocIf),
						   remoteClus, &clus_status->c2p_config,
						   &clus_status->c2p_header);
	clus_status->c2p_header._.tag = remote_cnoc_rx;

	ret = mppa_noc_cnoc_tx_configure(nocIf, cnoc_tx,
					 clus_status->c2p_config,
					 clus_status->c2p_header);
	if (ret != MPPA_NOC_RET_SUCCESS) {
		PCIE_RPC_ERR_MSG(answer, "Failed to configure CNoC Tx\n");
		return -1;
	}
	mppa_noc_cnoc_tx_push(nocIf, cnoc_tx, clus_status->c2p_credit);

	for (int rx_id = clus_status->c2p_min_rx; rx_id <= clus_status->c2p_max_rx; ++rx_id ) {
		ret = pcie_setup_rx(nocIf, rx_id, if_id, c2h_q, clus_status, answer);
		if (ret)
			return ret;
	}
	if (verbose)
		printf("if %d RXs [%u..%u] allocated for cluster %d\n",
		       nocIf, clus_status->c2p_min_rx, clus_status->c2p_max_rx, remoteClus);

	return 0;
}

int pcietool_enable_cluster(unsigned remoteClus, unsigned if_id,
			    mppa_rpc_odp_answer_t *answer)
{
	struct mpodp_if_config * cfg = netdev_get_eth_if_config(if_id);
	struct mpodp_h2c_entry entry;
	mppa_pcie_link_cluster_status_t *clus =
		&pcie_status.link[if_id].cluster[remoteClus];

	if (!clus->tx_enabled)
		return 0;

	entry.pkt_addr =
		(unsigned long)&mppa_dnoc[clus->p2c_nocIf]->tx_ports[clus->p2c_txId].push_data;

	if (netdev_h2c_enqueue_buffer(cfg, clus->p2c_q, &entry)) {
		PCIE_RPC_ERR_MSG(answer,
				 "Failed to register cluster %d to pcie interface %d\n",
				 remoteClus, if_id);
		return -1;
	}
	return 0;

}
