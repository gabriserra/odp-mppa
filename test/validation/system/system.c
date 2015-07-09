/* Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:	BSD-3-Clause
 */

#include <ctype.h>
#include <odp.h>
#include "odp_cunit_common.h"
#include "test_debug.h"
#include "system.h"

static void system_test_odp_version_numbers(void)
{
	int char_ok;
	char version_string[128];
	char *s = version_string;

	strncpy(version_string, odp_version_api_str(),
		sizeof(version_string) - 1);

	while (*s) {
		if (isdigit((int)*s) || (strncmp(s, ".", 1) == 0)) {
			char_ok = 1;
			s++;
		} else {
			char_ok = 0;
			LOG_DBG("\nBAD VERSION=%s\n", version_string);
			break;
		}
	}
	CU_ASSERT(char_ok);
}

static void system_test_odp_cpu_count(void)
{
	int cpus;

	cpus = odp_cpu_count();
	CU_ASSERT(0 < cpus);
}

static void system_test_odp_sys_cache_line_size(void)
{
	uint64_t cache_size;

	cache_size = odp_sys_cache_line_size();
	CU_ASSERT(0 < cache_size);
	CU_ASSERT(ODP_CACHE_LINE_SIZE == cache_size);
}

static void system_test_odp_sys_cpu_model_str(void)
{
	char model[128];

	snprintf(model, 128, "%s", odp_sys_cpu_model_str());
	CU_ASSERT(strlen(model) > 0);
	CU_ASSERT(strlen(model) < 127);
}

static void system_test_odp_sys_page_size(void)
{
	uint64_t page;

	page = odp_sys_page_size();
	CU_ASSERT(0 < page);
	CU_ASSERT(ODP_PAGE_SIZE == page);
}

static void system_test_odp_sys_huge_page_size(void)
{
	uint64_t page;

	page = odp_sys_huge_page_size();
	CU_ASSERT(0 < page);
}

static void system_test_odp_sys_cpu_hz(void)
{
	uint64_t hz;

	hz = odp_sys_cpu_hz();
	CU_ASSERT(0 < hz);
}

static CU_TestInfo system_suite[] = {
	{"odp version",  system_test_odp_version_numbers},
	{"odp_cpu_count",  system_test_odp_cpu_count},
	{"odp_sys_cache_line_size",  system_test_odp_sys_cache_line_size},
	{"odp_sys_cpu_model_str",  system_test_odp_sys_cpu_model_str},
	{"odp_sys_page_size",  system_test_odp_sys_page_size},
	{"odp_sys_huge_page_size",  system_test_odp_sys_huge_page_size},
	{"odp_sys_cpu_hz",  system_test_odp_sys_cpu_hz},
	CU_TEST_INFO_NULL,
};

static CU_SuiteInfo system_suites[] = {
	{"System Info", NULL, NULL, NULL, NULL, system_suite},
	CU_SUITE_INFO_NULL,
};

int system_main(void)
{
	return odp_cunit_run(system_suites);
}
