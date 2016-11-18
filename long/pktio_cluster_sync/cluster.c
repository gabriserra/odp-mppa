/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp.h>
#include <odp_cunit_common.h>

#include <odp/helper/eth.h>
#include <odp/helper/ip.h>
#include <odp/helper/udp.h>

#include <stdlib.h>
#include <mppa_power.h>

int main(int argc, char **argv)
{
	odp_instance_t instance;
	(void) argc;
	(void) argv;
	if (0 != odp_init_global(&instance, NULL, NULL)) {
		fprintf(stderr, "error: odp_init_global() failed.\n");
		return 1;
	}
	if (0 != odp_init_local(instance, ODP_THREAD_CONTROL)) {
		fprintf(stderr, "error: odp_init_local() failed.\n");
		return 1;
	}
	if (0 != odp_term_local()) {
		fprintf(stderr, "error: odp_term_local() failed.\n");
		return 1;
	}
	if (0 != odp_term_global(instance)) {
		fprintf(stderr, "error: odp_term_global() failed.\n");
		return 1;
	}

	return 0;
}
