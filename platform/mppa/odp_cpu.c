/* Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/api/cpu.h>
#include <odp/api/hints.h>
#include <HAL/hal/cluster/dsu.h>

uint64_t odp_cpu_cycles(void)
{
	return __k1_read_dsu_timestamp();
}

uint64_t odp_cpu_cycles_diff(uint64_t c2, uint64_t c1)
{
	if (odp_likely(c2 >= c1))
		return c2 - c1;

	return c2 + (odp_cpu_cycles_max() - c1) + 1;
}

uint64_t odp_cpu_cycles_max(void)
{
	return UINT64_MAX;
}

uint64_t odp_cpu_cycles_resolution(void)
{
	return 1;
}
