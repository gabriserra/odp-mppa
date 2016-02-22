/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <stdio.h>
#include <inttypes.h>
#include <HAL/hal/hal.h>

#ifndef BSP_NB_DMA_IO_MAX
#define BSP_NB_DMA_IO_MAX 1
#endif

#include "odp_rpc_internal.h"

#include <mppa_bsp.h>
#include <mppa_routing.h>
#include <mppa_noc.h>

static struct {
	odp_rpc_t rpc_cmd;
	uint8_t payload[RPC_MAX_PAYLOAD];
} odp_rpc_ack_buf;
static unsigned rx_port = -1;
static int rpc_default_server_id = -1;

int g_rpc_init = 0;

int odp_rpc_client_init(void){
	/* Already initialized */
	if(rx_port < (unsigned)(-1))
		return 0;

	mppa_noc_ret_t ret;
	mppa_noc_dnoc_rx_configuration_t conf = {0};

	ret = mppa_noc_dnoc_rx_alloc_auto(0, &rx_port, MPPA_NOC_BLOCKING);
	if (ret != MPPA_NOC_RET_SUCCESS)
		return -1;

	conf.buffer_base = (uintptr_t)&odp_rpc_ack_buf;
	conf.buffer_size = sizeof(odp_rpc_ack_buf),
	conf.activation = MPPA_NOC_ACTIVATED;
	conf.reload_mode = MPPA_NOC_RX_RELOAD_MODE_INCR_DATA_NOTIF;

	ret = mppa_noc_dnoc_rx_configure(0, rx_port, conf);
	g_rpc_init = 1;

	return 0;
}
int odp_rpc_client_term(void){
 	if (!g_rpc_init)
		return -1;

	mppa_noc_dnoc_rx_free(0, rx_port);

	return 0;
}

int odp_rpc_client_get_default_server(void)
{
	if (rpc_default_server_id >= 0)
		return rpc_default_server_id;

	int io_id = 0;
	if (__k1_spawn_type() == __MPPA_MPPA_SPAWN)
		io_id = __k1_spawner_id() / 128 - 1;

	rpc_default_server_id = odp_rpc_get_io_dma_id(io_id,
												  __k1_get_cluster_id());

	if (getenv("SYNC_IODDR_ID")) {
		rpc_default_server_id = atoi(getenv("SYNC_IODDR_ID"));
	}

	return rpc_default_server_id;
}

