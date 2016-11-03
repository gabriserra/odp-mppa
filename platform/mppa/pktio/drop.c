/* Copyright (c) 2013, Linaro Limited
 * Copyright (c) 2013, Nokia Solutions and Networks
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <odp.h>
#include <odp_packet_io_internal.h>
#include <odp/helper/eth.h>

static int drop_open(odp_pktio_t id ODP_UNUSED,
		     pktio_entry_t *pktio_entry ODP_UNUSED,
		     const char *devname, odp_pool_t pool ODP_UNUSED)
{
	if (strcmp(devname, "drop"))
		return -1;

	return 0;
}

static int drop_close(pktio_entry_t *pktio_entry ODP_UNUSED)
{
	return 0;
}

static int drop_recv_pkt(pktio_entry_t *pktio_entry ODP_UNUSED,
			 odp_packet_t pkts[] ODP_UNUSED,
			 unsigned len ODP_UNUSED)
{

	return 0;
}

static int drop_send_pkt(pktio_entry_t *pktio_entry ODP_UNUSED,
			 odp_packet_t pkt_tbl[] ODP_UNUSED,
			 unsigned len)
{
	odp_packet_free_multi(pkt_tbl, len);
	return len;
}

static int drop_mtu_get(pktio_entry_t *pktio_entry ODP_UNUSED)
{
	return INT_MAX;
}

static int drop_mac_addr_get(pktio_entry_t *pktio_entry ODP_UNUSED,
			     void *mac_addr ODP_UNUSED)
{
	static const char drop_mac[ODPH_ETHADDR_LEN] =
		{ 0x2, 0xb, 0x0, 0x0, 0xb, 0x5 };
	memcpy(mac_addr, drop_mac, ODPH_ETHADDR_LEN);
	return ODPH_ETHADDR_LEN;
}

static int drop_promisc_mode_set(pktio_entry_t *pktio_entry ODP_UNUSED,
				 odp_bool_t enable ODP_UNUSED)
{
	return 0;
}

static int drop_promisc_mode_get(pktio_entry_t *pktio_entry ODP_UNUSED)
{
	return 0;
}

const pktio_if_ops_t drop_pktio_ops = {
	.init = NULL,
	.term = NULL,
	.open = drop_open,
	.close = drop_close,
	.start = NULL,
	.stop = NULL,
	.recv = drop_recv_pkt,
	.send = drop_send_pkt,
	.mtu_get = drop_mtu_get,
	.promisc_mode_set = drop_promisc_mode_set,
	.promisc_mode_get = drop_promisc_mode_get,
	.mac_get = drop_mac_addr_get
};
