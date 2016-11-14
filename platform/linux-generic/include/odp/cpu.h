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

#include <odp/thread.h>

static inline int odp_cpu_id(void)
{
	return this_thread->cpu;
}

#include <odp/api/cpu.h>

#ifdef __cplusplus
}
#endif

#endif
