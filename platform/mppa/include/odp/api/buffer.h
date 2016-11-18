/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP buffer descriptor
 */

#ifndef ODP_PLAT_BUFFER_H_
#define ODP_PLAT_BUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/api/std_types.h>
#include <odp/api/plat/event_types.h>
#include <odp/api/plat/buffer_types.h>
#include <odp/api/plat/pool_types.h>

/** @ingroup odp_buffer
 *  @{
 */

static inline odp_buffer_t odp_buffer_from_event(odp_event_t ev)
{
	return (odp_buffer_t)ev;
}

static inline odp_event_t odp_buffer_to_event(odp_buffer_t buf)
{
	return (odp_event_t)buf;
}
/**
 * @}
 */

#include <odp/api/spec/buffer.h>

#ifdef __cplusplus
}
#endif

#endif