static const char * rpc_cmd_names[ODP_RPC_CMD_N_CMD] = {
	[ODP_RPC_CMD_BAS_INVL]    = "INVALID",
	[ODP_RPC_CMD_BAS_PING]    = "PING",
	[ODP_RPC_CMD_BAS_SYNC]    = "SYNC",
	[ODP_RPC_CMD_BAS_EXIT]    = "EXIT",
	[ODP_RPC_CMD_ETH_OPEN]    = "ETH OPEN",
	[ODP_RPC_CMD_ETH_CLOS]    = "ETH CLOSE",
	[ODP_RPC_CMD_ETH_PROMISC] = "ETH PROMISC",
	[ODP_RPC_CMD_PCIE_OPEN]   = "PCIE OPEN",
	[ODP_RPC_CMD_PCIE_CLOS]   = "PCIE CLOSE",
	[ODP_RPC_CMD_C2C_OPEN]    = "C2C OPEN",
	[ODP_RPC_CMD_C2C_CLOS]    = "C2C CLOSE",
	[ODP_RPC_CMD_C2C_QUERY]   = "C2C QUERY",
	[ODP_RPC_CMD_RND_GET]     = "RANDOM GET",
};
void odp_rpc_print_msg(const odp_rpc_t * cmd)
{
	if (cmd->pkt_type >=  ODP_RPC_CMD_N_CMD) {
		printf("Invalid Packet ! Type = %d\n", cmd->pkt_type);
		return;
	}
	printf("RPC CMD:\n"
	       "\tType: %u | %s\n"
	       "\tData: %u\n"
	       "\tDMA : %u\n"
	       "\tTag : %u\n"
	       "\tFlag: %x\n"
	       "\tInl Data:\n",
	       cmd->pkt_type, rpc_cmd_names[cmd->pkt_type],
	       cmd->data_len, cmd->dma_id,
	       cmd->dnoc_tag, cmd->flags);
	if (cmd->ack) {
		odp_rpc_cmd_ack_t ack = { .inl_data = cmd->inl_data };
		printf("\t\tstatus: %d\n", ack.status);
	}
	switch (cmd->pkt_type){
	case ODP_RPC_CMD_ETH_OPEN:
		{
			odp_rpc_cmd_eth_open_t open = { .inl_data = cmd->inl_data };
			printf("\t\tifId: %d\n"
				"\t\tRx(s): [%d:%d]\n"
				"\t\tLoopback: %d\n",
				open.ifId,
			       open.min_rx, open.max_rx,
			       open.loopback);
		}
		break;
	case ODP_RPC_CMD_ETH_CLOS:
		{
			odp_rpc_cmd_eth_clos_t clos = { .inl_data = cmd->inl_data };
			printf("\t\tifId: %d\n", clos.ifId);
		}
		break;
	case ODP_RPC_CMD_ETH_PROMISC:
		{
			odp_rpc_cmd_eth_promisc_t promisc = { .inl_data = cmd->inl_data };
			printf("\t\tifId: %d\n"
			       "\t\tEnabled: %d\n",
			       promisc.ifId, promisc.enabled);
		}
		break;
	case ODP_RPC_CMD_PCIE_OPEN:
		{
			odp_rpc_cmd_pcie_open_t open = { .inl_data = cmd->inl_data };
			printf("\t\tpcie_eth_id: %d\n"
				"\t\tRx(s): [%d:%d]\n",
				open.pcie_eth_if_id,
				open.min_rx, open.max_rx);
		}
		break;
	case ODP_RPC_CMD_PCIE_CLOS:
		{
			odp_rpc_cmd_eth_clos_t clos = { .inl_data = cmd->inl_data };
			printf("\t\tifId: %d\n", clos.ifId);
		}
		break;
	case ODP_RPC_CMD_C2C_OPEN:
		{
			odp_rpc_cmd_c2c_open_t open = { .inl_data = cmd->inl_data };
			printf("\t\tdstClus: %d\n"
				"\t\tRx(s): [%d:%d]\n"
				"\t\tMTU: %d\n",
			       open.cluster_id,
			       open.min_rx, open.max_rx,
			       open.mtu);
		}
		break;
	case ODP_RPC_CMD_C2C_CLOS:
		{
			odp_rpc_cmd_c2c_clos_t clos = { .inl_data = cmd->inl_data };
			printf("\t\tdstClus: %d\n", clos.cluster_id);
		}
		break;

	case ODP_RPC_CMD_RND_GET:
		{
			int i;
			for ( i = 0; i < 4; ++i ) {
				printf("%llx ", ((uint64_t*)cmd->inl_data.data)[i]);
			}
			printf("\n");
		}
		break;
	case ODP_RPC_CMD_BAS_INVL:
	case ODP_RPC_CMD_BAS_PING:
	case ODP_RPC_CMD_BAS_SYNC:
	case ODP_RPC_CMD_BAS_EXIT:
	default:
		break;
	}
}

