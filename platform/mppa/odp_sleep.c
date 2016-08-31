/* Copyright (c) 2015, Kalray
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP sleep service
 *
 */

#include <stdint.h>
#include <unistd.h>
#include <HAL/hal/cluster/dsu.h>
#include "../../syscall/include/common.h"

static int my_nanosleep(struct timespec *ts){
	uint64_t freq = __bsp_frequency;
	uint64_t tdiff = (ts->tv_sec * freq) + ((ts->tv_nsec * freq) / 1000000000ULL);

#ifdef MAGIC_SCALL
 	uint64_t divisor = __k1_read_dsu_timestamp_divisor();
	uint64_t dsu_ts = __k1_read_dsu_timestamp();
	uint64_t cc = dsu_ts * divisor;
	uint64_t target = cc + tdiff;;
	return __k1_syscall2(MAGIC_SCALL_SLEEP, target & 0xffffffffULL, target >> 32);
#else
	while(tdiff > INT32_MAX){
		__k1_cpu_backoff(INT32_MAX);
		tdiff -= INT32_MAX;
	}
	__k1_cpu_backoff(tdiff);
	return 0;
#endif
}

unsigned int sleep(unsigned int seconds){
   struct timespec ts;

    ts.tv_sec = seconds;
    ts.tv_nsec = 0;
    return my_nanosleep(&ts);
};
