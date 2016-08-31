/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <odp/time.h>
#include <odp/hints.h>
#include <odp_debug_internal.h>
#include <HAL/hal/cluster/dsu.h>

static odp_time_t start_time;

static inline odp_time_t get_time(void)
{
	odp_time_t time;
	time.cycles = __k1_read_dsu_timestamp();
#ifdef DSU_DIVISOR
	time.cycles *= __k1_read_dsu_timestamp_divisor();
#endif
	return time;
}

static inline
uint64_t time_to_ns(odp_time_t time)
{
	const uint64_t freq = __bsp_frequency;

	return (time.cycles / freq) * ODP_TIME_SEC_IN_NS +
		(time.cycles % freq) * ODP_TIME_SEC_IN_NS / freq;
}

static inline odp_time_t time_diff(odp_time_t t2, odp_time_t t1)
{
	odp_time_t time;
	time.cycles = t2.cycles - t1.cycles;

	return time;
}

static inline odp_time_t time_local(void)
{
	return time_diff(get_time(), start_time);
}

static inline int time_cmp(odp_time_t t2, odp_time_t t1)
{
	if (t2.cycles < t1.cycles)
		return -1;

	return (t2.cycles > t1.cycles);
}

static inline odp_time_t time_sum(odp_time_t t1, odp_time_t t2)
{
	odp_time_t time;

	time.cycles = t1.cycles + t2.cycles;

	return time;
}

static inline odp_time_t time_local_from_ns(uint64_t ns)
{
	odp_time_t time;
	const uint64_t freq = __bsp_frequency;
	time.cycles = (ns / ODP_TIME_SEC_IN_NS) * freq +
		((ns % ODP_TIME_SEC_IN_NS) * freq) / ODP_TIME_SEC_IN_NS;

	return time;
}

static inline void time_wait_until(odp_time_t time)
{
	odp_time_t cur;

	do {
		cur = time_local();
	} while (time_cmp(time, cur) > 0);
}

static inline uint64_t time_local_res(void)
{
	return __bsp_frequency;
}

odp_time_t odp_time_local(void)
{
	return time_local();
}

odp_time_t odp_time_global(void)
{
	return time_local();
}

odp_time_t odp_time_diff(odp_time_t t2, odp_time_t t1)
{
	return time_diff(t2, t1);
}

uint64_t odp_time_to_ns(odp_time_t time)
{
	return time_to_ns(time);
}

odp_time_t odp_time_local_from_ns(uint64_t ns)
{
	return time_local_from_ns(ns);
}

odp_time_t odp_time_global_from_ns(uint64_t ns)
{
	return time_local_from_ns(ns);
}

int odp_time_cmp(odp_time_t t2, odp_time_t t1)
{
	return time_cmp(t2, t1);
}

odp_time_t odp_time_sum(odp_time_t t1, odp_time_t t2)
{
	return time_sum(t1, t2);
}

uint64_t odp_time_local_res(void)
{
	return time_local_res();
}

uint64_t odp_time_global_res(void)
{
	return time_local_res();
}

void odp_time_wait_ns(uint64_t ns)
{
	odp_time_t cur = time_local();
	odp_time_t wait = time_local_from_ns(ns);
	odp_time_t end_time = time_sum(cur, wait);

	time_wait_until(end_time);
}

void odp_time_wait_until(odp_time_t time)
{
	return time_wait_until(time);
}

uint64_t odp_time_to_u64(odp_time_t time)
{
	return time.cycles;
}

int odp_time_global_init(void)
{
	start_time = get_time();

	return 0;
}
