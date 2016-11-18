/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

#include <odp/api/cpu.h>
#include <odp/api/cpumask.h>
#include <odp_debug_internal.h>

#include <stdlib.h>
#include <string.h>

void odp_cpumask_from_str(odp_cpumask_t *mask, const char *str_in)
{
	uint32_t cpuset = 0;
	const char *str = str_in;
	const char *p;
	int cpu = 0;
	int len = strlen(str);

	odp_cpumask_zero(mask);

	/* Strip leading "0x"/"0X" if present and verify length */
	if ((len >= 2) && ((str[1] == 'x') || (str[1] == 'X'))) {
		str += 2;
		len -= 2;
	}
	if (!len)
		return;

	/* Walk string from LSB setting cpu bits */
	for (p = str + len - 1; (len > 0) && (cpu < CPU_SETSIZE); p--, len--) {
		char c = *p;
		int value;
		int idx;

		/* Convert hex nibble, abort when invalid value found */
		if ((c >= '0') && (c <= '9'))
			value = c - '0';
		else if ((c >= 'A') && (c <= 'F'))
			value = c - 'A' + 10;
		else if ((c >= 'a') && (c <= 'f'))
			value = c - 'a' + 10;
		else
			return;

		/* Walk converted nibble and set bits in mask */
		for (idx = 0; idx < 4; idx++, cpu++)
			if (value & (1 << idx))
				cpuset |= 1 << cpu;
	}

	/* Copy the computed mask */
	memcpy(&mask->set, &cpuset, sizeof(cpuset));
}

int32_t odp_cpumask_to_str(const odp_cpumask_t *mask, char *str, int32_t len)
{
	char *p = str;
	int cpu = odp_cpumask_last(mask);
	int nibbles;
	int value;

	/* Handle bad string length, need at least 4 chars for "0x0" and
	 * terminating null char */
	if (len < 4)
		return -1; /* Failure */

	/* Handle no CPU found */
	if (cpu < 0) {
		strcpy(str, "0x0");
		return strlen(str) + 1; /* Success */
	}
	/* CPU was found and cpu >= 0 */

	/* Compute number of nibbles in cpumask that have bits set */
	nibbles = (cpu / 4) + 1;

	/* Verify minimum space (account for "0x" and termination) */
	if (len < (3 + nibbles))
		return -1; /* Failure */

	/* Prefix */
	*p++ = '0';
	*p++ = 'x';

	/*
	 * Now we can scan the cpus down to zero and
	 * build the string one nibble at a time
	 */
	value = 0;
	do {
		/* Set bit to go into the current nibble */
		if (mask->set & (1 << cpu))
			value |= 1 << (cpu % 4);

		/* If we are on a nibble boundary flush value to string */
		if (0 == (cpu % 4)) {
			if (value < 0xA)
				*p++ = '0' + value;
			else
				*p++ = 'A' + value - 0xA;
			value = 0;
		}
	} while (cpu--);

	/* Terminate the string */
	*p++ = 0;
	return p - str; /* Success */
}

void odp_cpumask_zero(odp_cpumask_t *mask)
{
	mask->set = 0;
}

void odp_cpumask_set(odp_cpumask_t *mask, int cpu)
{
	mask->set |= 1 << cpu;
}

void odp_cpumask_setall(odp_cpumask_t *mask)
{
	int cpu;

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
		mask->set |= 1 << cpu;
}

void odp_cpumask_clr(odp_cpumask_t *mask, int cpu)
{
	mask->set &= ~(1 << cpu);
}

int odp_cpumask_isset(const odp_cpumask_t *mask, int cpu)
{
	return mask->set & (1 << cpu);
}

int odp_cpumask_count(const odp_cpumask_t *mask)
{
	unsigned v = mask->set;
	return __builtin_popcount(v);
}

void odp_cpumask_and(odp_cpumask_t *dest, const odp_cpumask_t *src1,
		     const odp_cpumask_t *src2)
{
	dest->set = src1->set & src2->set;
}

void odp_cpumask_or(odp_cpumask_t *dest, const odp_cpumask_t *src1,
		    const odp_cpumask_t *src2)
{
	dest->set = src1->set | src2->set;
}

void odp_cpumask_xor(odp_cpumask_t *dest, const odp_cpumask_t *src1,
		     const odp_cpumask_t *src2)
{
	dest->set = src1->set ^ src2->set;
}

int odp_cpumask_equal(const odp_cpumask_t *mask1,
		      const odp_cpumask_t *mask2)
{
	return mask1->set == mask2->set;
}

void odp_cpumask_copy(odp_cpumask_t *dest, const odp_cpumask_t *src)
{
	dest->set = src->set;
}

int odp_cpumask_first(const odp_cpumask_t *mask)
{
	int cpu;

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
		if (odp_cpumask_isset(mask, cpu))
			return cpu;
	return -1;
}

int odp_cpumask_last(const odp_cpumask_t *mask)
{
	int cpu;

	for (cpu = CPU_SETSIZE - 1; cpu >= 0; cpu--)
		if (odp_cpumask_isset(mask, cpu))
			return cpu;
	return -1;
}

int odp_cpumask_next(const odp_cpumask_t *mask, int cpu)
{
	for (cpu += 1; cpu < CPU_SETSIZE; cpu++)
		if (odp_cpumask_isset(mask, cpu))
			return cpu;
	return -1;
}

