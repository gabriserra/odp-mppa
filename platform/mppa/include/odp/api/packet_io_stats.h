/* Copyright (c) 2016, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP packet IO stats
 */

#ifndef ODP_PLAT_PACKET_IO_STATS_H_
#define ODP_PLAT_PACKET_IO_STATS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/api/spec/packet_io_stats.h>

/**
 * Print pktio statistics
 * @param      pktio    Packet IO handle
 * @param      stats    Output buffer for counters
 */
void _odp_pktio_stats_print(odp_pktio_t pktio,
			    const odp_pktio_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
