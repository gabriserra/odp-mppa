/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp_packet_io_internal.h>
#include <odp/api/cpu.h>
#include <odp/api/cpumask.h>
#include <odp/api/errno.h>
#include <errno.h>
#include <HAL/hal/core/mp.h>
#include <odp/rpc/api.h>
#include <odp/rpc/eth.h>
#include <HAL/hal/core/optimize.h>

#ifdef K1_NODEOS
#include <pthread.h>
#else
#include <utask.h>
#endif

#include <odp_classification_internal.h>
#include "odp_pool_internal.h"
#include "odp_macros_internal.h"
#include "odp_rx_internal.h"
#include "odp_tx_uc_internal.h"

#define MAX_ETH_SLOTS 2
#define MAX_ETH_PORTS 5
ODP_STATIC_ASSERT(MAX_ETH_PORTS * MAX_ETH_SLOTS <= MAX_RX_ETH_IF,
		   "MAX_RX_ETH_IF__ERROR");

#define N_RX_P_ETH 20
#define NOC_ETH_UC_COUNT 2

#include <mppa_noc.h>
#include <mppa_routing.h>

#include "ucode_fw/ucode_eth_v2.h"

typedef union eth_tx_metadata_s {
  uint64_t reg;
  uint64_t dword;
  uint32_t word[2];
  uint16_t hword[4];
  uint8_t bword[8];
  struct {
    uint32_t packet_size    : 14;
    uint32_t reserved_0     : 2;
    uint32_t lane_dest      : 2;
    uint32_t reserved_1     : 14;
    uint32_t pkt_sending_nb : 16;
    uint32_t pkt_grp_id     : 3;
    uint32_t ordered_en     : 1;
    uint32_t drop           : 1;
    uint32_t icrc_en        : 1;
    uint32_t reserved_2     : 10;
  } _;
} eth_tx_metadata_t;

/**
 * #############################
 * PKTIO Interface
 * #############################
 */

static tx_uc_ctx_t g_eth_tx_uc_ctx[NOC_ETH_UC_COUNT] = {{0}};

static inline tx_uc_ctx_t *eth_get_ctx(const pkt_eth_t *eth)
{
	const unsigned int tx_index =
		eth->tx_config.config._.first_dir % NOC_ETH_UC_COUNT;
	return &g_eth_tx_uc_ctx[tx_index];
}

static int eth_init(void)
{
	if (rx_thread_init())
		return 1;

	return 0;
}

static int eth_destroy(void)
{
	/* Last pktio to close should work. Expect an err code for others */
	rx_thread_destroy();
	return 0;
}

static int eth_rpc_send_eth_open(odp_pktio_param_t * params, pkt_eth_t *eth)
{
	unsigned cluster_id = __k1_get_cluster_id();
	mppa_rpc_odp_t *ack_msg;
	mppa_rpc_odp_ack_eth_t ack;
	int ret;
	uint8_t *payload;

	/*
	 * RPC Msg to IOETH  #N so the LB will dispatch to us
	 */
	mppa_rpc_odp_cmd_eth_open_t open_cmd = {
		{
			.ifId = eth->port_id,
			.dma_if = __k1_get_cluster_id() + eth->rx_config.dma_if,
			.min_rx = eth->rx_config.min_port,
			.max_rx = eth->rx_config.max_port,
			.loopback = eth->loopback,
			.jumbo = eth->jumbo,
			.rx_enabled = 1,
			.tx_enabled = 1,
			.nb_rules = eth->nb_rules,
			.verbose = eth->verbose,
			.min_payload = eth->min_payload,
			.max_payload = eth->max_payload,
		}
	};
	if (params) {
		if (params->in_mode == ODP_PKTIN_MODE_DISABLED)
			open_cmd.rx_enabled = 0;
		if (params->out_mode == ODP_PKTOUT_MODE_DISABLED)
			open_cmd.tx_enabled = 0;
	}
	mppa_rpc_odp_t cmd = {
		.data_len = eth->nb_rules * sizeof(pkt_rule_t),
		.pkt_class = MPPA_RPC_ODP_CLASS_ETH,
		.pkt_subtype = MPPA_RPC_ODP_CMD_ETH_OPEN,
		.cos_version = MPPA_RPC_ODP_ETH_VERSION,
		.inl_data = open_cmd.inl_data,
		.flags = 0,
	};

	mppa_rpc_odp_do_query(mppa_rpc_odp_get_io_dma_id(eth->slot_id, cluster_id),
					 mppa_rpc_odp_get_io_tag_id(cluster_id),
					 &cmd, eth->rules);

	ret = mppa_rpc_odp_wait_ack(&ack_msg, (void**)&payload, 30 * MPPA_RPC_ODP_TIMEOUT_1S, "[ETH]");
	if (ret <= 0)
		return 1;

	ack.inl_data = ack_msg->inl_data;
	if (ack.status) {
		fprintf(stderr, "[ETH] Error: Server declined opening of eth interface\n");
		if (ack_msg->err_str && ack_msg->data_len > 0)
			fprintf(stderr, "[ETH] Error Log: %s\n", payload);
		return 1;
	}

	eth->tx_if = ack.cmd.eth_open.tx_if;
	eth->tx_tag = ack.cmd.eth_open.tx_tag;
	eth->lb_ts_off = ack.cmd.eth_open.lb_ts_off;

	memcpy(eth->mac_addr, ack.cmd.eth_open.mac, 6);
	eth->mtu = ack.cmd.eth_open.mtu;

	return 0;
}

