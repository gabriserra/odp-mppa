/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <string.h>
#include <odp.h>
#include <odp_cunit_common.h>
#include <odp/helper/linux.h>
/* Globals */
static odph_linux_pthread_t thread_tbl[MAX_WORKERS];

/*
 * global init/term functions which may be registered
 * defaults to functions performing odp init/term.
 */
static int tests_global_init(void);
static int tests_global_term(void);
static struct {
	int (*global_init_ptr)(void);
	int (*global_term_ptr)(void);
} global_init_term = {tests_global_init, tests_global_term};

/** create test thread */
int odp_cunit_thread_create(void *func_ptr(void *), pthrd_arg *arg)
{
	odp_cpumask_t cpumask;

	/* Create and init additional threads */
	odph_linux_cpumask_default(&cpumask, arg->numthrds);
	odph_linux_pthread_create(thread_tbl, &cpumask, func_ptr,
				  (void *)arg);

	return 0;
}

/** exit from test thread */
int odp_cunit_thread_exit(pthrd_arg *arg)
{
	/* Wait for other threads to exit */
	odph_linux_pthread_join(thread_tbl, arg->numthrds);

	return 0;
}

static int tests_global_init(void)
{
	if (0 != odp_init_global(NULL, NULL)) {
		fprintf(stderr, "error: odp_init_global() failed.\n");
		return -1;
	}
	if (0 != odp_init_local()) {
		fprintf(stderr, "error: odp_init_local() failed.\n");
		return -1;
	}

	return 0;
}

static int tests_global_term(void)
{
	if (0 != odp_term_local()) {
		fprintf(stderr, "error: odp_term_local() failed.\n");
		return -1;
	}

	if (0 != odp_term_global()) {
		fprintf(stderr, "error: odp_term_global() failed.\n");
		return -1;
	}

	return 0;
}

/*
 * register tests_global_init and tests_global_term functions.
 * If some of these functions are not registered, the defaults functions
 * (tests_global_init() and tests_global_term()) defined above are used.
 * One should use these register functions when defining these hooks.
 * Note that passing NULL as function pointer is valid and will simply
 * prevent the default (odp init/term) to be done.
 */
void odp_cunit_register_global_init(int (*func_init_ptr)(void))
{
	global_init_term.global_init_ptr = func_init_ptr;
}

void odp_cunit_register_global_term(int (*func_term_ptr)(void))
{
	global_init_term.global_term_ptr = func_term_ptr;
}

int odp_cunit_run(CU_SuiteInfo testsuites[])
{
	int ret;

	printf("\tODP API version: %s\n", odp_version_api_str());
	printf("\tODP implementation version: %s\n", odp_version_impl_str());

	/* call test executable init hook, if any */
	if (global_init_term.global_init_ptr &&
	    ((*global_init_term.global_init_ptr)() != 0))
		return -1;

	CU_set_error_action(CUEA_ABORT);

	CU_initialize_registry();
	CU_register_suites(testsuites);
	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();

	ret = CU_get_number_of_failure_records();

	CU_cleanup_registry();

	/* call test executable terminason hook, if any */
	if (global_init_term.global_term_ptr &&
	    ((*global_init_term.global_term_ptr)() != 0))
		return -1;

	return (ret) ? -1 : 0;
}
