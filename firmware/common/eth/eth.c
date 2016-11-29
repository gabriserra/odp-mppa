#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdio.h>
#include <HAL/hal/hal.h>
#include <odp/rpc/rpc.h>

#include "rpc-server.h"
#include "internal/rpc-server.h"
#include "internal/eth.h"
#include "internal/mac.h"

eth_status_t status[N_ETH_LANE];
eth_lb_status_t lb_status;

static inline int get_eth_dma_id(unsigned cluster_id){
	unsigned offset = (cluster_id / 4) % ETH_N_DMA_TX;

	if (cluster_id >= 128)
		offset = cluster_id % ETH_N_DMA_TX;

	return offset + ETH_BASE_TX;
}

void eth_open(unsigned remoteClus, mppa_rpc_odp_t *msg,
	      uint8_t *payload, unsigned fallthrough,
	      mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_eth_open_t data = { .inl_data = msg->inl_data };
	const int nocIf = get_eth_dma_id(data.dma_if);
	const unsigned int eth_if = data.ifId % 4; /* 4 is actually 0 in 40G mode */

	if(nocIf < 0) {
		ETH_RPC_ERR_MSG(answer, "Invalid NoC interface (%d %d)\n", nocIf, remoteClus);
		goto err;
	}
	if (data.ifId != 4 && data.ifId > N_ETH_LANE) {
		ETH_RPC_ERR_MSG(answer, "Bad lane id %d\n", data.ifId);
		goto err;
	}

	if (data.ifId == 4) {
		/* 40G port. We need to check all lanes */
		for (int i = 0; i < N_ETH_LANE; ++i) {
			if(status[i].cluster[remoteClus].opened != ETH_CLUS_STATUS_OFF) {
				ETH_RPC_ERR_MSG(answer, "Lane %d is already opened for cluster %d\n",
						i, remoteClus);
				goto err;
			}
		}
	} else {
		if (status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_OFF) {
			ETH_RPC_ERR_MSG(answer, "Lane %d is already opened for cluster %d\n",
					eth_if, remoteClus);
			goto err;
		}
		if (data.jumbo) {
			fprintf(stderr,
				"[ETH] Error: Trying to enable Jumbo on 1/10G lane %d\n",
				eth_if);
			goto err;

		}
	}

	if (fallthrough && !lb_status.dual_mac) {
		ETH_RPC_ERR_MSG(answer, "Trying to open in fallthrough with Dual-MAC mode disabled\n");
		goto err;
	}

	int externalAddress = mppa_rpc_odp_get_cluster_id(nocIf);

	status[eth_if].cluster[remoteClus].rx_enabled = data.rx_enabled;
	status[eth_if].cluster[remoteClus].tx_enabled = data.tx_enabled;
	status[eth_if].cluster[remoteClus].jumbo = data.jumbo;

	if (ethtool_open_cluster(remoteClus, data.ifId, answer))
		goto err;
	if (ethtool_setup_eth2clus(remoteClus, data.ifId, nocIf, externalAddress,
				   data.min_rx, data.max_rx,
				   data.min_payload, data.max_payload, answer))
		goto err;
	if (ethtool_setup_clus2eth(remoteClus, data.ifId, nocIf, answer))
		goto err;

	if ( ethtool_configure_policy(remoteClus, data.ifId, fallthrough,
				      data.nb_rules, (pkt_rule_t*)payload,
				      answer))
		goto err;

	if (ethtool_start_lane(data.ifId, data.loopback, data.verbose, answer))
		goto err;

	answer->ack.cmd.eth_open.tx_if = externalAddress;
	answer->ack.cmd.eth_open.tx_tag = status[eth_if].cluster[remoteClus].rx_tag;
	if (data.jumbo) {
		answer->ack.cmd.eth_open.mtu = 9000;
	} else {
		answer->ack.cmd.eth_open.mtu = 1600;
	}

	if (!lb_status.dual_mac || fallthrough) {
		memcpy(answer->ack.cmd.eth_open.mac, status[eth_if].mac_address[0], ETH_ALEN);
	} else {
		memcpy(answer->ack.cmd.eth_open.mac, status[eth_if].mac_address[1], ETH_ALEN);
	}

	answer->ack.cmd.eth_open.lb_ts_off = lb_timestamp;

	return;
 err:
	ethtool_close_cluster(remoteClus, data.ifId, NULL);
	return;
}

void eth_set_state(unsigned remoteClus, mppa_rpc_odp_t *msg,
		   mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_eth_state_t data = { .inl_data = msg->inl_data };
	const unsigned int eth_if = data.ifId % 4; /* 4 is actually 0 in 40G mode */

	if (data.ifId != 4 && data.ifId > N_ETH_LANE) {
		ETH_RPC_ERR_MSG(answer, "Bad lane id %d\n", data.ifId);
		return;
	}

	if (data.ifId == 4) {
		if(status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_40G) {
			ETH_RPC_ERR_MSG(answer, "Tring to set state for 40G lane while lane is closed or in a different mode\n");
			return;
		}
	} else {
		if(status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_ON) {
			ETH_RPC_ERR_MSG(answer, "Tring to set state for 1/10G lane while lane is closed or in 40G\n");
			return;
		}
	}

	if (data.enabled) {
		if (ethtool_enable_cluster(remoteClus, data.ifId, answer)) {
			return;
		}
	} else {
		if (ethtool_disable_cluster(remoteClus, data.ifId, answer)) {
			return;
		}
	}

	return;
}

