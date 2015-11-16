/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp_packet_io_internal.h>
#include <odp/thread.h>
#include <odp/cpumask.h>
#include "HAL/hal/hal.h"
#include <odp/errno.h>
#include <errno.h>

#ifdef K1_NODEOS
#include <pthread.h>
#else
#include <utask.h>
#endif

#include "odp_pool_internal.h"
#include "odp_rpc_internal.h"
#include "odp_rx_internal.h"
#include "ucode_fw/ucode_eth.h"
#include "ucode_fw/ucode_eth_v2.h"

#define MAX_ETH_SLOTS 2
#define MAX_ETH_PORTS 4
_ODP_STATIC_ASSERT(MAX_ETH_PORTS * MAX_ETH_SLOTS <= MAX_RX_ETH_IF,
		   "MAX_RX_ETH_IF__ERROR");

#define N_RX_P_ETH 12

#define NOC_UC_COUNT		2
#define MAX_PKT_PER_UC		4
/* must be > greater than max_threads */
#define MAX_JOB_PER_UC          MOS_NB_UC_TRS
#define DNOC_CLUS_IFACE_ID	0

#include <mppa_noc.h>
#include <mppa_routing.h>

extern char _heap_end;
static int tx_init = 0;

typedef struct eth_uc_job_ctx {
	odp_packet_t pkt_table[MAX_PKT_PER_UC];
	unsigned int pkt_count;
	unsigned char nofree;
} eth_uc_job_ctx_t;

typedef struct eth_uc_ctx {
	unsigned int dnoc_tx_id;
	unsigned int dnoc_uc_id;

	odp_atomic_u64_t head;
#if MOS_UC_VERSION == 1
	odp_atomic_u64_t commit_head;
#endif
	eth_uc_job_ctx_t job_ctxs[MAX_JOB_PER_UC];
} eth_uc_ctx_t;

static eth_uc_ctx_t g_eth_uc_ctx[NOC_UC_COUNT] = {{0}};


static inline uint64_t _eth_alloc_uc_slots(eth_uc_ctx_t *ctx,
					   unsigned int count)
{
	ODP_ASSERT(count <= MAX_JOB_PER_UC);

	const uint64_t head =
		odp_atomic_fetch_add_u64(&ctx->head, count);
	const uint32_t last_id = head + count - 1;
	unsigned  ev_counter, diff;

	/* Wait for slot */
	ev_counter = mOS_uc_read_event(ctx->dnoc_uc_id);
	diff = last_id - ev_counter;
	while (diff > 0x80000000 || ev_counter + MAX_JOB_PER_UC <= last_id) {
		odp_spin();
		ev_counter = mOS_uc_read_event(ctx->dnoc_uc_id);
		diff = last_id - ev_counter;
	}

	/* Free previous packets */
	for (uint64_t pos = head; pos < head + count; pos++) {
		if(pos > MAX_JOB_PER_UC){
			eth_uc_job_ctx_t *job = &ctx->job_ctxs[pos % MAX_JOB_PER_UC];
			INVALIDATE(job);
			if (!job->pkt_count || job->nofree)
				continue;

			packet_free_multi(job->pkt_table,
					  job->pkt_count);
		}
	}
	return head;
}

static inline void _eth_uc_commit(eth_uc_ctx_t *ctx,
				  uint64_t slot,
				  unsigned int count)
{
#if MOS_UC_VERSION == 1
	while (odp_atomic_load_u64(&ctx->commit_head) != slot)
		odp_spin();
#endif

	__builtin_k1_wpurge();
	__builtin_k1_fence ();

#if MOS_UC_VERSION == 1
	for (unsigned i = 0; i < count; ++i)
		mOS_ucore_commit(ctx->dnoc_tx_id);
	odp_atomic_fetch_add_u64(&ctx->commit_head, count);
#else
	for (unsigned i = 0, pos = slot % MAX_JOB_PER_UC; i < count;
	     ++i, pos = (pos + 1) % MAX_JOB_PER_UC) {
		mOS_uc_transaction_t * const trs =
			&_scoreboard_start.SCB_UC.trs [ctx->dnoc_uc_id][pos];
		mOS_ucore_commit(ctx->dnoc_tx_id, trs);
	}
#endif
}