static int eth_open(odp_pktio_t id ODP_UNUSED, pktio_entry_t *pktio_entry,
		    const char *devname, odp_pool_t pool)
{
	int ret = 0;
	/*
	 * Check device name and extract slot/port
	 */
	const char* pptr = devname;
	char * eptr;
	int slot_id;

	if (*(pptr++) != 'e')
		return -1;

	slot_id = strtoul(pptr, &eptr, 10);
	if (eptr == pptr || slot_id < 0 || slot_id >= MAX_ETH_SLOTS) {
		ODP_ERR("Invalid Ethernet name %s\n", devname);
		return -1;
	}

	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	memset(eth, 0, sizeof(*eth));

	eth->slot_id = slot_id;
	rx_options_default(&eth->rx_opts);
	eth->rx_opts.nRx = N_RX_P_ETH;


	pptr = eptr;
	if (*pptr == 'p') {
		/* Found a port */
		pptr++;
		eth->port_id = strtoul(pptr, &eptr, 10);

		if (eptr == pptr || eth->port_id >= MAX_ETH_PORTS) {
			ODP_ERR("Invalid Ethernet name %s\n", devname);
			return -1;
		}
		pptr = eptr;
	} else {
		/* Default port is 4 (40G), but physically lane 0 */
		eth->port_id = 4;
	}

	while (*pptr == ':') {
		/* Parse arguments */
		pptr++;
		ret = rx_parse_options(&pptr, &eth->rx_opts);
		if (ret < 0)
			return -1;
		if (ret > 0)
			continue;
		if (!strncmp(pptr, "hashpolicy=", strlen("hashpolicy="))){
			if ( eth->nb_rules ) {
				ODP_ERR("hashpolicy can only be set once\n");
				return -1;
			}
			pptr += strlen("hashpolicy=");
			pptr = parse_hashpolicy(pptr, &eth->nb_rules,
						eth->rules, eth->port_id < 4 ? 2 : 8);
			if ( pptr == NULL ) {
				return -1;
			}
		} else if (!strncmp(pptr, "loop", strlen("loop"))){
			pptr += strlen("loop");
			eth->loopback = 1;
		} else if (!strncmp(pptr, "jumbo", strlen("jumbo"))){
			pptr += strlen("jumbo");
			eth->jumbo = 1;
		} else if (!strncmp(pptr, "verbose", strlen("verbose"))){
			pptr += strlen("verbose");
			eth->verbose = 1;
		} else if (!strncmp(pptr, "nofree", strlen("nofree"))){
			pptr += strlen("nofree");
			eth->tx_config.nofree = 1;
		} else if (!strncmp(pptr, "min_payload=", strlen("min_payload="))){
			pptr += strlen("min_payload=");
			eth->min_payload = strtoul(pptr, &eptr, 10);
			if(pptr == eptr){
				ODP_ERR("Invalid min_payload %s\n", pptr);
				return -1;
			}
			pptr = eptr;
		} else if (!strncmp(pptr, "max_payload=", strlen("max_payload="))){
			pptr += strlen("max_payload=");
			eth->max_payload = strtoul(pptr, &eptr, 10);
			if(pptr == eptr){
				ODP_ERR("Invalid max_payload %s\n", pptr);
				return -1;
			}
			pptr = eptr;
		} else if (!strncmp(pptr, "nowaitlink", strlen("nowaitlink"))){
			pptr += strlen("nowaitlink");
			eth->no_wait_link = 1;
		} else {
			/* Unknown parameter */
			ODP_ERR("Invalid option %s\n", pptr);
			return -1;
		}
	}
	if (*pptr != 0) {
		/* Garbage at the end of the name... */
		ODP_ERR("Invalid option %s\n", pptr);
		return -1;
	}
#ifdef MAGIC_SCALL
	ODP_ERR("Trying to invoke ETH interface in simulation. Use magic: interface type");
	return 1;
#endif

	if (eth->rx_opts.flow_controlled) {
		ODP_ERR("Cannot enable fc=1 on an ETH interface");
		return -1;
	}

	uintptr_t ucode;
	ucode = (uintptr_t)ucode_eth_v2;
	if (pktio_entry->s.param.in_mode != ODP_PKTIN_MODE_DISABLED) {
		/* Setup Rx threads */
		eth->rx_config.dma_if = 0;
		eth->rx_config.pool = pool;
		eth->rx_config.if_type = RX_IF_TYPE_ETH;
		eth->rx_config.pktio_id = RX_ETH_IF_BASE + slot_id * MAX_ETH_PORTS + eth->port_id;
		eth->rx_config.header_sz = sizeof(mppa_ethernet_header_t);
		ret = rx_thread_link_open(&eth->rx_config, &eth->rx_opts);
		if(ret < 0)
			return -1;
	}

	ret = eth_rpc_send_eth_open(&pktio_entry->s.param, eth);

	if (pktio_entry->s.param.out_mode != ODP_PKTOUT_MODE_DISABLED) {
		tx_uc_flags_t flags = TX_UC_FLAGS_DEFAULT;

		tx_uc_init(g_eth_tx_uc_ctx, NOC_ETH_UC_COUNT, ucode, flags, 0xf);

		mppa_routing_get_dnoc_unicast_route(__k1_get_cluster_id(),
						    eth->tx_if,
						    &eth->tx_config.config,
						    &eth->tx_config.header);

		eth->tx_config.config._.loopback_multicast = 0;
		eth->tx_config.config._.cfg_pe_en = 1;
		eth->tx_config.config._.cfg_user_en = 1;
		eth->tx_config.config._.write_pe_en = 1;
		eth->tx_config.config._.write_user_en = 1;
		eth->tx_config.config._.decounter_id = 0;
		eth->tx_config.config._.decounted = 0;
		eth->tx_config.config._.payload_min = 6;
		eth->tx_config.config._.payload_max = 32;
		eth->tx_config.config._.bw_current_credit = 0xff;
		eth->tx_config.config._.bw_max_credit     = 0xff;
		eth->tx_config.config._.bw_fast_delay     = 0x00;
		eth->tx_config.config._.bw_slow_delay     = 0x00;

		eth->tx_config.header._.multicast = 0;
		eth->tx_config.header._.tag = eth->tx_tag;
		eth->tx_config.header._.valid = 1;
	}

	return ret;
}

