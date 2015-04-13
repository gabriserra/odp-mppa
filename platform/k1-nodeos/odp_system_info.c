/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/system_info.h>
#include <odp_internal.h>
#include <odp_debug_internal.h>
#include <odp/align.h>
#include <odp/cpu.h>
#include <string.h>
#include <stdio.h>

/* sysconf */
#include <unistd.h>



typedef struct {
	const char *cpu_arch_str;
	int (*cpuinfo_parser)(FILE *file, odp_system_info_t *sysinfo);

} odp_compiler_info_t;

/*
 * Report the number of online CPU's
 */
static int sysconf_cpu_count(void)
{
	long ret;

	ret = sysconf(_SC_NPROCESSORS_ONLN);
	if (ret < 0)
		return 0;

	return (int)ret;
}

static int huge_page_size(void)
{
#ifdef __K1__
	return 0;
#else
	DIR *dir;
	struct dirent *dirent;
	int size = 0;

	dir = opendir(HUGE_PAGE_DIR);
	if (dir == NULL) {
		ODP_ERR("%s not found\n", HUGE_PAGE_DIR);
		return 0;
	}

	while ((dirent = readdir(dir)) != NULL) {
		int temp = 0;
		sscanf(dirent->d_name, "hugepages-%i", &temp);

		if (temp > size)
			size = temp;
	}

	if (closedir(dir)) {
		ODP_ERR("closedir failed\n");
		return 0;
	}

	return size*1024;
#endif
}



/*
 * HW specific /proc/cpuinfo file parsing
 */
#if defined __x86_64__ || defined __i386__

static int cpuinfo_x86(FILE *file, odp_system_info_t *sysinfo)
{
	char str[1024];
	char *pos;
	double mhz = 0.0;
	int model = 0;
	int count = 2;

	while (fgets(str, sizeof(str), file) != NULL && count > 0) {
		if (!mhz) {
			pos = strstr(str, "cpu MHz");
			if (pos) {
				sscanf(pos, "cpu MHz : %lf", &mhz);
				count--;
			}
		}

		if (!model) {
			pos = strstr(str, "model name");
			if (pos) {
				int len;
				pos = strchr(str, ':');
				strncpy(sysinfo->model_str, pos+2,
					sizeof(sysinfo->model_str));
				len = strlen(sysinfo->model_str);
				sysinfo->model_str[len - 1] = 0;
				model = 1;
				count--;
			}
		}
	}

	sysinfo->cpu_hz = (uint64_t) (mhz * 1000000.0);

	return 0;
}

#elif defined __arm__ || defined __aarch64__

static int cpuinfo_arm(FILE *file ODP_UNUSED,
odp_system_info_t *sysinfo ODP_UNUSED)
{
	return 0;
}

#elif defined __OCTEON__

static int cpuinfo_octeon(FILE *file, odp_system_info_t *sysinfo)
{
	char str[1024];
	char *pos;
	double mhz = 0.0;
	int model = 0;
	int count = 2;

	while (fgets(str, sizeof(str), file) != NULL && count > 0) {
		if (!mhz) {
			pos = strstr(str, "BogoMIPS");

			if (pos) {
				sscanf(pos, "BogoMIPS : %lf", &mhz);
				count--;
			}
		}

		if (!model) {
			pos = strstr(str, "cpu model");

			if (pos) {
				int len;
				pos = strchr(str, ':');
				strncpy(sysinfo->model_str, pos+2,
					sizeof(sysinfo->model_str));
				len = strlen(sysinfo->model_str);
				sysinfo->model_str[len - 1] = 0;
				model = 1;
				count--;
			}
		}
	}

	/* bogomips seems to be 2x freq */
	sysinfo->cpu_hz = (uint64_t) (mhz * 1000000.0 / 2.0);

	return 0;
}
#elif defined __powerpc__
static int cpuinfo_mppa(FILE *file, odp_system_info_t *sysinfo)
{
	char str[1024];
	char *pos;
	double mhz = 0.0;
	int model = 0;
	int count = 2;

	while (fgets(str, sizeof(str), file) != NULL && count > 0) {
		if (!mhz) {
			pos = strstr(str, "clock");

			if (pos) {
				sscanf(pos, "clock : %lf", &mhz);
				count--;
			}
		}

		if (!model) {
			pos = strstr(str, "cpu");

			if (pos) {
				int len;
				pos = strchr(str, ':');
				strncpy(sysinfo->model_str, pos+2,
					sizeof(sysinfo->model_str));
				len = strlen(sysinfo->model_str);
				sysinfo->model_str[len - 1] = 0;
				model = 1;
				count--;
			}
		}

		sysinfo->cpu_hz = (uint64_t) (mhz * 1000000.0);
	}


	return 0;
}
#elif defined __K1__
static int cpuinfo_mppa(FILE *file ODP_UNUSED, odp_system_info_t *sysinfo)
{
	sysinfo->cpu_hz = 400000000ULL;
	return 0;
}