int odp_rpc_send_msg(uint16_t local_interface, uint16_t dest_id,
		     uint16_t dest_tag, odp_rpc_t * cmd,
		     void * payload)
{
	if ( cmd->data_len > RPC_MAX_PAYLOAD ) {
		fprintf(stderr, "Error, msg payload %d > max payload %d\n", cmd->data_len, RPC_MAX_PAYLOAD);
		return 1;
	}
	mppa_noc_ret_t ret;
	mppa_routing_ret_t rret;
	unsigned tx_port;
	mppa_dnoc_channel_config_t config = {
		._ = {
			.loopback_multicast = 0,
			.cfg_pe_en = 1,
			.cfg_user_en = 1,
			.write_pe_en = 1,
			.write_user_en = 1,
			.decounter_id = 0,
			.decounted = 0,
			.payload_min = 1,
			.payload_max = 32,
			.bw_current_credit = 0xff,
			.bw_max_credit     = 0xff,
			.bw_fast_delay     = 0x00,
			.bw_slow_delay     = 0x00,
		}
	};
	mppa_dnoc_header_t header;

 	if (!g_rpc_init)
		return -1;

	__k1_wmb();
	ret = mppa_noc_dnoc_tx_alloc_auto(local_interface,
					  &tx_port, MPPA_NOC_BLOCKING);
	if (ret != MPPA_NOC_RET_SUCCESS)
		return 1;

	/* Get and configure route */
	header._.multicast = 0;
	header._.tag = dest_tag;
	header._.valid = 1;

	int externalAddress = __k1_get_cluster_id();
	if (local_interface >= 4) {
		externalAddress += 32 + (local_interface % 4);
	} else {
		externalAddress += local_interface;
	}
#ifdef K1B_EXPLORER
	externalAddress = __k1_get_cluster_id() + (local_interface % 4);
#endif
	rret = mppa_routing_get_dnoc_unicast_route(externalAddress,
						   dest_id, &config, &header);
	if (rret != MPPA_ROUTING_RET_SUCCESS)
		goto err_tx;

	ret = mppa_noc_dnoc_tx_configure(local_interface, tx_port,
					 header, config);
	if (ret != MPPA_NOC_RET_SUCCESS)
		goto err_tx;

	mppa_dnoc_push_offset_t off =
		{ ._ = { .offset = 0, .protocol = 1, .valid = 1 }};
	mppa_noc_dnoc_tx_set_push_offset(local_interface, tx_port, off);

	if (cmd->data_len) {
		mppa_noc_dnoc_tx_send_data(local_interface, tx_port,
					   sizeof(*cmd), cmd);
		mppa_noc_dnoc_tx_send_data_eot(local_interface, tx_port,
					   cmd->data_len, payload);
	} else {
		mppa_noc_dnoc_tx_send_data_eot(local_interface, tx_port,
					       sizeof(*cmd), cmd);
	}

	mppa_noc_dnoc_tx_free(local_interface, tx_port);
	return 0;

 err_tx:
	mppa_noc_dnoc_tx_free(local_interface, tx_port);
	return 1;
}

int odp_rpc_do_query(uint16_t dest_id,
		     uint16_t dest_tag, odp_rpc_t * cmd,
		     void * payload)
{
 	if (!g_rpc_init)
		return -1;

	cmd->dma_id = __k1_get_cluster_id();
	cmd->dnoc_tag = rx_port;
	return odp_rpc_send_msg(0, dest_id, dest_tag, cmd, payload);
}

int odp_rpc_wait_ack(odp_rpc_t ** cmd, void ** payload, uint64_t timeout)
{
	int ret;

 	if (!g_rpc_init)
		return -1;

	while ((ret = mppa_noc_dnoc_rx_lac_event_counter(0, rx_port)) == 0) {
		__k1_cpu_backoff(100);
		if (timeout < 110)
			break;
		timeout -= 110;

	}
	if (!ret)
		return 0;

	odp_rpc_t * msg = &odp_rpc_ack_buf.rpc_cmd;
	INVALIDATE(msg);
	*cmd = msg;

	if (payload && msg->data_len) {
		if ( msg->data_len > RPC_MAX_PAYLOAD ) {
			fprintf(stderr, "Error, msg payload %d > max payload %d\n", msg->data_len, RPC_MAX_PAYLOAD);
			return 1;
		}
		INVALIDATE_AREA(&odp_rpc_ack_buf.payload, msg->data_len);
		*payload = odp_rpc_ack_buf.payload;
	}

	return 1;

}