static int eth_close(pktio_entry_t * const pktio_entry)
{

	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	int slot_id = eth->slot_id;
	int port_id = eth->port_id;
	mppa_rpc_odp_t *ack_msg;
	mppa_rpc_odp_ack_eth_t ack;
	int ret;
	mppa_rpc_odp_cmd_eth_clos_t close_cmd = {
		{
			.ifId = port_id

		}
	};
	unsigned cluster_id = __k1_get_cluster_id();
	mppa_rpc_odp_t cmd = {
		.pkt_class = MPPA_RPC_ODP_CLASS_ETH,
		.pkt_subtype = MPPA_RPC_ODP_CMD_ETH_CLOS,
		.data_len = 0,
		.flags = 0,
		.cos_version = MPPA_RPC_ODP_ETH_VERSION,
		.inl_data = close_cmd.inl_data
	};
	uint8_t *payload;

	/* Free packets being sent by DMA */
	tx_uc_flush(eth_get_ctx(eth));

	mppa_rpc_odp_do_query(mppa_rpc_odp_get_io_dma_id(slot_id, cluster_id),
					 mppa_rpc_odp_get_io_tag_id(cluster_id),
					 &cmd, NULL);

	ret = mppa_rpc_odp_wait_ack(&ack_msg, (void**)&payload, 5 * MPPA_RPC_ODP_TIMEOUT_1S, "[ETH]");
	if (ret <= 0)
		return 1;

	ack.inl_data = ack_msg->inl_data;
	if (ack.status) {
		fprintf(stderr, "[ETH] Error: Server declined closure of eth interface\n");
		if (ack_msg->err_str && ack_msg->data_len > 0)
			fprintf(stderr, "[ETH] Error Log: %s\n", payload);
	}

	/* Push Context to handling threads */
	rx_thread_link_close(eth->rx_config.pktio_id);

	return ack.status;
}

