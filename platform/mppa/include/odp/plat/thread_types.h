/* Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP thread
 */

#ifndef ODP_THREAD_TYPES_H_
#define ODP_THREAD_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup odp_thread
 *  @{
 */

#define ODP_THREAD_COUNT_MAX 16

#ifndef CPU_SETSIZE
#define CPU_SETSIZE (32)
#endif

typedef struct {
	int thr;
	int cpu;
	enum odp_thread_type_e type;
} thread_state_t;

extern __thread const thread_state_t * const this_thread;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
