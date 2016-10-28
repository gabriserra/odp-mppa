/* Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP initialization.
 */

#ifndef ODP_INIT_TYPES_H_
#define ODP_INIT_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal platform specific data
 */
typedef struct odp_platform_init_t {
	unsigned n_rx_thr;              /**< Number of PE dedicated to handle
					 *   incoming Rx packets */
	int sort_buffers;               /**< Force buffer reordering when n_rx_thr > 1
					 * This only work if a pktio is affected to a single threads */
	unsigned enable_pkt_nofree;     /**< Enable usage of the nofree refcount in packets */
} odp_platform_init_t;

#ifdef __cplusplus
}
#endif

#endif
