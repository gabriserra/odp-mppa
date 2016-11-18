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

#define ODP_ENABLE_CAS64

#include <stdint.h>
#include <odp/api/align.h>
#include <odp/api/plat/atomic_types.h>

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

static inline int odp_atomic_cas_u32(odp_atomic_u32_t *atom, uint32_t *old_val,
				     uint32_t new_val)
{
	__k1_uint64_t tmp = 0;
	uint32_t read_val;

	tmp = __builtin_k1_acwsu((void *)&atom->v, new_val, *old_val );
	read_val = tmp & 0xFFFFFFFF;
	if(read_val == *old_val){
		return 1;
	} else {
		*old_val = read_val;
		return 0;
	}
}

static inline uint32_t odp_atomic_xchg_u32(odp_atomic_u32_t *atom,
					   uint32_t new_val)
{
	uint32_t old_val = LOAD_U32(atom->v);
	do {
		__k1_uint64_t tmp = 0;
		tmp = __builtin_k1_acwsu((void *)&atom->v, new_val, old_val );

		uint32_t ret_val = tmp & 0xFFFFFFFF;
		if (ret_val == old_val)
			return ret_val;

		old_val = ret_val;
	} while(1);
}

static inline void odp_atomic_max_u32(odp_atomic_u32_t *atom, uint32_t new_max)
{
	uint32_t old_val;

	old_val = odp_atomic_load_u32(atom);

	while (new_max > old_val) {
		if (odp_atomic_cas_u32(atom, &old_val, new_max))
			break;
	}
}

static inline void odp_atomic_min_u32(odp_atomic_u32_t *atom, uint32_t new_min)
{
	uint32_t old_val;

	old_val = odp_atomic_load_u32(atom);

	while (new_min < old_val) {
		if (odp_atomic_cas_u32(atom, &old_val, new_min))
			break;
	}
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
#ifdef ODP_ENABLE_CAS64
	odp_atomic_store_u32(&atom->lock, 0);
#endif
}

static inline uint64_t odp_atomic_fetch_add_u64(odp_atomic_u64_t *atom,
						uint64_t val)
{
#ifdef ODP_ENABLE_CAS64
	return ATOMIC_OP(atom, STORE_U64(atom->v, _old_val + val));
#else
	unsigned long long val64 = val;
	asm volatile ("afdau 0[%1] = %0\n;;\n" : "+r"(val64) : "r" (&atom->_u64): "memory");
	return (unsigned)val64;
#endif
}

static inline uint64_t odp_atomic_fetch_sub_u64(odp_atomic_u64_t *atom,
						uint64_t val)
{
#ifdef ODP_ENABLE_CAS64
	return ATOMIC_OP(atom, STORE_U64(atom->v, _old_val - val));
#else
	long long val64 = -val;
	asm volatile ("afdau 0[%1] = %0\n;;\n" : "+r"(val64) : "r" (&atom->_u64): "memory");
	return (unsigned)val64;
#endif
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

#ifdef ODP_ENABLE_CAS64
static inline int odp_atomic_cas_u64(odp_atomic_u64_t *atom, uint64_t *old_val,
				     uint64_t new_val)
{
	int ret;
	ATOMIC_OP(atom,
		  {
			  if (_old_val == *old_val) {
				  STORE_U64(atom->v, new_val);
				  ret = 1;
			  } else {
				  ret = 0;
			  }
			  *old_val = _old_val;

		  });
	return ret;
}

static inline uint64_t odp_atomic_xchg_u64(odp_atomic_u64_t *atom,
					   uint64_t new_val)
{
	return ATOMIC_OP(atom, STORE_U64(atom->v, new_val));
}

static inline void odp_atomic_max_u64(odp_atomic_u64_t *atom, uint64_t new_max)
{
	uint64_t old_val;

	old_val = odp_atomic_load_u64(atom);

	while (new_max > old_val) {
		if (odp_atomic_cas_u64(atom, &old_val, new_max))
			break;
	}
}

static inline void odp_atomic_min_u64(odp_atomic_u64_t *atom, uint64_t new_min)
{
	uint64_t old_val;

	old_val = odp_atomic_load_u64(atom);

	while (new_min < old_val) {
		if (odp_atomic_cas_u64(atom, &old_val, new_min))
			break;
	}
}
#endif

static inline uint32_t odp_atomic_load_acq_u32(odp_atomic_u32_t *atom)
{
	return odp_atomic_load_u32(atom);
}

static inline void odp_atomic_store_rel_u32(odp_atomic_u32_t *atom,
					    uint32_t val)
{
	odp_atomic_store_u32(atom, val);
}

static inline void odp_atomic_add_rel_u32(odp_atomic_u32_t *atom,
					  uint32_t val)
{
	odp_atomic_add_u32(atom, val);
}

static inline void odp_atomic_sub_rel_u32(odp_atomic_u32_t *atom,
					  uint32_t val)
{
	odp_atomic_sub_u32(atom, val);
}

static inline int odp_atomic_cas_acq_u32(odp_atomic_u32_t *atom,
					 uint32_t *old_val, uint32_t new_val)
{
	return odp_atomic_cas_u32(atom, old_val, new_val);
}

static inline int odp_atomic_cas_rel_u32(odp_atomic_u32_t *atom,
					 uint32_t *old_val, uint32_t new_val)
{
	return odp_atomic_cas_u32(atom, old_val, new_val);
}

static inline int odp_atomic_cas_acq_rel_u32(odp_atomic_u32_t *atom,
					     uint32_t *old_val,
					     uint32_t new_val)
{
	return odp_atomic_cas_u32(atom, old_val, new_val);
}

static inline uint64_t odp_atomic_load_acq_u64(odp_atomic_u64_t *atom)
{
	return odp_atomic_load_u64(atom);
}

static inline void odp_atomic_store_rel_u64(odp_atomic_u64_t *atom,
					    uint64_t val)
{
	odp_atomic_store_u64(atom, val);
}

static inline void odp_atomic_add_rel_u64(odp_atomic_u64_t *atom,
					  uint64_t val)
{
	odp_atomic_add_u64(atom, val);
}

static inline void odp_atomic_sub_rel_u64(odp_atomic_u64_t *atom,
					  uint64_t val)
{
	odp_atomic_sub_u64(atom, val);
}

#ifdef ODP_ENABLE_CAS64
static inline int odp_atomic_cas_acq_u64(odp_atomic_u64_t *atom,
					 uint64_t *old_val, uint64_t new_val)
{
	return odp_atomic_cas_u64(atom, old_val, new_val);
}

static inline int odp_atomic_cas_rel_u64(odp_atomic_u64_t *atom,
					 uint64_t *old_val, uint64_t new_val)
{
	return odp_atomic_cas_u64(atom, old_val, new_val);
}

static inline int odp_atomic_cas_acq_rel_u64(odp_atomic_u64_t *atom,
					     uint64_t *old_val,
					     uint64_t new_val)
{
	return odp_atomic_cas_u64(atom, old_val, new_val);
}
#endif

/**
 * @}
 */

#include <odp/api/spec/atomic.h>

#ifdef __cplusplus
}
#endif

#endif
