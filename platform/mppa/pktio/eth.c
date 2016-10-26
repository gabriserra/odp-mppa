/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp_packet_io_internal.h>
#include <odp/thread.h>
#include <odp/cpumask.h>
#include <HAL/hal/hal.h>
#include <odp/errno.h>
#include <errno.h>
#include <odp/rpc/api.h>

#ifdef K1_NODEOS
#include <pthread.h>
#else
#include <utask.h>
#endif

#include <odp_classification_internal.h>
#include "odp_pool_internal.h"
#include "odp_rx_internal.h"
#include "odp_tx_uc_internal.h"

#define MAX_ETH_SLOTS 2
#define MAX_ETH_PORTS 5
_ODP_STATIC_ASSERT(MAX_ETH_PORTS * MAX_ETH_SLOTS <= MAX_RX_ETH_IF,
		   "MAX_RX_ETH_IF__ERROR");

#define N_RX_P_ETH 20
#define NOC_ETH_UC_COUNT 2

#include <mppa_noc.h>
#include <mppa_routing.h>

#include "ucode_fw/ucode_eth_v2.h"

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

static int eth_rpc_send_eth_open(odp_pktio_param_t * params, pkt_eth_t *eth, int nb_rules, pkt_rule_t *rules)
{
	unsigned cluster_id = __k1_get_cluster_id();
	mppa_rpc_odp_t *ack_msg;
	mppa_rpc_odp_ack_t ack;
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
			.nb_rules = nb_rules,
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
		.data_len = nb_rules * sizeof(pkt_rule_t),
		.pkt_class = MPPA_RPC_ODP_CLASS_ETH,
		.pkt_subtype = MPPA_RPC_ODP_CMD_ETH_OPEN,
		.cos_version = MPPA_RPC_ODP_ETH_VERSION,
		.inl_data = open_cmd.inl_data,
		.flags = 0,
	};

	mppa_rpc_odp_do_query(mppa_rpc_odp_get_io_dma_id(eth->slot_id, cluster_id),
					 mppa_rpc_odp_get_io_tag_id(cluster_id),
					 &cmd, rules);

	ret = mppa_rpc_odp_wait_ack(&ack_msg, (void**)&payload, 15 * MPPA_RPC_ODP_TIMEOUT_1S, "[ETH]");
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

#define PARSE_HASH_ERR(msg) do { error_msg = msg; goto error_parse; } while (0) ;

static int update_entry(pkt_rule_entry_t *entry) {
	if ( entry->cmp_mask != 0 ) {
		int upper_byte = 7 - (__k1_clz(entry->cmp_mask) - ( 32 - 8 ));
		union {
			uint64_t d;
			uint8_t b[8];
		} original_value, reordered_value;
		reordered_value.d = 0ULL;
		original_value.d = entry->cmp_value;
		for ( int src = upper_byte, dst = 0; src >= 0; --src, ++dst ) {
			reordered_value.b[dst] = original_value.b[src];
		}
		uint64_t bitmask = 0ULL;
		uint8_t cmp_mask = entry->cmp_mask;
		while ( cmp_mask ) {
			int byte_id = __k1_ctz(cmp_mask);
			cmp_mask &= ~( 1 << byte_id );
			bitmask |= 0xffULL << (byte_id * 8);
		}
		if ( reordered_value.d & ~bitmask ) {
			return 1;
		}
		entry->cmp_value = reordered_value.d;
	}
	return 0;
}