/**
 * #############################
 * PKTIO Interface
 * #############################
 */

static int eth_init_dnoc_tx(void)
{
	int i;
	mppa_noc_ret_t ret;
	mppa_noc_dnoc_uc_configuration_t uc_conf =
		MPPA_NOC_DNOC_UC_CONFIGURATION_INIT;

#if MOS_UC_VERSION == 1
	uc_conf.program_start = (uintptr_t)ucode_eth;
#else
	uc_conf.program_start = (uintptr_t)ucode_eth_v2;
#endif
	uc_conf.buffer_base = (uintptr_t)&_data_start;
	uc_conf.buffer_size = (uintptr_t)&_heap_end - (uintptr_t)&_data_start;

	for (i = 0; i < NOC_UC_COUNT; i++) {

		odp_atomic_init_u64(&g_eth_uc_ctx[i].head, 0);
#if MOS_UC_VERSION == 1
		odp_atomic_init_u64(&g_eth_uc_ctx[i].commit_head, 0);
#endif
		/* DNoC */
		ret = mppa_noc_dnoc_tx_alloc_auto(DNOC_CLUS_IFACE_ID,
						  &g_eth_uc_ctx[i].dnoc_tx_id,
						  MPPA_NOC_BLOCKING);
		if (ret != MPPA_NOC_RET_SUCCESS)
			return 1;

		ret = mppa_noc_dnoc_uc_alloc_auto(DNOC_CLUS_IFACE_ID,
						  &g_eth_uc_ctx[i].dnoc_uc_id,
						  MPPA_NOC_BLOCKING);
		if (ret != MPPA_NOC_RET_SUCCESS)
			return 1;

		/* We will only use events */
		mppa_noc_disable_interrupt_handler(DNOC_CLUS_IFACE_ID,
						   MPPA_NOC_INTERRUPT_LINE_DNOC_TX,
						   g_eth_uc_ctx[i].dnoc_uc_id);


		ret = mppa_noc_dnoc_uc_link(DNOC_CLUS_IFACE_ID,
					    g_eth_uc_ctx[i].dnoc_uc_id,
					    g_eth_uc_ctx[i].dnoc_tx_id, uc_conf);
		if (ret != MPPA_NOC_RET_SUCCESS)
			return 1;

#if MOS_UC_VERSION == 2
		for (int j = 0; j < MOS_NB_UC_TRS; j++) {
			mOS_uc_transaction_t  * trs =
				& _scoreboard_start.SCB_UC.trs[g_eth_uc_ctx[i].dnoc_uc_id][j];
			trs->notify._word = 0;
			trs->desc.tx_set = 1 << g_eth_uc_ctx[i].dnoc_tx_id;
			trs->desc.param_count = 8;
			trs->desc.pointer_count = 4;
		}
#endif
	}

	return 0;
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
	odp_rpc_t *ack_msg;
	odp_rpc_cmd_ack_t ack;
	int ret;

	/*
	 * RPC Msg to IOETH  #N so the LB will dispatch to us
	 */
	odp_rpc_cmd_eth_open_t open_cmd = {
		{
			.ifId = eth->port_id,
			.dma_if = eth->rx_config.dma_if,
			.min_rx = eth->rx_config.min_port,
			.max_rx = eth->rx_config.max_port,
			.loopback = eth->loopback,
			.rx_enabled = 1,
			.tx_enabled = 1,
		}
	};
	if (params) {
		if (params->in_mode == ODP_PKTIN_MODE_DISABLED)
			open_cmd.rx_enabled = 0;
		if (params->out_mode == ODP_PKTOUT_MODE_DISABLED)
			open_cmd.tx_enabled = 0;
	}
	odp_rpc_t cmd = {
		.data_len = 0,
		.pkt_type = ODP_RPC_CMD_ETH_OPEN,
		.inl_data = open_cmd.inl_data,
		.flags = 0,
	};

	odp_rpc_do_query(odp_rpc_get_ioeth_dma_id(eth->slot_id, cluster_id),
			 odp_rpc_get_ioeth_tag_id(eth->slot_id, cluster_id),
			 &cmd, NULL);

	ret = odp_rpc_wait_ack(&ack_msg, NULL, 15 * RPC_TIMEOUT_1S);
	if (ret < 0) {
		fprintf(stderr, "[ETH] RPC Error\n");
		return 1;
	} else if (ret == 0){
		fprintf(stderr, "[ETH] Query timed out\n");
		return 1;
	}

	ack.inl_data = ack_msg->inl_data;
	if (ack.status) {
		fprintf(stderr, "[ETH] Error: Server declined opening of eth interface\n");
		return 1;
	}

	eth->tx_if = ack.cmd.eth_open.tx_if;
	eth->tx_tag = ack.cmd.eth_open.tx_tag;
	memcpy(eth->mac_addr, ack.cmd.eth_open.mac, 6);
	eth->mtu = ack.cmd.eth_open.mtu;

	return 0;
}

