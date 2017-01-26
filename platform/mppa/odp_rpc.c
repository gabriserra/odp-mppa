/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <stdio.h>
#include <inttypes.h>
#include <HAL/hal/hal.h>
#include <odp/rpc/api.h>

#include <mppa_bsp.h>
#include <mppa_routing.h>
#include <mppa_noc.h>

#define INVALIDATE_AREA(p, s) do {	__k1_dcache_invalidate_mem_area((__k1_uintptr_t)(void*)p, s);	\
	}while(0)
#define INVALIDATE(p) INVALIDATE_AREA((p), sizeof(*p))

static struct {
	mppa_rpc_odp_t rpc_cmd;
	uint8_t payload[RPC_MAX_PAYLOAD];
} mppa_rpc_odp_ack_buf;
static unsigned rx_port = -1;
static int rpc_default_server_id = -1;

int g_rpc_init = 0;

int mppa_rpc_odp_client_init(void){
	/* Already initialized */
	if(rx_port < (unsigned)(-1))
		return 0;

	mppa_noc_ret_t ret;
	mppa_noc_dnoc_rx_configuration_t conf = {0};

	ret = mppa_noc_dnoc_rx_alloc_auto(0, &rx_port, MPPA_NOC_BLOCKING);
	if (ret != MPPA_NOC_RET_SUCCESS)
		return -1;

	conf.buffer_base = (uintptr_t)&mppa_rpc_odp_ack_buf;
	conf.buffer_size = sizeof(mppa_rpc_odp_ack_buf),
	conf.activation = MPPA_NOC_ACTIVATED;
	conf.reload_mode = MPPA_NOC_RX_RELOAD_MODE_INCR_DATA_NOTIF;

	ret = mppa_noc_dnoc_rx_configure(0, rx_port, conf);
	g_rpc_init = 1;

	return 0;
}
int mppa_rpc_odp_client_term(void){
 	if (!g_rpc_init)
		return -1;

	mppa_noc_dnoc_rx_free(0, rx_port);
	g_rpc_init = 0;
	rx_port = -1;

	return 0;
}

int mppa_rpc_odp_client_get_default_server(void)
{
	if (rpc_default_server_id >= 0)
		return rpc_default_server_id;

	int io_id = 0;
	if (__k1_spawn_type() == __MPPA_MPPA_SPAWN)
		io_id = __k1_spawner_id() / 128 - 1;

	rpc_default_server_id = mppa_rpc_odp_get_io_dma_id(io_id,
						      __k1_get_cluster_id());

	if (getenv("SYNC_IODDR_ID")) {
		rpc_default_server_id = atoi(getenv("SYNC_IODDR_ID"))
			+ mppa_rpc_odp_get_dma_offset(__k1_get_cluster_id());
	}

	return rpc_default_server_id;
}

static const char * const rpc_cmd_bas_names[MPPA_RPC_ODP_CMD_BAS_N_CMD] = {
	MPPA_RPC_ODP_CMD_NAMES_BAS
};

static const char * const rpc_cmd_eth_names[MPPA_RPC_ODP_CMD_ETH_N_CMD] = {
	MPPA_RPC_ODP_CMD_NAMES_ETH
};

static const char * const rpc_cmd_pcie_names[MPPA_RPC_ODP_CMD_PCIE_N_CMD] = {
	MPPA_RPC_ODP_CMD_NAMES_PCIE
};

static const char * const rpc_cmd_c2c_names[MPPA_RPC_ODP_CMD_C2C_N_CMD] = {
	MPPA_RPC_ODP_CMD_NAMES_C2C
};

static const char * const rpc_cmd_rnd_names[MPPA_RPC_ODP_CMD_RND_N_CMD] = {
	MPPA_RPC_ODP_CMD_NAMES_RND
};

static const char * const rpc_cmd_fp_names[MPPA_RPC_ODP_CMD_FP_N_CMD] = {
	MPPA_RPC_ODP_CMD_NAMES_FP
};

static const char * const * const rpc_cmd_names[MPPA_RPC_ODP_N_CLASS] = {
	[MPPA_RPC_ODP_CLASS_BAS] = rpc_cmd_bas_names,
	[MPPA_RPC_ODP_CLASS_ETH] = rpc_cmd_eth_names,
	[MPPA_RPC_ODP_CLASS_PCIE] = rpc_cmd_pcie_names,
	[MPPA_RPC_ODP_CLASS_C2C] = rpc_cmd_c2c_names,
	[MPPA_RPC_ODP_CLASS_RND] = rpc_cmd_rnd_names,
	[MPPA_RPC_ODP_CLASS_FP] = rpc_cmd_fp_names,
};