static int eth_set_state(pktio_entry_t * const pktio_entry, int enabled)
{

	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	int slot_id = eth->slot_id;
	int port_id = eth->port_id;
	mppa_rpc_odp_t *ack_msg;
	mppa_rpc_odp_ack_eth_t ack;
	int ret;
	mppa_rpc_odp_cmd_eth_state_t state_cmd = {
		{
			.ifId = port_id,
			.enabled = enabled,
			.no_wait_link = eth->no_wait_link,
		}
	};
	unsigned cluster_id = __k1_get_cluster_id();
	mppa_rpc_odp_t cmd = {
		.pkt_class = MPPA_RPC_ODP_CLASS_ETH,
		.pkt_subtype = MPPA_RPC_ODP_CMD_ETH_STATE,
		.data_len = 0,
		.flags = 0,
		.cos_version = MPPA_RPC_ODP_ETH_VERSION,
		.inl_data = state_cmd.inl_data
	};
	uint8_t *payload;

	mppa_rpc_odp_do_query(mppa_rpc_odp_get_io_dma_id(slot_id, cluster_id),
					 mppa_rpc_odp_get_io_tag_id(cluster_id),
					 &cmd, NULL);

	ret = mppa_rpc_odp_wait_ack(&ack_msg, (void**)&payload, 20 * MPPA_RPC_ODP_TIMEOUT_1S, "[ETH]");
	if (ret <= 0)
		return 1;

	ack.inl_data = ack_msg->inl_data;
	if (ack.status) {
		fprintf(stderr, "[ETH] Error: Server declined change of eth state\n");
		if (ack_msg->err_str && ack_msg->data_len > 0)
			fprintf(stderr, "[ETH] Error Log: %s\n", payload);
		return 1;
	}

	return ack.status;
}

static int eth_start(pktio_entry_t * const pktio_entry)
{
	return eth_set_state(pktio_entry, 1);
}

static int eth_stop(pktio_entry_t * const pktio_entry)
{
	return eth_set_state(pktio_entry, 0);
}

static int eth_mac_addr_get(pktio_entry_t *pktio_entry,
			    void *mac_addr)
{
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	memcpy(mac_addr, eth->mac_addr, ETH_ALEN);
	return ETH_ALEN;
}




static int eth_recv(pktio_entry_t *pktio_entry, int index ODP_UNUSED,
		    odp_packet_t pkt_table[], unsigned len)
{
	int n_packet;
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	odp_buffer_ring_t *ring = rx_get_ring(&eth->rx_config);

	n_packet = odp_buffer_ring_get_multi(ring,
					     (odp_buffer_hdr_t **)pkt_table,
					     len, 0, NULL);

	for (int i = 0; i < n_packet; ++i) {
		odp_packet_t pkt = pkt_table[i];
		odp_packet_hdr_t *pkt_hdr = odp_packet_hdr(pkt);
		uint8_t * const base_addr =
			((uint8_t *)pkt_hdr->buf_hdr.addr) +
			pkt_hdr->headroom;

		INVALIDATE(pkt_hdr);
		packet_parse_reset(pkt_hdr);

		union mppa_ethernet_header_info_t info;
		uint8_t * const hdr_addr = base_addr -
			sizeof(mppa_ethernet_header_t);
		mppa_ethernet_header_t * const header =
			(mppa_ethernet_header_t *)hdr_addr;

		info.dword = LOAD_U64(header->info.dword);
		const unsigned frame_len =
			info._.pkt_size - sizeof(mppa_ethernet_header_t);
		pull_tail(pkt_hdr, pkt_hdr->frame_len - frame_len);
		packet_parse_l2(&pkt_hdr->p, frame_len);
		pkt_hdr->input = pktio_entry->s.handle;
	}

	if (n_packet && pktio_cls_enabled(pktio_entry))
		n_packet = _odp_pktio_classify(pktio_entry, index, pkt_table, n_packet);

	return n_packet;
}

static int eth_send(pktio_entry_t *pktio_entry, int index ODP_UNUSED,
		    const odp_packet_t pkt_table[], unsigned len)
{
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	tx_uc_ctx_t *ctx = eth_get_ctx(eth);

	return tx_uc_send_packets(&eth->tx_config, ctx,
				  pkt_table, len,
				  eth->mtu);
}

static int eth_promisc_mode_set(pktio_entry_t *const pktio_entry,
				odp_bool_t enable){
	/* FIXME */
	pktio_entry->s.pkt_eth.promisc = enable;
	return 0;
}

