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
#include <odp/api/cpu.h>

#ifdef K1_NODEOS
#include <pthread.h>
#else
#include <utask.h>
#endif

#include <odp_classification_internal.h>
#include "odp_pool_internal.h"
#include "odp_rx_internal.h"
#include "odp_tx_uc_internal.h"

#define MAX_IODDR_SLOTS 2

#define N_RX_P_IODDR 10
#define NOC_CLUS_IFACE_ID       0
#define NOC_IODDR_UC_COUNT 0

#include <mppa_noc.h>
#include <mppa_routing.h>

static int g_cnoc_tx_id = -1;
static odp_spinlock_t g_cnoc_tx_lock;
/**
 * #############################
 * PKTIO Interface
 * #############################
 */
static uint8_t ioddr_mac[ETH_ALEN] =  { 0xde, 0xad, 0xbe, 0xbe, 0x00 };

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

static int cluster_configure_cnoc_tx(pkt_ioddr_t *ioddr)
{
	mppa_noc_ret_t nret;

	nret = mppa_noc_cnoc_tx_configure(NOC_CLUS_IFACE_ID,
					  g_cnoc_tx_id,
					  ioddr->config, ioddr->header);

	return (nret != MPPA_NOC_RET_SUCCESS);
}

static int ioddr_init(void)
{
	if (rx_thread_init())
		return 1;

	return 0;
}

static int ioddr_destroy(void)
{
	/* Last pktio to close should work. Expect an err code for others */
	rx_thread_destroy();
	return 0;
}