void eth_close(unsigned remoteClus, mppa_rpc_odp_t *msg,
	       mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_eth_clos_t data = { .inl_data = msg->inl_data };
	const unsigned int eth_if = data.ifId % 4; /* 4 is actually 0 in 40G mode */

	if (data.ifId != 4 && data.ifId > N_ETH_LANE) {
		ETH_RPC_ERR_MSG(answer, "Bad lane id %d\n", data.ifId);
		return;
	}

	if (data.ifId == 4) {
		if(status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_40G) {
			ETH_RPC_ERR_MSG(answer, "Tring to close 40G lane while lane is closed or in a different mode\n");
			return;
		}
	} else {
		if(status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_ON) {
			ETH_RPC_ERR_MSG(answer, "Tring to set state for 1/10G lane while lane is closed or in 40G\n");
			return;
		}
	}

	if (status[eth_if].cluster[remoteClus].enabled)
		if (ethtool_disable_cluster(remoteClus, data.ifId, answer))
			return;
	if (ethtool_close_cluster(remoteClus, data.ifId, answer))
		return;

	if (data.ifId == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i) {
			_eth_cluster_status_init(&status[i].cluster[remoteClus]);
		}
	} else {
		_eth_cluster_status_init(&status[eth_if].cluster[remoteClus]);
	}

	return;
}

void eth_dual_mac(unsigned remoteClus __attribute__((unused)),
		  mppa_rpc_odp_t *msg,
		  mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_eth_dual_mac_t data = { .inl_data = msg->inl_data };
	ethtool_set_dual_mac(data.enabled, answer);
	return;
}

void eth_get_stat(unsigned remoteClus __attribute__((unused)),
			   mppa_rpc_odp_t *msg,
			   mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_eth_get_stat_t data = { .inl_data = msg->inl_data };

	if (data.ifId != 4 && data.ifId > N_ETH_LANE) {
		ETH_RPC_ERR_MSG(answer, "Bad lane id %d\n", data.ifId);
		return;
	}
	if (data.ifId == 4 && status[0].initialized != ETH_LANE_ON_40G) {
		ETH_RPC_ERR_MSG(answer, "Trying to get stats on 40G lane while lane is closed or in a different mode\n");
		return;
	} else if (data.ifId < N_ETH_LANE &&
		   status[data.ifId].initialized !=! ETH_LANE_ON) {
		   ETH_RPC_ERR_MSG(answer, "Trying to get stats for 1/10G lane while lane is closed or in 40G\n");
		   return;
	}
	answer->ack.cmd.eth_get_stat.link_status = ethtool_poll_lane(data.ifId);

	if (data.link_stats) {
		ethtool_lane_stats(data.ifId, answer);
	}
	return;
}
static void eth_init(void)
{
	_eth_lb_status_init(&lb_status);
	for (int eth_if = 0; eth_if < N_ETH_LANE; ++eth_if) {
		_eth_status_init(&status[eth_if]);
		ethtool_init_lane(eth_if);

		int eth_clus_id = 160;
		if (__k1_get_cluster_id() == 192 || __k1_get_cluster_id() == 224)
			eth_clus_id = 224;
		mppa_ethernet_generate_mac(eth_clus_id, eth_if,
					   status[eth_if].mac_address[0]);
		memcpy(status[eth_if].mac_address[1], status[eth_if].mac_address[0], ETH_ALEN);
		status[eth_if].mac_address[1][ETH_ALEN - 1] |= 1;
	}
}

static int eth_rpc_handler(unsigned remoteClus, mppa_rpc_odp_t *msg, uint8_t *payload)
{
	mppa_rpc_odp_answer_t answer = MPPA_RPC_ODP_ANSWER_INITIALIZER(msg);

	if (msg->pkt_class != MPPA_RPC_ODP_CLASS_ETH)
		return -MPPA_RPC_ODP_ERR_INTERNAL_ERROR;
	if (msg->cos_version != MPPA_RPC_ODP_ETH_VERSION)
		return -MPPA_RPC_ODP_ERR_VERSION_MISMATCH;

	switch (msg->pkt_subtype){
	case MPPA_RPC_ODP_CMD_ETH_OPEN:
		eth_open(remoteClus, msg, payload, 0, &answer);
		break;
	case MPPA_RPC_ODP_CMD_ETH_STATE:
		eth_set_state(remoteClus, msg, &answer);
		break;
	case MPPA_RPC_ODP_CMD_ETH_CLOS:
	case MPPA_RPC_ODP_CMD_ETH_CLOS_DEF:
		eth_close(remoteClus, msg, &answer);
		break;
	case MPPA_RPC_ODP_CMD_ETH_OPEN_DEF:
		eth_open(remoteClus, msg, payload, 1, &answer);
		break;
	case MPPA_RPC_ODP_CMD_ETH_DUAL_MAC:
		eth_dual_mac(remoteClus, msg, &answer);
		break;
	case MPPA_RPC_ODP_CMD_ETH_GET_STAT:
		eth_get_stat(remoteClus, msg, &answer);
		break;
	default:
		return -MPPA_RPC_ODP_ERR_BAD_SUBTYPE;
	}

	mppa_rpc_odp_server_ack(&answer);
	return -MPPA_RPC_ODP_ERR_NONE;
}

void  __attribute__ ((constructor)) __eth_rpc_constructor()
{
#if defined(MAGIC_SCALL)
	return;
#endif

	eth_init();
	__rpc_handlers[MPPA_RPC_ODP_CLASS_ETH] = eth_rpc_handler;
}
