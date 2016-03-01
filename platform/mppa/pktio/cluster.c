/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp_packet_io_internal.h>
#include <HAL/hal/hal.h>
#include <odp/errno.h>
#include <errno.h>

#include <mppa_bsp.h>
#include <mppa_routing.h>
#include <mppa_noc.h>

#include <odp_classification_internal.h>
#include "odp_rpc_internal.h"
#include "odp_tx_uc_internal.h"
#include "odp_rx_internal.h"

#include "ucode_fw/ucode_eth.h"
#include "ucode_fw/ucode_eth_v2.h"

#include <unistd.h>

#define NOC_CLUS_IFACE_ID       0
#define NOC_C2C_UC_COUNT	2

#define ODP_CLUS_DBG(fmt, ...)	ODP_DBG("[Clus %d] " fmt, __k1_get_cluster_id(), ##__VA_ARGS__)

static tx_uc_ctx_t g_c2c_tx_uc_ctx[NOC_C2C_UC_COUNT] = {{0}};
static int g_cnoc_tx_id = -1;
static odp_spinlock_t g_cnoc_tx_lock;

static inline tx_uc_ctx_t *c2c_get_ctx(const pkt_cluster_t *clus)
{
	const unsigned int tx_index =
		clus->tx_config.config._.first_dir % NOC_C2C_UC_COUNT;
	return &g_c2c_tx_uc_ctx[tx_index];
}
static int cluster_init_cnoc_rx(void)
{
	mppa_cnoc_mailbox_notif_t notif = {0};
	mppa_noc_ret_t ret;
	mppa_noc_cnoc_rx_configuration_t conf = {0};
	unsigned rx_id;

	conf.mode = MPPA_NOC_CNOC_RX_MAILBOX;
	conf.init_value = 0;

	/* CNoC */
	ret = mppa_noc_cnoc_rx_alloc_auto(NOC_CLUS_IFACE_ID, &rx_id,
					  MPPA_NOC_BLOCKING);
	if (ret != MPPA_NOC_RET_SUCCESS)
		return 1;

	ret = mppa_noc_cnoc_rx_configure(NOC_CLUS_IFACE_ID, rx_id,
					 conf, &notif);
	if (ret != MPPA_NOC_RET_SUCCESS)
		return -1;

	return rx_id;
}

static int cluster_init_cnoc_tx(void)
{
	mppa_noc_ret_t ret;

	if (g_cnoc_tx_id >= 0)
		return 0;

	/* CnoC */
	odp_spinlock_init(&g_cnoc_tx_lock);
	ret = mppa_noc_cnoc_tx_alloc_auto(NOC_CLUS_IFACE_ID,
					  (unsigned *)&g_cnoc_tx_id, MPPA_NOC_BLOCKING);
	if (ret != MPPA_NOC_RET_SUCCESS)
		return 1;

	return 0;
}

static int cluster_configure_cnoc_tx(pkt_cluster_t *cluster)
{
	mppa_noc_ret_t nret;

	cluster->header._.tag = cluster->remote.cnoc_rx;
	nret = mppa_noc_cnoc_tx_configure(NOC_CLUS_IFACE_ID,
					  g_cnoc_tx_id,
					  cluster->config, cluster->header);

	return (nret != MPPA_NOC_RET_SUCCESS);
}

static int cluster_init(void)
{
	if (rx_thread_init())
		return 1;

	return 0;
}