static int ioddr_open(odp_pktio_t id ODP_UNUSED, pktio_entry_t *pktio_entry,
		    const char *devname, odp_pool_t pool)
{
	int ret = 0;
	rx_opts_t rx_opts;
	int slot_id;
	int log2_fragments = 0;
	int cnoc_port = -1;

	/*
	 * Check device name and extract slot/port
	 */
	const char* pptr = devname;
	char * eptr;

	rx_options_default(&rx_opts);
	rx_opts.nRx = N_RX_P_IODDR;

	if (strncmp(pptr, "ioddr", strlen("ioddr")))
		return -1;
	pptr += strlen("ioddr");
	slot_id = strtoul(pptr, &eptr, 10);
	if (eptr == pptr || slot_id < 0 || slot_id >= MAX_IODDR_SLOTS) {
		ODP_ERR("Invalid ioddr name %s\n", devname);
		return -1;
	}
	pptr = eptr;

	while (*pptr == ':') {
		/* Parse arguments */
		pptr++;

		ret = rx_parse_options(&pptr, &rx_opts);
		if (ret < 0)
			return -1;
		if (ret > 0)
			continue;

		if (!strncmp(pptr, "log2fragments=", strlen("log2fragments="))){
			pptr += strlen("log2fragments=");
			log2_fragments = strtoul(pptr, &eptr, 10);
			if(pptr == eptr){
				ODP_ERR("Invalid log2fragments %s\n", pptr);
				return -1;
			}
			pptr = eptr;
		}  else if (!strncmp(pptr, "cnoc=", strlen("cnoc="))){
			pptr += strlen("cnoc=");
			cnoc_port = strtoul(pptr, &eptr, 10);
			if(pptr == eptr){
				ODP_ERR("Invalid cnoc %s\n", pptr);
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

	pkt_ioddr_t *ioddr = &pktio_entry->s.pkt_ioddr;
	/*
	 * Init ioddr status
	 */
	ioddr->slot_id = slot_id;
	ioddr->pool = pool;
	ioddr->log2_fragments = log2_fragments;
	ioddr->tx_config.nofree = 0;
	if (rx_opts.min_rx == -1 || rx_opts.max_rx == -1) {
		ODP_ERR("min_rx and max_rx options must be specified\n");
		return -1;
	}
	if (cnoc_port == -1) {
		ODP_ERR("cnoc option must be specified\n");
		return -1;
	}

	if (pktio_entry->s.param.in_mode == ODP_PKTIN_MODE_DISABLED)
		return 0;

	/* Setup Rx threads */
	ioddr->rx_config.dma_if = 0;
	ioddr->rx_config.pool = pool;
	ioddr->rx_config.pktio_id = RX_IODDR_IF_BASE + slot_id;
	ioddr->rx_config.header_sz = sizeof(mppa_ethernet_header_t);
	ioddr->rx_config.if_type = RX_IF_TYPE_IODDR;
	ret = rx_thread_link_open(&ioddr->rx_config, &rx_opts);
	ioddr->rx_config.pktio = &pktio_entry->s;
	if(ret < 0)
		return -1;

	if (cluster_init_cnoc_tx()) {
		ODP_ERR("Failed to initialize CNoC Rx\n");
		return -1;
	}

	ret = mppa_routing_get_cnoc_unicast_route(__k1_get_cluster_id(),
						   128 + 64 * slot_id,
						   &ioddr->config,
						   &ioddr->header);
	ioddr->header._.tag = cnoc_port + __k1_get_cluster_id();
	return 0;
}

static int ioddr_close(pktio_entry_t * const pktio_entry)
{

	pkt_ioddr_t *ioddr = &pktio_entry->s.pkt_ioddr;

	/* Push Context to handling threads */
	rx_thread_link_close(ioddr->rx_config.pktio_id);

	return 0;
}


static int ioddr_start(pktio_entry_t * const pktio_entry ODP_UNUSED)
{
	return 0;
}

static int ioddr_stop(pktio_entry_t * const pktio_entry ODP_UNUSED)
{
	return 0;
}

static int ioddr_mac_addr_get(pktio_entry_t *pktio_entry,
			    void *mac_addr)
{
	pkt_ioddr_t *ioddr = &pktio_entry->s.pkt_ioddr;

	memcpy(mac_addr,ioddr_mac, ETH_ALEN);
	((uint8_t*)mac_addr)[ETH_ALEN - 1 ] = ioddr->slot_id;
	return ETH_ALEN;
}


static void _ioddr_compute_pkt_size(odp_packet_t pkt)
{
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

static int ioddr_recv(pktio_entry_t *pktio_entry, odp_packet_t pkt_table[],
		    unsigned len)
{
	int total_packet = 0, n_packet;
	pkt_ioddr_t *ioddr = &pktio_entry->s.pkt_ioddr;
	const int log2_frag_per_pkt = ioddr->log2_fragments;
	const int frag_per_pkt = 1 << log2_frag_per_pkt;
	const unsigned wanted_segs = len << log2_frag_per_pkt;
	odp_packet_t tmp_table[wanted_segs];
	uint64_t pkt_count;

	odp_buffer_ring_t *ring = rx_get_ring(&ioddr->rx_config);

	total_packet =
		odp_buffer_ring_get_multi(ring,
					  (odp_buffer_hdr_t **)(&tmp_table[total_packet]),
					  wanted_segs, log2_frag_per_pkt, NULL);

	if (!total_packet)
		return 0;


	odp_spinlock_lock(&g_cnoc_tx_lock);
	pkt_count = LOAD_U64(ioddr->pkt_count) + total_packet;
	STORE_U64(ioddr->pkt_count, pkt_count);
	cluster_configure_cnoc_tx(ioddr);
	mppa_noc_cnoc_tx_push(NOC_CLUS_IFACE_ID, g_cnoc_tx_id, pkt_count);
	odp_spinlock_unlock(&g_cnoc_tx_lock);

	for (n_packet = 0; n_packet < total_packet >> log2_frag_per_pkt; ++n_packet) {
		const int pkt_base = n_packet << log2_frag_per_pkt;
		odp_packet_t top_pkt = tmp_table[pkt_base];
		odp_packet_hdr_t *top_pkt_hdr = odp_packet_hdr(top_pkt);

		pkt_table[n_packet] = top_pkt;
		_ioddr_compute_pkt_size(top_pkt);

		for (int j = 1; j < frag_per_pkt; ++j) {
			odp_packet_t pkt = tmp_table[pkt_base + j];

			top_pkt_hdr->sub_packets[j - 1] = pkt;
			_ioddr_compute_pkt_size(pkt);
		}
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

static int ioddr_send(pktio_entry_t *pktio_entry ODP_UNUSED,
		      odp_packet_t pkt_table[] ODP_UNUSED,
		      unsigned len ODP_UNUSED)
{
	return 0;
}

static int ioddr_promisc_mode_set(pktio_entry_t *const pktio_entry,
				odp_bool_t enable)
{
	pktio_entry->s.pkt_ioddr.promisc = enable;
	return 0;
}

static int ioddr_promisc_mode(pktio_entry_t *const pktio_entry){
	return 	pktio_entry->s.pkt_ioddr.promisc;
}

static int ioddr_mtu_get(pktio_entry_t *const pktio_entry) {
	pkt_ioddr_t *ioddr = &pktio_entry->s.pkt_ioddr;
	return ioddr->mtu;
}

static int ioddr_stats(pktio_entry_t *const pktio_entry,
		     _odp_pktio_stats_t *stats)
{
	pkt_ioddr_t *ioddr = &pktio_entry->s.pkt_ioddr;

	memset(stats, 0, sizeof(*stats));
	if (rx_thread_fetch_stats(ioddr->rx_config.pktio_id,
				  &stats->in_dropped, &stats->in_discards))
		return -1;
	return 0;
}

const pktio_if_ops_t ioddr_pktio_ops = {
	.init = ioddr_init,
	.term = ioddr_destroy,
	.open = ioddr_open,
	.close = ioddr_close,
	.start = ioddr_start,
	.stop = ioddr_stop,
	.stats = ioddr_stats,
	.recv = ioddr_recv,
	.send = ioddr_send,
	.mtu_get = ioddr_mtu_get,
	.promisc_mode_set = ioddr_promisc_mode_set,
	.promisc_mode_get = ioddr_promisc_mode,
	.mac_get = ioddr_mac_addr_get,
};
