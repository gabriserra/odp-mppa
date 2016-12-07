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

#include <odp/align.h>
#include <HAL/hal/core/cache.h>

/**
 * @internal
 * Atomic 64-bit unsigned integer
 */
struct odp_atomic_u64_s {
	union {
		uint64_t v;
		uint64_t _type;
		uint64_t _u64;
	};
} ODP_ALIGNED(sizeof(uint64_t)); /* Enforce alignement! */;

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

#define CAS_PTR(ptr, new, cur) ((void*)(unsigned long)(__builtin_k1_acwsu((void *)(ptr),	\
									  (unsigned long)(new),	\
									  (unsigned long)(cur))))

#ifdef __cplusplus
}
#endif

#endif
