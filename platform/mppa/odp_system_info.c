/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/api/system_info.h>
#include <odp_internal.h>
#include <odp_debug_internal.h>
#include <odp_packet_io_internal.h>
#include <odp/api/align.h>
#include <odp/api/cpu.h>
#include <string.h>
#include <stdio.h>
#include <mppa_bsp.h>
/* sysconf */
#include <unistd.h>

/*
 * System info initialisation
 */
int odp_system_info_init(void)
{
	memset(&odp_global_data.system_info, 0, sizeof(odp_system_info_t));

	odp_global_data.system_info.page_size = ODP_PAGE_SIZE;

	odp_global_data.system_info.cpu_count = BSP_NB_PE_P;
	odp_global_data.system_info.huge_page_size =  ODP_PAGE_SIZE;
	odp_global_data.system_info.cpu_hz          = _K1_CPU_FREQ;

	if (__bsp_flavour == BSP_EXPLORER) {
		odp_global_data.system_info.cpu_hz = 20000000ULL;
	}

	odp_global_data.system_info.cache_line_size = _K1_DCACHE_LINE_SIZE;

#if defined(__K1B__)
#define K1_MODEL_STR	"K1B - Bostan"
#else
#define K1_MODEL_STR	"K1 - Unknown"
#endif
	snprintf(odp_global_data.system_info.model_str,
		 sizeof(odp_global_data.system_info.model_str),
		 "%s", K1_MODEL_STR);

	return 0;
}

/*
 *************************
 * Public access functions
 *************************
 */
uint64_t odp_cpu_hz(void)
{
	return odp_global_data.system_info.cpu_hz;
}

uint64_t odp_cpu_hz_id(int id ODP_UNUSED)
{
	return odp_cpu_hz();
}

uint64_t odp_cpu_hz_max(void)
{
	return odp_global_data.system_info.cpu_hz;
}

uint64_t odp_cpu_hz_max_id(int id ODP_UNUSED)
{
	return odp_cpu_hz_max();
}

uint64_t odp_sys_huge_page_size(void)
{
	return odp_global_data.system_info.huge_page_size;
}

uint64_t odp_sys_page_size(void)
{
	return odp_global_data.system_info.page_size;
}

const char *odp_cpu_model_str(void)
{
	return odp_global_data.system_info.model_str;
}

const char *odp_cpu_model_str_id(int id ODP_UNUSED)
{
	return odp_cpu_model_str();
}

int odp_sys_cache_line_size(void)
{
	return odp_global_data.system_info.cache_line_size;
}

int odp_cpu_count(void)
{
	return odp_global_data.system_info.cpu_count;
}