#else
	#error GCC target not found
#endif

static odp_compiler_info_t compiler_info = {
	#if defined __x86_64__ || defined __i386__
	.cpu_arch_str = "x86",
	.cpuinfo_parser = cpuinfo_x86

	#elif defined __arm__ || defined __aarch64__
	.cpu_arch_str = "arm",
	.cpuinfo_parser = cpuinfo_arm

	#elif defined __OCTEON__
	.cpu_arch_str = "octeon",
	.cpuinfo_parser = cpuinfo_octeon

	#elif defined __powerpc__
	.cpu_arch_str = "powerpc",
	.cpuinfo_parser = cpuinfo_powerpc

	#elif defined __K1__
	.cpu_arch_str = "mppa",
	.cpuinfo_parser = cpuinfo_mppa

	#else
	#error GCC target not found
	#endif
};


#if defined __x86_64__ || defined __i386__ || defined __OCTEON__ || \
defined __powerpc__

/*
 * Analysis of /sys/devices/system/cpu/ files
 */
static int systemcpu(odp_system_info_t *sysinfo)
{
	int ret;

	ret = sysconf_cpu_count();
	if (ret == 0) {
		ODP_ERR("sysconf_cpu_count failed.\n");
		return -1;
	}

	sysinfo->cpu_count = ret;


	ret = systemcpu_cache_line_size();
	if (ret == 0) {
		ODP_ERR("systemcpu_cache_line_size failed.\n");
		return -1;
	}

	sysinfo->cache_line_size = ret;

	if (ret != ODP_CACHE_LINE_SIZE) {
		ODP_ERR("Cache line sizes definitions don't match.\n");
		return -1;
	}

	odp_global_data.system_info.huge_page_size = huge_page_size();

	return 0;
}

#else

/*
 * Use sysconf and dummy values in generic case
 */


static int systemcpu(odp_system_info_t *sysinfo)
{
	int ret;

	ret = sysconf_cpu_count();
	if (ret == 0) {
		ODP_ERR("sysconf_cpu_count failed.\n");
		return -1;
	}

	sysinfo->cpu_count = ret;

	sysinfo->huge_page_size = huge_page_size();

	/* Dummy values */
	sysinfo->cpu_hz          = 400000000;
	sysinfo->cache_line_size = _K1_DCACHE_LINE_SIZE;

	strncpy(sysinfo->model_str, "K1B - Bostan", sizeof(sysinfo->model_str));

	return 0;
}

#endif

/*
 * System info initialisation
 */
int odp_system_info_init(void)
{
	FILE  *file;

	memset(&odp_global_data.system_info, 0, sizeof(odp_system_info_t));

	odp_global_data.system_info.page_size = ODP_PAGE_SIZE;

	file = fopen("/proc/cpuinfo", "rt");
	if (file == NULL) {
		ODP_ERR("Failed to open /proc/cpuinfo\n");
		return -1;
	}

	compiler_info.cpuinfo_parser(file, &odp_global_data.system_info);

	fclose(file);

	if (systemcpu(&odp_global_data.system_info)) {
		ODP_ERR("systemcpu failed\n");
		return -1;
	}

	return 0;
}

/*
 *************************
 * Public access functions
 *************************
 */
uint64_t odp_sys_cpu_hz(void)
{
	return odp_global_data.system_info.cpu_hz;
}

uint64_t odp_sys_huge_page_size(void)
{
	return odp_global_data.system_info.huge_page_size;
}

uint64_t odp_sys_page_size(void)
{
	return odp_global_data.system_info.page_size;
}

const char *odp_sys_cpu_model_str(void)
{
	return odp_global_data.system_info.model_str;
}

int odp_sys_cache_line_size(void)
{
	return odp_global_data.system_info.cache_line_size;
}

int odp_cpu_count(void)
{
	return odp_global_data.system_info.cpu_count;
}
