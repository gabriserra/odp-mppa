/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP Packet IO
 */

#ifndef ODP_PLAT_PACKET_IO_H_
#define ODP_PLAT_PACKET_IO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/std_types.h>
#include <odp/plat/pool_types.h>
#include <odp/plat/classification_types.h>
#include <odp/plat/packet_types.h>
#include <odp/plat/packet_io_types.h>
#include <odp/plat/queue_types.h>

/** @ingroup odp_packet_io
 *  @{
 */

/**
 * Get statistics for pktio handle
 *
 * @param	pktio	 Packet IO handle
 * @param[out]	stats	 Output buffer for counters
 * @retval  0 on success
 * @retval <0 on failure
 *
 * @note: If counter is not supported by platform it has
 *	  to be set to 0.
 */
int _odp_pktio_stats(odp_pktio_t pktio,
		     _odp_pktio_stats_t *stats);

/**
 * @}
 */

#include <odp/api/packet_io.h>

#ifdef __cplusplus
}
#endif

#endif