static int eth_open(odp_pktio_t id ODP_UNUSED, pktio_entry_t *pktio_entry,
		    const char *devname, odp_pool_t pool)
{
	int ret = 0;
	int nRx = N_RX_P_ETH;
	int rr_policy = -1;
	int port_id, slot_id;
	int loopback = 0;
	int nofree = 0;
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
				ODP_ERR("Invalid rr_policy %s\n", pptr);
				return -1;
			}
			pptr = eptr;
		} else if (!strncmp(pptr, "loop", strlen("loop"))){
			pptr += strlen("loop");
			loopback = 1;
		} else if (!strncmp(pptr, "nofree", strlen("nofree"))){
			pptr += strlen("nofree");
			nofree = 1;
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

	if (!tx_init) {
		if(eth_init_dnoc_tx()) {
			ODP_ERR("Not enough DMA Tx for ETH send setup\n");
			return 1;
		}
		tx_init = 1;
	}

	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	/*
	 * Init eth status
	 */
	eth->slot_id = slot_id;
	eth->port_id = port_id;
	eth->pool = pool;
	eth->loopback = loopback;
	eth->nofree = nofree;
	odp_spinlock_init(&eth->wlock);

	if (pktio_entry->s.param.in_mode != ODP_PKTIN_MODE_DISABLED) {
		/* Setup Rx threads */
		eth->rx_config.dma_if = 0;
		eth->rx_config.pool = pool;
		eth->rx_config.pktio_id = slot_id * MAX_ETH_PORTS + port_id;
		eth->rx_config.header_sz = sizeof(mppa_ethernet_header_t);
		ret = rx_thread_link_open(&eth->rx_config, nRx, rr_policy);
		if(ret < 0)
			return -1;
	}

	ret = eth_rpc_send_eth_open(&pktio_entry->s.param, eth);

	if (pktio_entry->s.param.out_mode != ODP_PKTOUT_MODE_DISABLED) {
		mppa_routing_get_dnoc_unicast_route(__k1_get_cluster_id(),
						    eth->tx_if,
						    &eth->config, &eth->header);

		eth->config._.loopback_multicast = 0;
		eth->config._.cfg_pe_en = 1;
		eth->config._.cfg_user_en = 1;
		eth->config._.write_pe_en = 1;
		eth->config._.write_user_en = 1;
		eth->config._.decounter_id = 0;
		eth->config._.decounted = 0;
		eth->config._.payload_min = 6;
		eth->config._.payload_max = 32;
		eth->config._.bw_current_credit = 0xff;
		eth->config._.bw_max_credit     = 0xff;
		eth->config._.bw_fast_delay     = 0x00;
		eth->config._.bw_slow_delay     = 0x00;

		eth->header._.multicast = 0;
		eth->header._.tag = eth->tx_tag;
		eth->header._.valid = 1;
	}

	return ret;
}