static const char* parse_hashpolicy(const char* pptr, int *nb_rules,
				    pkt_rule_t *rules, int lane) {
	const char *start_ptr = pptr;
	int rule_id = -1;
	int entry_id = 0;
	bool opened_rule = false;
	bool opened_entry = false;
	const char *error_msg;
	char *eptr;

	while ( true ) {
		switch ( *pptr ) {
			case PKT_RULE_OPEN_SIGN:
				if ( opened_rule == true )
					PARSE_HASH_ERR("open rule");
				rule_id++;
				if ( rule_id > 7 )
					PARSE_HASH_ERR("nb rules > 8");
				if ( rule_id > 1 && lane < 4)
					PARSE_HASH_ERR("nb rules > 2 on 1/10G port");
				entry_id = -1;
				opened_rule = true;
				pptr++;
				break;
			case PKT_RULE_PRIO_SIGN:
				if ( !( opened_rule == true &&
					opened_entry == false &&
					entry_id == -1 ) )
					PARSE_HASH_ERR("misplaced priority sign");
				pptr++;
				int priority = strtoul(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad priority value");
				if ( priority & ~0x7 )
					PARSE_HASH_ERR("priority must be in [0..7] range");
				rules[rule_id].priority = priority;
				pptr = eptr;
				break;
			case PKT_ENTRY_OPEN_SIGN:
				if ( opened_entry == true || opened_rule == false)
					PARSE_HASH_ERR("open entry");
				entry_id++;
				if ( entry_id > 8 )
					PARSE_HASH_ERR("nb entries > 9");
				opened_entry = true;
				pptr++;
				break;
			case PKT_RULE_CLOSE_SIGN:
				if ( opened_entry == true || opened_rule == false )
					PARSE_HASH_ERR("close rule");
				opened_rule = false;
				pptr++;
				break;
			case PKT_ENTRY_CLOSE_SIGN:
				if ( opened_entry == false || opened_rule == false)
					PARSE_HASH_ERR("close entry");
				opened_entry = false;
				rules[rule_id].nb_entries = entry_id + 1;
				if ( update_entry(&rules[rule_id].entries[entry_id]) )
					PARSE_HASH_ERR("compare value and mask does not fit");
				pptr++;
				break;
			case PKT_ENTRY_OFFSET_SIGN:
				if ( opened_entry == false )
					PARSE_HASH_ERR("offset entry");
				pptr++;
				int offset = strtoul(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad offset");
				rules[rule_id].entries[entry_id].offset = offset;
				pptr = eptr;
				break;
			case PKT_ENTRY_CMP_MASK_SIGN:
				if ( opened_entry == false )
					PARSE_HASH_ERR("cmp_mask entry");
				pptr++;
				int cmp_mask = strtoul(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad comparison mask");
				if ( cmp_mask & ~0xff )
					PARSE_HASH_ERR("cmp mask must be on 8 bits");
				rules[rule_id].entries[entry_id].cmp_mask = cmp_mask;
				pptr = eptr;
				break;
			case PKT_ENTRY_CMP_VALUE_SIGN:
				if ( opened_entry == false )
					PARSE_HASH_ERR("cmp_value entry");
				pptr++;
				uint64_t cmp_value = strtoull(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad comparison mask");
				rules[rule_id].entries[entry_id].cmp_value = cmp_value;
				pptr = eptr;
				break;
			case PKT_ENTRY_HASH_MASK_SIGN:
				if ( opened_entry == false )
					PARSE_HASH_ERR("hash_mask entry");
				pptr++;
				int hash_mask = strtoul(pptr, &eptr, 0);
				if(pptr == eptr)
					PARSE_HASH_ERR("bad hash mask");
				if ( hash_mask & ~0xff )
					PARSE_HASH_ERR("hash mask must be on 8 bits");
				rules[rule_id].entries[entry_id].hash_mask = hash_mask;
				pptr = eptr;
				break;
			case ':':
			case '\0':
				if ( opened_entry == true || opened_rule == true )
					PARSE_HASH_ERR("should not end");
				goto end;
			default:
				PARSE_HASH_ERR("unexpected character");
		}
	}

end:
	for ( int _rule_id = 0; _rule_id <= rule_id; ++_rule_id ) {
		for ( int _entry_id = 0; _entry_id < rules[_rule_id].nb_entries; ++_entry_id) {
			if ( rules[_rule_id].entries[_entry_id].cmp_mask == 0 &&
					rules[_rule_id].entries[_entry_id].cmp_value != 0 ) {
				ODP_ERR("rule %d entry %d: "
								"mask 0x%02x value %016llx\n"
								"compare value can't be set when compare mask is 0\n",
								_rule_id, _entry_id,
								rules[_rule_id].entries[_entry_id].cmp_mask,
								rules[_rule_id].entries[_entry_id].cmp_value );
				*nb_rules = 0;
				return NULL;
			}
			ODP_DBG("Rule[%d] (P%d) Entry[%d]: offset %d cmp_mask 0x%x cmp_value %"PRIu64" hash_mask 0x%x> ",
					_rule_id,
					rules[_rule_id].priority,
					entry_id,
					rules[_rule_id].entries[_entry_id].offset,
					rules[_rule_id].entries[_entry_id].cmp_mask,
					rules[_rule_id].entries[_entry_id].cmp_value,
					rules[_rule_id].entries[_entry_id].hash_mask);
		}
	}

	*nb_rules = rule_id + 1;
	return pptr;

error_parse: ;
	int error_index = pptr - start_ptr;
	while( *pptr != ':' && *pptr != '\0' ) {
		pptr++;
	}
	char *error_str = strndup(start_ptr, pptr - start_ptr + 1);
	assert( error_str != NULL );
	ODP_ERR("Error in parsing hashpolicy: %s\n", error_msg);
	ODP_ERR("%s\n", error_str);
	ODP_ERR("%*s%s\n", error_index, " ", "^");
	free(error_str);
	*nb_rules = 0;
	return NULL;
}

static int eth_open(odp_pktio_t id ODP_UNUSED, pktio_entry_t *pktio_entry,
		    const char *devname, odp_pool_t pool)
{
	int ret = 0;
	int nRx = N_RX_P_ETH;
	int rr_policy = -1;
	int rr_offset = 0;
	int port_id, slot_id;
	int loopback = 0;
	int nofree = 0;
	int jumbo = 0;
	int verbose = 0;
	int min_payload = 0;
	int max_payload = 0;

	pkt_rule_t *rules = NULL;
	int nb_rules = 0;
	/*
	 * Check device name and extract slot/port
	 */
	const char* pptr = devname;
	char * eptr;

	if (*(pptr++) != 'e')
		return -1;

	slot_id = strtoul(pptr, &eptr, 10);
	if (eptr == pptr || slot_id < 0 || slot_id >= MAX_ETH_SLOTS) {
		ODP_ERR("Invalid Ethernet name %s\n", devname);
		return -1;
	}

	pptr = eptr;
	if (*pptr == 'p') {
		/* Found a port */
		pptr++;
		port_id = strtoul(pptr, &eptr, 10);

		if (eptr == pptr || port_id < 0 || port_id >= MAX_ETH_PORTS) {
			ODP_ERR("Invalid Ethernet name %s\n", devname);
			return -1;
		}
		pptr = eptr;
	} else {
		/* Default port is 4 (40G), but physically lane 0 */
		port_id = 4;
	}

	while (*pptr == ':') {
		/* Parse arguments */
		pptr++;
		if (!strncmp(pptr, "tags=", strlen("tags="))){
			pptr += strlen("tags=");
			nRx = strtoul(pptr, &eptr, 10);
			if(pptr == eptr){
				ODP_ERR("Invalid tag count %s\n", pptr);
				return -1;
			}
			pptr = eptr;
		} else if (!strncmp(pptr, "rrpolicy=", strlen("rrpolicy="))){
			pptr += strlen("rrpolicy=");
			rr_policy = strtoul(pptr, &eptr, 10);
			if(pptr == eptr){
				ODP_ERR("Invalid rrpolicy %s\n", pptr);
				return -1;
			}
			pptr = eptr;
		} else if (!strncmp(pptr, "rroffset=", strlen("rroffset="))){
			pptr += strlen("rroffset=");
			rr_offset = strtoul(pptr, &eptr, 10);
			if(pptr == eptr){
				ODP_ERR("Invalid rroffset %s\n", pptr);
				return -1;
			}
			pptr = eptr;
		} else if (!strncmp(pptr, "hashpolicy=", strlen("hashpolicy="))){
			if ( rules ) {
				ODP_ERR("hashpolicy can only be set once\n");
				return -1;
			}
			rules = calloc(1, sizeof(pkt_rule_t) * 8);
			if ( rules == NULL ) {
				ODP_ERR("hashpolicy alloc failed\n");
				return -1;
			}
			pptr += strlen("hashpolicy=");
			pptr = parse_hashpolicy(pptr, &nb_rules,
						rules, port_id);
			if ( pptr == NULL ) {
				return -1;
			}
		} else if (!strncmp(pptr, "loop", strlen("loop"))){
			pptr += strlen("loop");
			loopback = 1;
		} else if (!strncmp(pptr, "jumbo", strlen("jumbo"))){
			pptr += strlen("jumbo");
			jumbo = 1;
		} else if (!strncmp(pptr, "verbose", strlen("verbose"))){
			pptr += strlen("verbose");
			verbose = 1;
		} else if (!strncmp(pptr, "nofree", strlen("nofree"))){
			pptr += strlen("nofree");
			nofree = 1;
		} else if (!strncmp(pptr, "min_payload=", strlen("min_payload="))){
			pptr += strlen("min_payload=");
			min_payload = strtoul(pptr, &eptr, 10);
			if(pptr == eptr){
				ODP_ERR("Invalid min_payload %s\n", pptr);
				return -1;
			}
			pptr = eptr;
		} else if (!strncmp(pptr, "max_payload=", strlen("max_payload="))){
			pptr += strlen("max_payload=");
			max_payload = strtoul(pptr, &eptr, 10);
			if(pptr == eptr){
				ODP_ERR("Invalid max_payload %s\n", pptr);
				return -1;
			}
			pptr = eptr;
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

	uintptr_t ucode;
	ucode = (uintptr_t)ucode_eth_v2;

	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	/*
	 * Init eth status
	 */
	eth->slot_id = slot_id;
	eth->port_id = port_id;
	eth->pool = pool;
	eth->loopback = loopback;
	eth->jumbo = jumbo;
	eth->verbose = verbose;
	eth->min_payload = min_payload;
	eth->max_payload = max_payload;
	eth->tx_config.nofree = nofree;
	eth->tx_config.add_end_marker = 0;

	if (pktio_entry->s.param.in_mode != ODP_PKTIN_MODE_DISABLED) {
		/* Setup Rx threads */
		eth->rx_config.dma_if = 0;
		eth->rx_config.pool = pool;
		eth->rx_config.if_type = RX_IF_TYPE_ETH;
		eth->rx_config.pktio_id = RX_ETH_IF_BASE + slot_id * MAX_ETH_PORTS + port_id;
		eth->rx_config.header_sz = sizeof(mppa_ethernet_header_t);
		eth->rx_config.flow_controlled = 0;
		ret = rx_thread_link_open(&eth->rx_config, nRx, rr_policy,
					  rr_offset, -1, -1);
		if(ret < 0)
			return -1;
	}

	ret = eth_rpc_send_eth_open(&pktio_entry->s.param, eth, nb_rules, rules);

	if ( rules ) {
		free(rules);
	}

	if (pktio_entry->s.param.out_mode != ODP_PKTOUT_MODE_DISABLED) {
		tx_uc_init(g_eth_tx_uc_ctx, NOC_ETH_UC_COUNT, ucode, 0);

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
	mppa_rpc_odp_ack_t ack;
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
	mppa_rpc_odp_ack_t ack;
	int ret;
	mppa_rpc_odp_cmd_eth_state_t state_cmd = {
		{
			.ifId = port_id,
			.enabled = enabled,
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

	ret = mppa_rpc_odp_wait_ack(&ack_msg, (void**)&payload, 5 * MPPA_RPC_ODP_TIMEOUT_1S, "[ETH]");
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




static int eth_recv(pktio_entry_t *pktio_entry, odp_packet_t pkt_table[],
		    unsigned len)
{
	int n_packet;
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;

	n_packet = odp_buffer_ring_get_multi(eth->rx_config.ring,
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
		packet_parse_l2(pkt_hdr);
	}

	if (n_packet && pktio_cls_enabled(pktio_entry)) {
		int defq_pkts = 0;
		for (int i = 0; i < n_packet; ++i) {
			if (0 > _odp_packet_classifier(pktio_entry, pkt_table[i])) {
				pkt_table[defq_pkts] = pkt_table[i];
			}
		}
		n_packet = defq_pkts;
	}

	return n_packet;
}

static int eth_send(pktio_entry_t *pktio_entry, odp_packet_t pkt_table[],
		    unsigned len)
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

static int eth_mtu_get(pktio_entry_t *const pktio_entry) {
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	return eth->mtu;
}

static int eth_stats(pktio_entry_t *const pktio_entry,
		     _odp_pktio_stats_t *stats)
{
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	unsigned cluster_id = __k1_get_cluster_id();
	mppa_rpc_odp_t *ack_msg;
	mppa_rpc_odp_ack_t ack;
	int ret;
	uint8_t *payload;
	mppa_rpc_odp_payload_eth_get_stat_t *rpc_stats;

	/*
	 * RPC Msg to IOETH  #N so the LB will dispatch to us
	 */
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
		return -1;

	ack.inl_data = ack_msg->inl_data;
	if (ack.status) {
		fprintf(stderr, "[ETH] Error: Server declined retrieval of eth stats\n");
		if (ack_msg->err_str && ack_msg->data_len > 0)
			fprintf(stderr, "[ETH] Error Log: %s\n", payload);
		return -1;
	}

	if (ack_msg->data_len != sizeof(mppa_rpc_odp_payload_eth_get_stat_t))
		return -1;

	rpc_stats = (mppa_rpc_odp_payload_eth_get_stat_t*)payload;

	stats->in_octets         = rpc_stats->in_octets;
	stats->in_ucast_pkts     = rpc_stats->in_ucast_pkts;
	stats->in_discards       = rpc_stats->in_discards;
	stats->in_dropped        = 0;
	stats->in_errors         = rpc_stats->in_errors;
	stats->in_unknown_protos = 0;
	stats->out_octets        = rpc_stats->out_octets;
	stats->out_ucast_pkts    = rpc_stats->out_ucast_pkts;
	stats->out_discards      = rpc_stats->out_discards;
	stats->out_errors        = rpc_stats->out_errors;

	if (rx_thread_fetch_stats(eth->rx_config.pktio_id,
				  &stats->in_dropped, &stats->in_discards))
		return -1;
	return 0;
}

const pktio_if_ops_t eth_pktio_ops = {
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
};
