/* Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP CPU
 */

#ifndef ODP_PLAT_CPU_H_
#define ODP_PLAT_CPU_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/api/thread.h>
#include <HAL/hal/core/atomic.h>

/* Forward declaration */
struct timespec;

/**
 * Spin loop for ODP internal use
 */
static inline void odp_cpu_pause(void)
{
	__k1_cpu_backoff(10);
}

int _odp_nanosleep(struct timespec *ts);

static inline int odp_cpu_id(void)
{
	return this_thread->cpu;
}

#include <odp/api/spec/cpu.h>

#ifdef __cplusplus
}
#endif

#endif
