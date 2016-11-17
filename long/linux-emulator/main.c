#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <mppa_noc.h>
#include <odp/rpc/rpc.h>
#include <odp/rpc/api.h>
#include <odp/rpc/eth.h>

typedef struct {
	uint8_t dma_if;          /**< DMA Rx Interface */
	uint8_t min_port;
	uint8_t max_port;
} rx_config_t;

int rx_open(rx_config_t *rx_config, int n_ports)
{
	const int dma_if = 0;

	/*
	 * Allocate contiguous RX ports
	 */
	int n_rx = 0, first_rx;

	for (first_rx = 0; first_rx <  MPPA_DNOC_RX_QUEUES_NUMBER - n_ports;
	     ++first_rx) {
		for (n_rx = 0; n_rx < n_ports; ++n_rx) {
			mppa_noc_ret_t ret;
			ret = mppa_noc_dnoc_rx_alloc(dma_if,
						     first_rx + n_rx);
			if (ret != MPPA_NOC_RET_SUCCESS)
				break;
		}
		if (n_rx < n_ports) {
			n_rx--;
			for ( ; n_rx >= 0; --n_rx) {
				mppa_noc_dnoc_rx_free(dma_if,
						      first_rx + n_rx);
			}
		} else {
			break;
		}
	}
	if (n_rx < n_ports) {
		assert(n_rx == 0);
		fprintf(stderr, "failed to allocate %d contiguous Rx ports\n", n_ports);
		return -1;
	}

	rx_config->dma_if = dma_if;
	rx_config->min_port = first_rx;
	rx_config->max_port = first_rx + n_rx - 1;
	return 0;
}

int main(){
	int ret;
	unsigned cluster_id = __k1_get_cluster_id();
	mppa_rpc_odp_t *ack_msg;
	rx_config_t rx_config;
	mppa_rpc_odp_ack_eth_t ack;

	for (volatile int i = 0; i < 20000000; ++i);
	mppa_rpc_odp_client_init();
	{
		mppa_rpc_odp_cmd_eth_dual_mac_t mac_cmd = {
			.enabled = 1
		};
		mppa_rpc_odp_t cmd = {
			.data_len = 0,
			.pkt_class = MPPA_RPC_ODP_CLASS_ETH,
			.pkt_subtype = MPPA_RPC_ODP_CMD_ETH_DUAL_MAC,
			.cos_version = MPPA_RPC_ODP_ETH_VERSION,
			.inl_data = mac_cmd.inl_data,
			.flags = 0,
		};

		mppa_rpc_odp_do_query(mppa_rpc_odp_get_io_dma_id(1, cluster_id),
					 mppa_rpc_odp_get_io_tag_id(cluster_id),
					 &cmd, NULL);
		ret = mppa_rpc_odp_wait_ack(&ack_msg, NULL, 15 * MPPA_RPC_ODP_TIMEOUT_1S, "[ETH]");
		if (ret <= 0)
			return 1;

		ack.inl_data = ack_msg->inl_data;
		if (ack.status) {
			fprintf(stderr, "[ETH] Error: Server declined dual mac mode\n");
			return 1;
		}
	}

	ret = rx_open(&rx_config, 20);
	if (ret) {
		fprintf(stderr, "Failed to open Rx ports\n");
	}

	for (int dma = 0; dma < 4; ++dma){
		{
			/*
			 * RPC Msg to IOETH  #N so the LB will dispatch to us
			 */
			mppa_rpc_odp_cmd_eth_open_t open_cmd = {
				{
					.ifId = 4,
					.dma_if = cluster_id + dma,
					.min_rx = rx_config.min_port,
					.max_rx = rx_config.max_port,
					.loopback = 1,
					.jumbo = 0,
					.rx_enabled = 1,
					.tx_enabled = 1,
					.nb_rules = 0,
				}
			};
			mppa_rpc_odp_t cmd = {
				.data_len = 0,
				.pkt_class = MPPA_RPC_ODP_CLASS_ETH,
				.pkt_subtype = MPPA_RPC_ODP_CMD_ETH_OPEN_DEF,
				.cos_version = MPPA_RPC_ODP_ETH_VERSION,
				.inl_data = open_cmd.inl_data,
				.flags = 0,
			};

			mppa_rpc_odp_do_query(mppa_rpc_odp_get_io_dma_id(1, cluster_id),
							 mppa_rpc_odp_get_io_tag_id(cluster_id),
							 &cmd, NULL);

			ret = mppa_rpc_odp_wait_ack(&ack_msg, NULL, -1, "[ETH]");
			if (ret <= 0)
				return 1;

			ack.inl_data = ack_msg->inl_data;
			if (ack.status) {
				fprintf(stderr, "[ETH] Error: Server declined opening of eth interface\n");
				return 1;
			}
		}
		printf("Ethernet =>%d  opened successfully\n", dma);

		{
			/*
			 * RPC Msg to IOETH  #N so the LB will dispatch to us
			 */
			mppa_rpc_odp_cmd_eth_clos_t close_cmd = {
				{
					.ifId = 4
				}
			};
			mppa_rpc_odp_t cmd = {
				.pkt_class = MPPA_RPC_ODP_CLASS_ETH,
				.pkt_subtype = MPPA_RPC_ODP_CMD_ETH_CLOS,
				.cos_version = MPPA_RPC_ODP_ETH_VERSION,
				.data_len = 0,
				.flags = 0,
				.inl_data = close_cmd.inl_data
			};
			mppa_rpc_odp_do_query(mppa_rpc_odp_get_io_dma_id(1, cluster_id),
							 mppa_rpc_odp_get_io_tag_id(cluster_id),
							 &cmd, NULL);

			ret = mppa_rpc_odp_wait_ack(&ack_msg, NULL, -1, "[ETH]");
			if (ret <= 0)
				return 1;

			ack.inl_data = ack_msg->inl_data;
			if (ack.status) {
				fprintf(stderr, "[ETH] Error: Server declined closing of eth interface\n");
				return 1;
			}
		}
		printf("Ethernet =>%d closed successfully\n", dma);
	}
	return 0;
}
