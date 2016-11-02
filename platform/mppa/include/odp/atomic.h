/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP atomic operations
 */

#ifndef ODP_PLAT_ATOMIC_H_
#define ODP_PLAT_ATOMIC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <odp/align.h>
#include <odp/plat/atomic_types.h>

/** @ingroup odpatomic
 *  @{
 */

static inline uint32_t odp_atomic_load_u32(odp_atomic_u32_t *atom)
{
	return LOAD_U32(atom->v);

}

static inline void odp_atomic_store_u32(odp_atomic_u32_t *atom,
					uint32_t val)
{
	return STORE_U32(atom->v, val);
}

static inline void odp_atomic_init_u32(odp_atomic_u32_t *atom, uint32_t val)
{
	odp_atomic_u32_t new = { ._u64 = 0ULL };
	new.v = val;
	STORE_U64(atom->_u64, new._u64);
}

static inline uint32_t odp_atomic_fetch_add_u32(odp_atomic_u32_t *atom,
						uint32_t val)
{
	unsigned long long val64 = val;
	asm volatile ("afdau 0[%1] = %0\n;;\n" : "+r"(val64) : "r" (&atom->_u64): "memory");
	return (unsigned)val64;
}

static inline uint32_t odp_atomic_fetch_sub_u32(odp_atomic_u32_t *atom,
						uint32_t val)
{
	long long val64 = -val;
	asm volatile ("afdau 0[%1] = %0\n;;\n" : "+r"(val64) : "r" (&atom->_u64): "memory");
	return (unsigned)val64;
}

static inline void odp_atomic_add_u32(odp_atomic_u32_t *atom,
				      uint32_t val)
{
	odp_atomic_fetch_add_u32(atom, val);
}

static inline void odp_atomic_sub_u32(odp_atomic_u32_t *atom,
				      uint32_t val)
{
	odp_atomic_fetch_sub_u32(atom, val);
}

static inline uint32_t odp_atomic_fetch_inc_u32(odp_atomic_u32_t *atom)
{
	return odp_atomic_fetch_add_u32(atom, 1);
}

static inline void odp_atomic_inc_u32(odp_atomic_u32_t *atom)
{
	odp_atomic_add_u32(atom, 1);
}

static inline uint32_t odp_atomic_fetch_dec_u32(odp_atomic_u32_t *atom)
{
	return odp_atomic_fetch_sub_u32(atom, 1);
}

static inline void odp_atomic_dec_u32(odp_atomic_u32_t *atom)
{
	odp_atomic_sub_u32(atom, 1);
}

static inline uint64_t odp_atomic_load_u64(odp_atomic_u64_t *atom)
{
	return LOAD_U64(atom->v);
}

static inline void odp_atomic_store_u64(odp_atomic_u64_t *atom,
					uint64_t val)
{
	return STORE_U64(atom->v, val);
}

static inline void odp_atomic_init_u64(odp_atomic_u64_t *atom, uint64_t val)
{
	STORE_U64(atom->v, val);
}

static inline uint64_t odp_atomic_fetch_add_u64(odp_atomic_u64_t *atom,
						uint64_t val)
{
	unsigned long long val64 = val;
	asm volatile ("afdau 0[%1] = %0\n;;\n" : "+r"(val64) : "r" (&atom->_u64): "memory");
	return (unsigned)val64;
}

static inline uint64_t odp_atomic_fetch_sub_u64(odp_atomic_u64_t *atom,
						uint64_t val)
{
	long long val64 = -val;
	asm volatile ("afdau 0[%1] = %0\n;;\n" : "+r"(val64) : "r" (&atom->_u64): "memory");
	return (unsigned)val64;
}

static inline void odp_atomic_add_u64(odp_atomic_u64_t *atom, uint64_t val)
{
	odp_atomic_fetch_add_u64(atom, val);
}

static inline void odp_atomic_sub_u64(odp_atomic_u64_t *atom, uint64_t val)
{
	odp_atomic_fetch_sub_u64(atom, val);
}

static inline uint64_t odp_atomic_fetch_inc_u64(odp_atomic_u64_t *atom)
{
	return odp_atomic_fetch_add_u64(atom, 1ULL);
}

static inline void odp_atomic_inc_u64(odp_atomic_u64_t *atom)
{
	return odp_atomic_add_u64(atom, 1ULL);
}

static inline uint64_t odp_atomic_fetch_dec_u64(odp_atomic_u64_t *atom)
{
	return odp_atomic_fetch_sub_u64(atom, 1ULL);
}

static inline void odp_atomic_dec_u64(odp_atomic_u64_t *atom)
{
	odp_atomic_sub_u64(atom, 1ULL);
}

/**
 * @}
 */

#include <odp/api/atomic.h>

#ifdef __cplusplus
}
#endif

#endif