static int cluster_rpc_send_c2c_open(odp_pktio_param_t * params, pkt_cluster_t *cluster)
{
	unsigned cluster_id = __k1_get_cluster_id();
	odp_rpc_t *ack_msg;
	odp_rpc_cmd_ack_t ack;
	int ret;

	/*
	 * RPC Msg to IODDR0 so the LB will dispatch to us
	 */
	odp_rpc_cmd_c2c_open_t open_cmd = {
		{
			.cluster_id = cluster->clus_id,
			.min_rx = cluster->local.min_rx,
			.max_rx = cluster->local.max_rx,
			.rx_enabled = 1,
			.tx_enabled = 1,
			.mtu = cluster->mtu,
			.cnoc_rx = cluster->local.cnoc_rx,
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
		.pkt_type = ODP_RPC_CMD_C2C_OPEN,
		.inl_data = open_cmd.inl_data,
		.flags = 0,
	};
	const unsigned int rpc_server_id = odp_rpc_client_get_default_server();

	odp_rpc_do_query(odp_rpc_get_ioddr_dma_id(rpc_server_id, cluster_id),
			 odp_rpc_get_ioddr_tag_id(rpc_server_id, cluster_id),
			 &cmd, NULL);

	ret = odp_rpc_wait_ack(&ack_msg, NULL, 15 * RPC_TIMEOUT_1S);
	if (ret < 0) {
		fprintf(stderr, "[C2C] RPC Error\n");
		return 1;
	} else if (ret == 0){
		fprintf(stderr, "[C2C] Query timed out\n");
		return 1;
	}

	ack.inl_data = ack_msg->inl_data;
	if (ack.status) {
		fprintf(stderr, "[C2C] Error: Server declined opening of cluster interface\n");
		return 1;
	}

	return 0;
}

static int cluster_rpc_send_c2c_query(pkt_cluster_t *cluster)
{
	unsigned cluster_id = __k1_get_cluster_id();
	odp_rpc_t *ack_msg;
	odp_rpc_cmd_ack_t ack;
	int ret;

	odp_rpc_cmd_c2c_query_t query_cmd = {
		{
			.cluster_id = cluster->clus_id,
		}
	};
	odp_rpc_t cmd = {
		.data_len = 0,
		.pkt_type = ODP_RPC_CMD_C2C_QUERY,
		.inl_data = query_cmd.inl_data,
		.flags = 0,
	};
	const unsigned int rpc_server_id = odp_rpc_client_get_default_server();

	odp_rpc_do_query(odp_rpc_get_ioddr_dma_id(rpc_server_id, cluster_id),
			 odp_rpc_get_ioddr_tag_id(rpc_server_id, cluster_id),
			 &cmd, NULL);

	ret = odp_rpc_wait_ack(&ack_msg, NULL, 15 * RPC_TIMEOUT_1S);
	if (ret < 0) {
		fprintf(stderr, "[C2C] RPC Error\n");
		__odp_errno = EPIPE;
		return 1;
	} else if (ret == 0){
		fprintf(stderr, "[C2C] Query timed out\n");
		__odp_errno = ETIMEDOUT;
		return 1;
	}

	ack.inl_data = ack_msg->inl_data;
	if (ack.status) {
		if (ack.cmd.c2c_query.closed) {
			__odp_errno = EAGAIN;
		} else if (ack.cmd.c2c_query.eacces) {
			__odp_errno = EACCES;
		}
		return 1;
	}

	cluster->remote.min_rx = ack.cmd.c2c_query.min_rx;
	cluster->remote.max_rx = ack.cmd.c2c_query.max_rx;
	cluster->remote.n_credits = cluster->remote.max_rx -
		cluster->remote.min_rx + 1;
	cluster->mtu = ack.cmd.c2c_query.mtu;
	cluster->remote.cnoc_rx = ack.cmd.c2c_query.cnoc_rx;
	cluster->tx_config.header._.tag = cluster->remote.min_rx;

	return 0;
}

static int cluster_open(odp_pktio_t id ODP_UNUSED, pktio_entry_t *pktio_entry,
			const char *devname, odp_pool_t pool)
{
	int ret;
	const char *pptr = devname;
	char *eptr;
	int cluster_id;
	int nRx = 3;
	int rr_policy = -1;
	int nofree = 0;

	/* String should in the following format: "cluster<cluster_id>" */
	if(strncmp("cluster", devname, strlen("cluster")))
		return -1;

	pptr +=	strlen("cluster");
	cluster_id = strtoul(pptr, &eptr, 10);
	if (eptr == pptr || cluster_id < 0 || cluster_id > 15){
		ODP_ERR("Invalid cluster name %s\n", devname);
		return -1;
	}
	pptr = eptr;
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

	pkt_cluster_t * pkt_cluster = &pktio_entry->s.pkt_cluster;
	uintptr_t ucode;
	mppa_routing_ret_t rret;

	memset(pkt_cluster, 0, sizeof(*pkt_cluster));
	ucode = (uintptr_t)ucode_eth_v2;

	pkt_cluster->clus_id = cluster_id;
	pkt_cluster->pool = pool;
	pkt_cluster->tx_config.nofree = nofree;
	pkt_cluster->tx_config.add_end_marker = 0;
	pkt_cluster->remote.cnoc_rx = pkt_cluster->local.cnoc_rx = -1;
	pkt_cluster->remote.min_rx = pkt_cluster->remote.max_rx = -1;
	pkt_cluster->mtu = odp_buffer_pool_segment_size(pool) -
		odp_buffer_pool_headroom(pool);
	odp_spinlock_init(&pkt_cluster->wlock);

	if (pktio_entry->s.param.in_mode != ODP_PKTIN_MODE_DISABLED) {
		/* Setup Rx threads */
		pkt_cluster->rx_config.dma_if = 0;
		pkt_cluster->rx_config.pool = pool;
		pkt_cluster->rx_config.pktio_id = MAX_RX_ETH_IF +
			MAX_RX_PCIE_IF + cluster_id;
		pkt_cluster->rx_config.header_sz = sizeof(tx_uc_header_t);

		if (cluster_init_cnoc_tx()) {
			ODP_ERR("Failed to initialize CNoC Rx\n");
			return -1;
		}

		rret = mppa_routing_get_cnoc_unicast_route(__k1_get_cluster_id(),
							   cluster_id,
							   &pkt_cluster->config,
							   &pkt_cluster->header);
		if (rret != MPPA_ROUTING_RET_SUCCESS)
			return 1;

		ret = rx_thread_link_open(&pkt_cluster->rx_config, nRx, rr_policy);
		if(ret < 0) {
			ODP_ERR("Failed to setup rx threads\n");
			return -1;
		}

		pkt_cluster->local.min_rx = pkt_cluster->rx_config.min_port;
		pkt_cluster->local.max_rx = pkt_cluster->rx_config.max_port;
		pkt_cluster->local.n_credits = pkt_cluster->local.max_rx -
			pkt_cluster->local.min_rx + 1;
	}

	if (pktio_entry->s.param.out_mode != ODP_PKTOUT_MODE_DISABLED) {
		tx_uc_init(g_c2c_tx_uc_ctx, NOC_C2C_UC_COUNT, ucode, 1);

		pkt_cluster->local.cnoc_rx = cluster_init_cnoc_rx();
		if (pkt_cluster->local.cnoc_rx < 0) {
			ODP_ERR("Failed to initialize CNoC Rx\n");
			return -1;
		}

		/* Get and configure route */
		mppa_routing_get_dnoc_unicast_route(__k1_get_cluster_id(),
						    pkt_cluster->clus_id,
						    &pkt_cluster->tx_config.config,
						    &pkt_cluster->tx_config.header);

		pkt_cluster->tx_config.config._.loopback_multicast = 0;
		pkt_cluster->tx_config.config._.cfg_pe_en = 1;
		pkt_cluster->tx_config.config._.cfg_user_en = 1;
		pkt_cluster->tx_config.config._.write_pe_en = 1;
		pkt_cluster->tx_config.config._.write_user_en = 1;
		pkt_cluster->tx_config.config._.decounter_id = 0;
		pkt_cluster->tx_config.config._.decounted = 0;
		pkt_cluster->tx_config.config._.payload_min = 0;
		pkt_cluster->tx_config.config._.payload_max = 32;
		pkt_cluster->tx_config.config._.bw_current_credit = 0xff;
		pkt_cluster->tx_config.config._.bw_max_credit     = 0xff;
		pkt_cluster->tx_config.config._.bw_fast_delay     = 0x00;
		pkt_cluster->tx_config.config._.bw_slow_delay     = 0x00;

		pkt_cluster->tx_config.header._.multicast = 0;
		pkt_cluster->tx_config.header._.valid = 1;

	}

	ret = cluster_rpc_send_c2c_open(&pktio_entry->s.param, pkt_cluster);

	return 0;
}

static int cluster_close(pktio_entry_t * const pktio_entry ODP_UNUSED)
{
	pkt_cluster_t *clus = &pktio_entry->s.pkt_cluster;
	odp_rpc_t *ack_msg;
	int ret;
	odp_rpc_cmd_c2c_clos_t close_cmd = {
		{
			.cluster_id = clus->clus_id

		}
	};
	unsigned cluster_id = __k1_get_cluster_id();
	odp_rpc_t cmd = {
		.pkt_type = ODP_RPC_CMD_C2C_CLOS,
		.data_len = 0,
		.flags = 0,
		.inl_data = close_cmd.inl_data
	};
	const unsigned int rpc_server_id = odp_rpc_client_get_default_server();

	/* Free packets being sent by DMA */
	tx_uc_flush(c2c_get_ctx(clus));

	if (clus->remote.cnoc_rx >= 0) {
		odp_spinlock_lock(&g_cnoc_tx_lock);
		if (cluster_configure_cnoc_tx(clus)){
			/* Faile to configure cnoc tx */
			odp_spinlock_unlock(&g_cnoc_tx_lock);
			return 1;
		}
		/* Clear all credits on remote side */
		mppa_noc_cnoc_tx_push(NOC_CLUS_IFACE_ID, g_cnoc_tx_id, -1);
		odp_spinlock_unlock(&g_cnoc_tx_lock);
	}

	odp_rpc_do_query(odp_rpc_get_ioddr_dma_id(rpc_server_id, cluster_id),
			 odp_rpc_get_ioddr_tag_id(rpc_server_id, cluster_id),
			 &cmd, NULL);

	ret = odp_rpc_wait_ack(&ack_msg, NULL, 5 * RPC_TIMEOUT_1S);
	if (ret < 0) {
		fprintf(stderr, "[CLUS] RPC Error\n");
		return 1;
	} else if (ret == 0){
		fprintf(stderr, "[CLUS] Query timed out\n");
		return 1;
	}

	/* Push Context to handling threads */
	rx_thread_link_close(clus->rx_config.pktio_id);

	return 0;
}


static int cluster_mac_addr_get(pktio_entry_t *pktio_entry,
				void *mac_addr)
{
	const pkt_cluster_t *pktio_clus = &pktio_entry->s.pkt_cluster;
	uint8_t *mac_addr_u = mac_addr;

	memset(mac_addr_u, 0, ETH_ALEN);

	mac_addr_u[0] = pktio_clus->clus_id;
	return ETH_ALEN;
}

static int cluster_send_recv_pkt_count(pkt_cluster_t *pktio_clus)
{
	if (cluster_configure_cnoc_tx(pktio_clus) != 0)
		return 1;

	mppa_noc_cnoc_tx_push(NOC_CLUS_IFACE_ID, g_cnoc_tx_id,
			      pktio_clus->remote.pkt_count);

	return 0;
}


static int cluster_recv(pktio_entry_t *const pktio_entry,
			odp_packet_t pkt_table[],
			unsigned len)
{
	int n_packet;
	pkt_cluster_t *clus = &pktio_entry->s.pkt_cluster;

	if (clus->remote.cnoc_rx < 0) {
		/* We need to sync with the target first */
		if (cluster_rpc_send_c2c_query(clus)){
			return 0;
		}
	}

	n_packet = odp_buffer_ring_get_multi(clus->rx_config.ring,
					     (odp_buffer_hdr_t **)pkt_table,
					     len, NULL);

	if (!n_packet)
		return 0;

	odp_spinlock_lock(&g_cnoc_tx_lock);
	clus->remote.pkt_count += n_packet;
	if (cluster_send_recv_pkt_count(clus) != 0) {
		odp_spinlock_unlock(&g_cnoc_tx_lock);
		return 1;
	}
	odp_spinlock_unlock(&g_cnoc_tx_lock);

	for (int i = 0; i < n_packet; ++i) {
		odp_packet_t pkt = pkt_table[i];
		odp_packet_hdr_t *pkt_hdr = odp_packet_hdr(pkt);
		uint8_t * const base_addr =
			((uint8_t *)pkt_hdr->buf_hdr.addr) +
			pkt_hdr->headroom;

		INVALIDATE(pkt_hdr);
		packet_parse_reset(pkt_hdr);

		tx_uc_header_t info;
		uint8_t * const hdr_addr = base_addr -
			sizeof(tx_uc_header_t);
		tx_uc_header_t * const header =
			(tx_uc_header_t *)hdr_addr;

		info.dword = LOAD_U64(header->dword);
		const unsigned frame_len = info.pkt_size;
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

static int cluster_send(pktio_entry_t *const pktio_entry,
			odp_packet_t pkt_table[], unsigned len)
{
	pkt_cluster_t *pkt_cluster = &pktio_entry->s.pkt_cluster;

	odp_spinlock_lock(&pkt_cluster->wlock);
	if (pkt_cluster->remote.min_rx < 0) {
		/* We need to sync with the target first */
		if (cluster_rpc_send_c2c_query(pkt_cluster)){
			odp_spinlock_unlock(&pkt_cluster->wlock);
			return 0;
		}
	}

	tx_uc_ctx_t *ctx = c2c_get_ctx(pkt_cluster);

	/* Get credits first */
	int remote_pkt_count =
		mppa_noc_cnoc_rx_get_value(NOC_CLUS_IFACE_ID,
					   pkt_cluster->local.cnoc_rx);

	if (remote_pkt_count == -1) {
		/* Remote was closed. Try to reinit the pktio */
		odp_spinlock_unlock(&pkt_cluster->wlock);
		pkt_cluster->remote.min_rx = pkt_cluster->remote.max_rx = -1;
		pkt_cluster->remote.cnoc_rx = -1;
		return 0;
	}

	/* Is there enough room to send a packet ? */
	unsigned credit = pkt_cluster->remote.n_credits -
		(pkt_cluster->local.pkt_count - remote_pkt_count);

	if (credit < len)
		len = credit;

	int sent = 0;
	while (len > 0) {

		int ret  =  tx_uc_send_packets(&pkt_cluster->tx_config, ctx,
					       &pkt_table[sent], 1,
					       pkt_cluster->mtu);
		if (ret < 0){
			if (sent) {
				__odp_errno = 0;
				break;
			}
			odp_spinlock_unlock(&pkt_cluster->wlock);
			return ret;
		}

		len--;

		pkt_cluster->tx_config.header._.tag += 1;
		if (pkt_cluster->tx_config.header._.tag > pkt_cluster->remote.max_rx)
			pkt_cluster->tx_config.header._.tag = pkt_cluster->remote.min_rx;

		sent += ret;
	}
	pkt_cluster->local.pkt_count += sent;
	odp_spinlock_unlock(&pkt_cluster->wlock);
	return sent;
}

static int cluster_promisc_mode_set(pktio_entry_t *const pktio_entry,
				    odp_bool_t enable)
{
	/* FIXME */
	pktio_entry->s.pkt_cluster.promisc = enable;
	return 0;
}

static int cluster_promisc_mode(pktio_entry_t *const pktio_entry)
{
	return 	pktio_entry->s.pkt_cluster.promisc;
}

static int cluster_mtu_get(pktio_entry_t *const pktio_entry)
{
	pkt_cluster_t *pkt_cluster = &pktio_entry->s.pkt_cluster;
	return pkt_cluster->mtu;
}

const pktio_if_ops_t cluster_pktio_ops = {
	.init = cluster_init,
	.term = NULL,
	.open = cluster_open,
	.close = cluster_close,
	.start = NULL,
	.stop = NULL,
	.recv = cluster_recv,
	.send = cluster_send,
	.mtu_get = cluster_mtu_get,
	.promisc_mode_set = cluster_promisc_mode_set,
	.promisc_mode_get = cluster_promisc_mode,
	.mac_get = cluster_mac_addr_get,
};