static int eth_close(pktio_entry_t * const pktio_entry)
{

	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	int slot_id = eth->slot_id;
	int port_id = eth->port_id;
	odp_rpc_t *ack_msg;
	odp_rpc_cmd_ack_t ack;
	int ret;
	odp_rpc_cmd_eth_clos_t close_cmd = {
		{
			.ifId = eth->port_id = port_id

		}
	};
	unsigned cluster_id = __k1_get_cluster_id();
	odp_rpc_t cmd = {
		.pkt_type = ODP_RPC_CMD_ETH_CLOS,
		.data_len = 0,
		.flags = 0,
		.inl_data = close_cmd.inl_data
	};

	/* Free packets being sent by DMA */
	const unsigned int tx_index = eth->config._.first_dir % NOC_UC_COUNT;
	eth_uc_ctx_t * ctx = &g_eth_uc_ctx[tx_index];
	const uint64_t head = _eth_alloc_uc_slots(ctx, MAX_JOB_PER_UC);

	for (int slot_id = 0; slot_id < MAX_JOB_PER_UC; ++slot_id) {
		eth_uc_job_ctx_t * job = &ctx->job_ctxs[slot_id];
		mOS_uc_transaction_t * const trs =
			&_scoreboard_start.SCB_UC.trs[ctx->dnoc_uc_id][slot_id];
		for (unsigned i = 0; i < MAX_PKT_PER_UC; ++i ){
			trs->parameter.array[2 * i + 0] = 0;
			trs->parameter.array[2 * i + 1] = 0;
		}
		trs->notify._word = 0;
		trs->desc.tx_set = 0;
#if MOS_UC_VERSION == 1
		trs->desc.param_set = 0xff;
		trs->desc.pointer_set = 0;
#endif
		job->pkt_count = 0;
		job->nofree = 1;
	}
	_eth_uc_commit(ctx, head, MAX_JOB_PER_UC);

	odp_rpc_do_query(odp_rpc_get_ioeth_dma_id(slot_id, cluster_id),
			 odp_rpc_get_ioeth_tag_id(slot_id, cluster_id),
			 &cmd, NULL);

	ret = odp_rpc_wait_ack(&ack_msg, NULL, 5 * RPC_TIMEOUT_1S);
	if (ret < 0) {
		fprintf(stderr, "[ETH] RPC Error\n");
		return 1;
	} else if (ret == 0){
		fprintf(stderr, "[ETH] Query timed out\n");
		return 1;
	}
	ack.inl_data = ack_msg->inl_data;

	/* Push Context to handling threads */
	rx_thread_link_close(slot_id * MAX_ETH_PORTS + port_id);

	return ack.status;
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
					     len, NULL);

	for (int i = 0; i < n_packet; ++i) {
		odp_packet_t pkt = pkt_table[i];
		odp_packet_hdr_t *pkt_hdr = odp_packet_hdr(pkt);
		uint8_t * const base_addr =
			((uint8_t *)pkt_hdr->buf_hdr.addr) +
			pkt_hdr->headroom;

		packet_parse_reset(pkt);

		union mppa_ethernet_header_info_t info;
		uint8_t * const hdr_addr = base_addr -
			sizeof(mppa_ethernet_header_t);
		mppa_ethernet_header_t * const header =
			(mppa_ethernet_header_t *)hdr_addr;

		info.dword = LOAD_U64(header->info.dword);
		const unsigned frame_len =
			info._.pkt_size - sizeof(mppa_ethernet_header_t);
		pull_tail(pkt_hdr, pkt_hdr->frame_len - frame_len);
	}
	return n_packet;
}

