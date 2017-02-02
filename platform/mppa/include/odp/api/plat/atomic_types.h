/* Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */


/**
 * @file
 *
 * ODP atomic operations
 */

#ifndef ODP_ATOMIC_TYPES_H_
#define ODP_ATOMIC_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/api/align.h>
#include <HAL/hal/core/cache.h>

/**
 * @internal
 * Atomic 32-bit unsigned integer
 */
struct odp_atomic_u32_s {
	union {
		struct {
			uint32_t v; /**< Actual storage for the atomic variable */
			uint32_t _dummy; /**< Dummy field for force struct to 64b */
		};
		uint32_t _type;
		uint64_t _u64;
	};
} ODP_ALIGNED(sizeof(uint32_t)); /* Enforce alignement! */;

/**
 * @internal
 * Atomic 64-bit unsigned integer
 */
struct odp_atomic_u64_s {
	union {
#ifdef ODP_ENABLE_CAS64
		uint64_t v;
#else
		struct {
			uint64_t v: 63;
			uint8_t lock : 1;
		};
#endif
		uint64_t _type;
		uint64_t _u64;
	};
#ifdef ODP_ENABLE_CAS64
	struct odp_atomic_u32_s lock;
#endif
} ODP_ALIGNED(sizeof(uint64_t)); /* Enforce alignement! */;

/**
 * @internal
 * Helper macro for lock-based atomic operations on 64-bit integers
 * @param[in,out] atom Pointer to the 64-bit atomic variable
 * @param expr Expression used update the variable.
 * @return The old value of the variable.
 */
#ifdef ODP_ENABLE_CAS64
#define ATOMIC_OP(atom, expr) \
	({										\
 		uint64_t _old_val;							\
		uint32_t _lock_zero = 0;						\
		/* Loop while lock is already taken,					\
		 * stop when lock becomes clear */					\
		while (!odp_atomic_cas_u32(&(atom)->lock, &_lock_zero, 1))		\
			_lock_zero = 0;							\
		_old_val = LOAD_U64((atom)->v);						\
		STORE_U64(atom->_u64, (expr)); /* Perform whatever update is desired */	\
		odp_atomic_store_u32(&(atom)->lock, 0);					\
 		_old_val; /* Return old value */					\
 	})
#else
#define ATOMIC_OP(atom, expr) \
	({										\
		odp_atomic_u64_t _tmp;							\
		uint64_t _old_val;							\
		/* Loop while lock is already taken,					\
		 * stop when lock becomes clear */					\
		do {									\
			_tmp._u64 = __builtin_k1_ldc(&atom->_u64);			\
		} while(_tmp.lock == 0);						\
		_old_val = _tmp.v;							\
		_tmp.v = (expr); /* Perform whatever update is desired */		\
		STORE_U64(atom->_u64, _tmp._u64);					\
		_old_val; /* Return old value */					\
	})
#endif

typedef struct odp_atomic_u64_s odp_atomic_u64_t;

typedef struct odp_atomic_u32_s odp_atomic_u32_t;

#define INVALIDATE_AREA(p, s) do {	__k1_dcache_invalidate_mem_area((__k1_uintptr_t)(void*)p, s);	\
	}while(0)

#define INVALIDATE(p) INVALIDATE_AREA((p), sizeof(*p))

#define LOAD_U32(p) ((uint32_t)__builtin_k1_lwu((void*)(&p)))
#define STORE_U32(p, val) __builtin_k1_swu((void*)&(p), (uint32_t)(val))
#define STORE_U32_IMM(p, val) __builtin_k1_swu((void*)(p), (uint32_t)(val))

#define LOAD_S32(p) ((int32_t)__builtin_k1_lwu((void*)(&p)))
#define STORE_S32(p, val) __builtin_k1_swu((void*)&(p), (int32_t)(val))
#define STORE_S32_IMM(p, val) __builtin_k1_swu((void*)(p), (int32_t)(val))

#define LOAD_U64(p) ((uint64_t)__builtin_k1_ldu((void*)(&p)))
#define STORE_U64(p, val) __builtin_k1_sdu((void*)&(p), (uint64_t)(val))
#define STORE_U64_IMM(p, val) __builtin_k1_sdu((void*)(p), (uint64_t)(val))

#define LOAD_S64(p) ((int64_t)__builtin_k1_ldu((void*)(&p)))
#define STORE_S64(p, val) __builtin_k1_sdu((void*)&(p), (int64_t)(val))
#define STORE_S64_IMM(p, val) __builtin_k1_sdu((void*)(p), (int64_t)(val))

#define LOAD_PTR(p) ((void*)(unsigned long)(LOAD_U32(p)))
#define STORE_PTR(p, val) STORE_U32((p), (unsigned long)(val))
#define STORE_PTR_IMM(p, val) STORE_U32_IMM((p), (unsigned long)(val))

#define UNCACHED_ADDR_BIT 21
#define UNCACHED_ADDR_MASK (1UL << UNCACHED_ADDR_BIT)
#define CACHED_ADDR_MASK (~(1UL << UNCACHED_ADDR_BIT))
#define CACHED_TO_UNCACHED(p) ((typeof(p))\
			       (((unsigned long)(p)) | UNCACHED_ADDR_MASK))
#define UNCACHED_TO_CACHED(p) ((typeof(p))\
			       (((unsigned long)(p)) & CACHED_ADDR_MASK))

#define UNCACHED_OP32(p, x, expr)					\
	({								\
		uint32_t x = LOAD_U32(p);				\
		x = (expr);						\
		STORE_U32((p), x);					\
		x;							\
	})
#define UNCACHED_OP64(p, x, expr)					\
	({								\
		uint64_t x = LOAD_U64(p);				\
		x = (expr);						\
		STORE_U64((p), x);					\
		x;							\
	})

#define CAS_PTR(ptr, new, cur) ((void*)(unsigned long)(__builtin_k1_acwsu((void *)(ptr),	\
									  (unsigned long)(new),	\
									  (unsigned long)(cur))))

#ifdef __cplusplus
}
#endif

#endif
