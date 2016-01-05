/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <odp/helper/linux.h>
#include <odp/thread.h>
#include <odp/init.h>
#include <odp/system_info.h>
#include "odph_debug.h"

static void *odp_run_start_routine(void *arg)
{
	odp_start_args_t *start_args = arg;

	/* ODP thread local init */
	if (odp_init_local(start_args->thr_type)) {
		ODPH_ERR("Local init failed\n");
		return NULL;
	}

	void *ret_ptr = start_args->start_routine(start_args->arg);
	int ret = odp_term_local();
	if (ret < 0)
		ODPH_ERR("Local term failed\n");
	else if (ret == 0 && odp_term_global())
		ODPH_ERR("Global term failed\n");

	return ret_ptr;
}


int odph_linux_pthread_create(odph_linux_pthread_t *thread_tbl,
			      const odp_cpumask_t *mask_in,
			      void *(*start_routine) (void *), void *arg,
			      odp_thread_type_t thr_type)
{
	int i;
	int num;
	odp_cpumask_t mask;
	int cpu_count;
	int cpu;

	odp_cpumask_copy(&mask, mask_in);
	num = odp_cpumask_count(&mask);

	memset(thread_tbl, 0, num * sizeof(odph_linux_pthread_t));

	cpu_count = odp_cpu_count();

	if (num < 1 || num > cpu_count) {
		ODPH_ERR("Bad num\n");
		return 0;
	}

	cpu = odp_cpumask_first(&mask);
	for (i = 0; i < num; i++) {
		odp_cpumask_t thd_mask;

		odp_cpumask_zero(&thd_mask);
		odp_cpumask_set(&thd_mask, cpu);

		pthread_attr_init(&thread_tbl[i].attr);

		thread_tbl[i].cpu = cpu;

		/* FIXME */
		pthread_attr_setaffinity_np(&thread_tbl[i].attr,
					    sizeof(cpu_set_t), &thd_mask.set);

		thread_tbl[i].start_args = malloc(sizeof(odp_start_args_t));
		if (thread_tbl[i].start_args == NULL)
			ODPH_ABORT("Malloc failed");

		thread_tbl[i].start_args->start_routine = start_routine;
		thread_tbl[i].start_args->arg           = arg;
		thread_tbl[i].start_args->thr_type      = thr_type;

		if(pthread_create(&thread_tbl[i].thread, &thread_tbl[i].attr,
				  odp_run_start_routine, thread_tbl[i].start_args))
			ODPH_ABORT("Thread failed");

		cpu = odp_cpumask_next(&mask, cpu);
	}
	return i;
}


void odph_linux_pthread_join(odph_linux_pthread_t *thread_tbl, int num)
{
	int i;
	int ret;

	for (i = 0; i < num; i++) {
		/* Wait thread to exit */
		ret = pthread_join(thread_tbl[i].thread, NULL);
		if (ret != 0) {
			ODPH_ERR("Failed to join thread from cpu #%d\n",
				thread_tbl[i].cpu);
		}
		pthread_attr_destroy(&thread_tbl[i].attr);
		free(thread_tbl[i].start_args);
	}

}

int odph_linux_process_fork_n(odph_linux_process_t *proc_tbl ODP_UNUSED,
			      const odp_cpumask_t *mask_in ODP_UNUSED)
{
	return -1;
}


int odph_linux_process_fork(odph_linux_process_t *proc ODP_UNUSED, int cpu ODP_UNUSED)
{
	return -1;
}

int odph_linux_process_wait_n(odph_linux_process_t *proc_tbl ODP_UNUSED, int num ODP_UNUSED)
{
	return -1;
}