static const int rpc_n_cmds[MPPA_RPC_ODP_N_CLASS] = {
	[MPPA_RPC_ODP_CLASS_BAS] = MPPA_RPC_ODP_CMD_BAS_N_CMD,
	[MPPA_RPC_ODP_CLASS_ETH] = MPPA_RPC_ODP_CMD_ETH_N_CMD,
	[MPPA_RPC_ODP_CLASS_PCIE] = MPPA_RPC_ODP_CMD_PCIE_N_CMD,
	[MPPA_RPC_ODP_CLASS_C2C] = MPPA_RPC_ODP_CMD_C2C_N_CMD,
	[MPPA_RPC_ODP_CLASS_RND] = MPPA_RPC_ODP_CMD_RND_N_CMD,
	[MPPA_RPC_ODP_CLASS_FP] = MPPA_RPC_ODP_CMD_FP_N_CMD,
};

void mppa_rpc_odp_print_msg(const mppa_rpc_odp_t * cmd, const uint8_t *payload)
{
	if (cmd->pkt_class >=  MPPA_RPC_ODP_N_CLASS) {
		printf("Invalid packet class ! Class = %d\n", cmd->pkt_class);
		return;
	}
	if (cmd->pkt_subtype >= rpc_n_cmds[cmd->pkt_class]){
		printf("Invalid packet subtype ! Class = %d, Subtype = %u\n", cmd->pkt_class, cmd->pkt_subtype);
		return;
	}

	printf("RPC CMD:\n"
	       "\tClass: %u\n"
	       "\tType : %u | %s\n"
	       "\tData : %u\n"
	       "\tDMA  : %u\n"
	       "\tTag  : %u\n"
	       "\tFlag :\n"
	       "\t\tAck : %u\n"
	       "\t\tErrStr : %u\n"
	       "\t\tRPC Err: : %x\n"
	       "\tInl Data:\n",
	       cmd->pkt_class, cmd->pkt_subtype,
	       rpc_cmd_names[cmd->pkt_class][cmd->pkt_subtype],
	       cmd->data_len, cmd->dma_id,
	       cmd->dnoc_tag, cmd->ack, cmd->err_str, cmd->rpc_err);

	if (cmd->ack) {
		mppa_rpc_odp_ack_t ack = { .inl_data = cmd->inl_data };
		printf("\t\tAck status: %d\n", ack.status);
	}

	switch (cmd->pkt_class){
	case MPPA_RPC_ODP_CLASS_BAS:
		break;
	case MPPA_RPC_ODP_CLASS_ETH:
		switch(cmd->pkt_subtype) {
		case MPPA_RPC_ODP_CMD_ETH_OPEN:
		case MPPA_RPC_ODP_CMD_ETH_OPEN_DEF:
			{
				mppa_rpc_odp_cmd_eth_open_t open = { .inl_data = cmd->inl_data };
				printf("\t\tifId: %d\n"
				       "\t\tRx(s): [%d:%d]\n"
				       "\t\tLoopback: %u\n"
				       "\t\tRx Enbl : %d\n"
				       "\t\tTx Enbl : %d\n"
				       "\t\tJumbo   : %u\n"
				       "\t\tNbRules : %u\n",
				       open.ifId,
				       open.min_rx, open.max_rx,
				       open.loopback,
				       open.rx_enabled, open.tx_enabled,
				       open.jumbo, open.nb_rules);
			}
			break;
		case MPPA_RPC_ODP_CMD_ETH_CLOS:
		case MPPA_RPC_ODP_CMD_ETH_CLOS_DEF:
			{
				mppa_rpc_odp_cmd_eth_clos_t clos = { .inl_data = cmd->inl_data };
				printf("\t\tifId: %d\n", clos.ifId);
			}
			break;
		case MPPA_RPC_ODP_CMD_ETH_DUAL_MAC:
			{
				mppa_rpc_odp_cmd_eth_dual_mac_t dmac = { .inl_data = cmd->inl_data };
				printf("\t\tenabled: %d\n", dmac.enabled);
			}
			break;
		case MPPA_RPC_ODP_CMD_ETH_PROMISC:
			{
				mppa_rpc_odp_cmd_eth_promisc_t promisc = { .inl_data = cmd->inl_data };
				printf("\t\tifId: %d\n"
				       "\t\tEnabled: %d\n",
				       promisc.ifId, promisc.enabled);
			}
			break;
		case MPPA_RPC_ODP_CMD_ETH_STATE:
			{
				mppa_rpc_odp_cmd_eth_state_t state = { .inl_data = cmd->inl_data };
				printf("\t\tifId: %d\n"
				       "\t\tEnabled: %d\n"
				       "\t\tNoWaitLink: %d\n",
				       state.ifId, state.enabled, state.no_wait_link);
			}
			break;
		case MPPA_RPC_ODP_CMD_ETH_GET_STAT:
			{
				mppa_rpc_odp_cmd_eth_get_stat_t stats =
					{ .inl_data = cmd->inl_data };
				printf("\t\tifId: %d\n"
				       "\t\tLaneStats: %d\n",
				       stats.ifId, stats.link_stats);
			}
			break;
		default:
			break;
		}
		break;
	case MPPA_RPC_ODP_CLASS_PCIE:
		switch(cmd->pkt_subtype) {
		case MPPA_RPC_ODP_CMD_PCIE_OPEN:
			{
				mppa_rpc_odp_cmd_pcie_open_t open = { .inl_data = cmd->inl_data };
				printf("\t\tpcie_eth_id: %d\n"
				       "\t\tRx(s): [%d:%d]\n",
				       open.pcie_eth_if_id,
				       open.min_rx, open.max_rx);
			}
			break;
		case MPPA_RPC_ODP_CMD_PCIE_CLOS:
			{
				mppa_rpc_odp_cmd_eth_clos_t clos = { .inl_data = cmd->inl_data };
				printf("\t\tifId: %d\n", clos.ifId);
			}
			break;
		default:
			break;
		}
		break;
	case MPPA_RPC_ODP_CLASS_C2C:
		switch(cmd->pkt_subtype) {
		case MPPA_RPC_ODP_CMD_C2C_OPEN:
			{
				mppa_rpc_odp_cmd_c2c_open_t open = { .inl_data = cmd->inl_data };
				printf("\t\tdstClus: %d\n"
				       "\t\tRx(s): [%d:%d]\n"
				       "\t\tMTU: %d\n",
				       open.cluster_id,
				       open.min_rx, open.max_rx,
				       open.mtu);
			}
			break;
		case MPPA_RPC_ODP_CMD_C2C_CLOS:
			{
				mppa_rpc_odp_cmd_c2c_clos_t clos = { .inl_data = cmd->inl_data };
				printf("\t\tdstClus: %d\n", clos.cluster_id);
			}
			break;
		default:
			break;
		}
		break;
	case MPPA_RPC_ODP_CLASS_RND:
		switch(cmd->pkt_subtype) {
		case MPPA_RPC_ODP_CMD_RND_GET:
			{
				int i;
				for ( i = 0; i < 4; ++i ) {
					printf("%llx ", ((uint64_t*)cmd->inl_data.data)[i]);
				}
				printf("\n");
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	if (cmd->err_str && cmd->data_len > 0 && payload) {
		printf("\tError string:\n\t\t%s", payload);
	}
}

int mppa_rpc_odp_send_msg(uint16_t local_interface, uint16_t dest_id,
		     uint16_t dest_tag, const mppa_rpc_odp_t * cmd,
		     const void * payload)
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

	int externalAddress = mppa_rpc_odp_get_cluster_id(local_interface);

#ifdef VERBOSE
	printf("[RPC] Sending message from %d (%d) to %d/%d\n",
	       local_interface, externalAddress, dest_id, dest_tag);
	mppa_rpc_odp_print_msg(cmd, payload);
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

int mppa_rpc_odp_do_query(uint16_t dest_id,
		     uint16_t dest_tag, mppa_rpc_odp_t * cmd,
		     void * payload)
{
 	if (!g_rpc_init)
		return -1;

	cmd->dma_id = __k1_get_cluster_id();
	cmd->dnoc_tag = rx_port;
	return mppa_rpc_odp_send_msg(0, dest_id, dest_tag, cmd, payload);
}

mppa_rpc_odp_cmd_err_e mppa_rpc_odp_wait_ack(mppa_rpc_odp_t ** cmd, void ** payload, uint64_t timeout,
				   const char* mod)
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
	if (!ret) {
		fprintf(stderr, "%s Query timed out\n", mod);
		return -MPPA_RPC_ODP_ERR_TIMEOUT;
	}

	mppa_rpc_odp_t * msg = &mppa_rpc_odp_ack_buf.rpc_cmd;
	INVALIDATE(msg);
	*cmd = msg;


	if (payload && msg->data_len) {
		if ( msg->data_len > RPC_MAX_PAYLOAD ) {
			fprintf(stderr, "Error, msg payload %d > max payload %d\n",
				msg->data_len, RPC_MAX_PAYLOAD);
			return -1;
		}
		INVALIDATE_AREA(&mppa_rpc_odp_ack_buf.payload, msg->data_len);
		*payload = mppa_rpc_odp_ack_buf.payload;
	}

	/* Handle protocol error */
	if (msg->rpc_err) {
		fprintf(stderr, "%s RPC Error\n", mod);
		if (msg->err_str && msg->data_len > 0)
			fprintf(stderr, "\tError Log: %s\n", (char*)(*payload));

		return -msg->rpc_err;
	}


	return 1;

}

