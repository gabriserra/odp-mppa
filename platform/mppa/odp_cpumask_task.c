/* Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#include <pthread.h>

#include <odp/api/cpu.h>
#include <odp/api/cpumask.h>
#include <odp_debug_internal.h>

int odp_cpumask_default_worker(odp_cpumask_t *mask, int num_in)
{
	int i, count;
	int num = num_in;
	int cpu_count;
	const int abs_cpu_count = odp_cpu_count();
	cpu_count = abs_cpu_count - 1 - odp_global_data.n_rx_thr;

	/*
	 * If no user supplied number or it's too large, then attempt
	 * to use all CPUs
	 */
	if (0 == num)
		num = cpu_count;
	if (cpu_count < num)
		num = cpu_count;


	/* Build the mask */
	odp_cpumask_zero(mask);
	for (i = 1, count = 0; i < abs_cpu_count && count < num; i++) {
		int cpu = i;
		/* If this is a slot reserved by Rx threads, skip */
		if (cpu % 2 == 1 &&
		    (unsigned)((abs_cpu_count - 1 - i) / 2) < odp_global_data.n_rx_thr)
			continue;

		odp_cpumask_set(mask, cpu);
		count++;
	}

       if (odp_cpumask_isset(mask, 0))
               ODP_DBG("\n\tCPU0 will be used for both control and worker threads,\n"
                       "\tthis will likely have a performance impact on the worker thread.\n");


	return num;
}

int odp_cpumask_default_control(odp_cpumask_t *mask, int num ODP_UNUSED)
{
	odp_cpumask_zero(mask);
	/* By default all control threads on CPU 0 */
	odp_cpumask_set(mask, 0);
	return 1;
}

int odp_cpumask_all_available(odp_cpumask_t *mask)
{
	odp_cpumask_default_control(mask, 0);
	odp_cpumask_set(mask, 0);

	return odp_cpumask_count(mask);
}