static int eth_promisc_mode(pktio_entry_t *const pktio_entry){
	return 	pktio_entry->s.pkt_eth.promisc;
}

static uint32_t eth_mtu_get(pktio_entry_t *const pktio_entry) {
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	return eth->mtu;
}

static mppa_rpc_odp_payload_eth_get_stat_t*
eth_rpc_stats(pkt_eth_t *eth, mppa_rpc_odp_ack_eth_get_stat_t *ack_stats)
{
	int ret;
	mppa_rpc_odp_payload_eth_get_stat_t *rpc_stats;
	mppa_rpc_odp_t *ack_msg;
	mppa_rpc_odp_ack_eth_t ack;
	unsigned cluster_id = __k1_get_cluster_id();
	uint8_t *payload;
	mppa_rpc_odp_cmd_eth_get_stat_t stat_cmd = {
		{
			.ifId = eth->port_id,
			.link_stats = 1,
		}
	};

	mppa_rpc_odp_t cmd = {
		.data_len = 0,
		.pkt_class = MPPA_RPC_ODP_CLASS_ETH,
		.pkt_subtype = MPPA_RPC_ODP_CMD_ETH_GET_STAT,
		.cos_version = MPPA_RPC_ODP_ETH_VERSION,
		.inl_data = stat_cmd.inl_data,
		.flags = 0,
	};

	mppa_rpc_odp_do_query(mppa_rpc_odp_get_io_dma_id(eth->slot_id, cluster_id),
					 mppa_rpc_odp_get_io_tag_id(cluster_id),
					 &cmd, NULL);

	ret = mppa_rpc_odp_wait_ack(&ack_msg, (void**)&payload, 2 * MPPA_RPC_ODP_TIMEOUT_1S, "[ETH]");
	if (ret <= 0)
		return NULL;

	ack.inl_data = ack_msg->inl_data;
	if (ack.status) {
		fprintf(stderr, "[ETH] Error: Server declined retrieval of eth stats\n");
		if (ack_msg->err_str && ack_msg->data_len > 0)
			fprintf(stderr, "[ETH] Error Log: %s\n", payload);
		return NULL;
	}

	if (ack_msg->data_len != sizeof(mppa_rpc_odp_payload_eth_get_stat_t))
		return NULL;

	rpc_stats = (mppa_rpc_odp_payload_eth_get_stat_t*)payload;
	if (ack_stats)
		*ack_stats = ack.cmd.eth_get_stat;

	return rpc_stats;
}

static int eth_stats(pktio_entry_t *const pktio_entry,
		     odp_pktio_stats_t *stats)
{
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	mppa_rpc_odp_payload_eth_get_stat_t *rpc_stats = eth_rpc_stats(eth, NULL);

	if (!rpc_stats)
		return -1;

	stats->in_octets         = rpc_stats->in_octets;
	stats->in_ucast_pkts     = rpc_stats->in_ucast_pkts;
	stats->in_discards       = rpc_stats->in_discards;
	/* stats->in_dropped        = 0; */
	stats->in_errors         = rpc_stats->in_errors;
	stats->in_unknown_protos = 0;
	stats->out_octets        = rpc_stats->out_octets;
	stats->out_ucast_pkts    = rpc_stats->out_ucast_pkts;
	stats->out_discards      = rpc_stats->out_discards;
	stats->out_errors        = rpc_stats->out_errors;

	if (rx_thread_fetch_stats(eth->rx_config.pktio_id,
				  &stats->in_unknown_protos, &stats->in_discards))
		return -1;

	return 0;
}

static int eth_link_status(pktio_entry_t *pktio_entry)
{
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	mppa_rpc_odp_ack_eth_get_stat_t ack_stat;
	mppa_rpc_odp_payload_eth_get_stat_t *rpc_stats = eth_rpc_stats(eth, &ack_stat);
	if (!rpc_stats)
		return -1;

	return ack_stat.link_status;
}

const pktio_if_ops_t eth_pktio_ops = {
	.name = "eth",
	.init = eth_init,
	.term = eth_destroy,
	.open = eth_open,
	.close = eth_close,
	.start = eth_start,
	.stop = eth_stop,
	.stats = eth_stats,
	.recv = eth_recv,
	.send = eth_send,
	.mtu_get = eth_mtu_get,
	.promisc_mode_set = eth_promisc_mode_set,
	.promisc_mode_get = eth_promisc_mode,
	.mac_get = eth_mac_addr_get,
	.link_status = eth_link_status,
};