static inline int
eth_send_packets(pkt_eth_t *eth, odp_packet_t pkt_table[], int pkt_count, int *err)
{
	const unsigned int tx_index = eth->config._.first_dir % NOC_UC_COUNT;
	eth_uc_ctx_t *ctx = &g_eth_uc_ctx[tx_index];

	const uint64_t head = _eth_alloc_uc_slots(ctx, 1);
	const unsigned slot_id = head % MAX_JOB_PER_UC;
	eth_uc_job_ctx_t * job = &ctx->job_ctxs[slot_id];
	mOS_uc_transaction_t * const trs =
		&_scoreboard_start.SCB_UC.trs [ctx->dnoc_uc_id][slot_id];
	const odp_packet_hdr_t * pkt_hdr;

	*err = 0;
	job->nofree = eth->nofree;
	for (int i = 0; i < pkt_count; ++i ){
		job->pkt_table[i] = pkt_table[i];
		pkt_hdr = odp_packet_hdr(pkt_table[i]);

		if (pkt_hdr->frame_len > eth->mtu) {
			pkt_count = i;
			*err = EINVAL;
			break;
		}

		trs->parameter.array[2 * i + 0] =
			pkt_hdr->frame_len / sizeof(uint64_t);
		trs->parameter.array[2 * i + 1] =
			pkt_hdr->frame_len % sizeof(uint64_t);

		trs->pointer.array[i] = (unsigned long)
			(((uint8_t*)pkt_hdr->buf_hdr.addr + pkt_hdr->headroom)
			 - (uint8_t*)&_data_start);
	}
	for (int i = pkt_count; i < 4; ++i) {
		trs->parameter.array[2 * i + 0] = 0;
		trs->parameter.array[2 * i + 1] = 0;
	}

	trs->path.array[ctx->dnoc_tx_id].header = eth->header;
	trs->path.array[ctx->dnoc_tx_id].config = eth->config;
#if MOS_UC_VERSION == 1
	trs->notify._word = 0;
	trs->desc.tx_set = 1 << ctx->dnoc_tx_id;
	trs->desc.param_set = 0xff;
	trs->desc.pointer_set = (0x1 <<  pkt_count) - 1;
#endif

	job->pkt_count = pkt_count;

	_eth_uc_commit(ctx, head, 1);

	return pkt_count;
}

static int eth_send(pktio_entry_t *pktio_entry, odp_packet_t pkt_table[],
		    unsigned len)
{
	int sent = 0;
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	int pkt_count;


	while(sent < (int)len) {
		int ret, uc_sent;

		pkt_count = (len - sent) > MAX_PKT_PER_UC ? MAX_PKT_PER_UC :
			(len - sent);

		uc_sent = eth_send_packets(eth, &pkt_table[sent], pkt_count, &ret);
		sent += uc_sent;
		if (ret) {
			if (!sent) {
				__odp_errno = ret;
				return -1;
			}
			return sent;
		}
	}

	return sent;
}

static int eth_promisc_mode_set(pktio_entry_t *const pktio_entry,
				odp_bool_t enable ODP_UNUSED){
	/* FIXME */
	pktio_entry->s.pkt_eth.promisc = enable;
	return 0;
}

static int eth_promisc_mode(pktio_entry_t *const pktio_entry){
	return 	pktio_entry->s.pkt_eth.promisc;
}

static int eth_mtu_get(pktio_entry_t *const pktio_entry ODP_UNUSED) {
	pkt_eth_t *eth = &pktio_entry->s.pkt_eth;
	return eth->mtu;
}
const pktio_if_ops_t eth_pktio_ops = {
	.init = eth_init,
	.term = eth_destroy,
	.open = eth_open,
	.close = eth_close,
	.start = NULL,
	.stop = NULL,
	.recv = eth_recv,
	.send = eth_send,
	.mtu_get = eth_mtu_get,
	.promisc_mode_set = eth_promisc_mode_set,
	.promisc_mode_get = eth_promisc_mode,
	.mac_get = eth_mac_addr_get,
};
