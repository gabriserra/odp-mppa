/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp_packet_io_internal.h>

/* Ops for all implementation of pktio.
 * Order matters. The first implementation to setup successfully
 * will be picked.
 * Array must be NULL terminated */
const pktio_if_ops_t * const pktio_if_ops[]  = {
	&loopback_pktio_ops,
	&drop_pktio_ops,
	&magic_pktio_ops,
	&cluster_pktio_ops,
	&eth_pktio_ops,
	&pcie_pktio_ops,
	&ioddr_pktio_ops,
	NULL
};
