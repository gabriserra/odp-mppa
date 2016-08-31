/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */



#ifndef ODP_SPIN_INTERNAL_H_
#define ODP_SPIN_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <HAL/hal/core/atomic.h>

/**
 * Spin loop for ODP internal use
 */
static inline void odp_spin(void)
{
	__k1_cpu_backoff(10);
}


#ifdef __cplusplus
}
#endif

#endif
