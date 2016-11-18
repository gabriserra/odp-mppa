/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP atomic types and operations, semantically a subset of C11 atomics.
 * Reuse the 32-bit and 64-bit type definitions from odp_atomic.h. Introduces
 * new atomic pointer and flag types.
 * Atomic functions must be used to operate on atomic variables!
 */

#ifndef ODP_ATOMIC_INTERNAL_H_
#define ODP_ATOMIC_INTERNAL_H_

#include <odp/api/std_types.h>
#include <odp/api/align.h>
#include <odp/api/hints.h>
#include <odp/api/atomic.h>
#include <HAL/hal/core/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Atomic flag (boolean) type
 * @Note this is not the same as a plain boolean type.
 * _odp_atomic_flag_t is guaranteed to be able to operate on atomically.
 */
typedef uint64_t _odp_atomic_flag_t;

/**
 * Memory orderings supported by ODP.
 */
typedef enum {
/** Relaxed memory ordering, no ordering of other accesses enforced.
 * Atomic operations with relaxed memory ordering cannot be used for
 * synchronization */
	_ODP_MEMMODEL_RLX = __ATOMIC_RELAXED,
/** Acquire memory ordering, synchronize with release stores from another
 * thread (later accesses cannot move before acquire operation).
 * Use acquire and release memory ordering for Release Consistency */
	_ODP_MEMMODEL_ACQ = __ATOMIC_ACQUIRE,
/** Release memory ordering, synchronize with acquire loads from another
 * thread (earlier accesses cannot move after release operation).
 * Use acquire and release memory ordering for Release Consistency */
	_ODP_MEMMODEL_RLS = __ATOMIC_RELEASE,
/** Acquire&release memory ordering, synchronize with acquire loads and release
 * stores in another (one other) thread */
	_ODP_MEMMODEL_ACQ_RLS = __ATOMIC_ACQ_REL,
/** Sequential consistent memory ordering, synchronize with acquire loads and
 * release stores in all threads */
	_ODP_MEMMODEL_SC = __ATOMIC_SEQ_CST
} _odp_memmodel_t;

/**
 * Insert a full memory barrier (fence) in the compiler and instruction
 * sequence.
 */
#define _ODP_FULL_BARRIER() __k1_mb()


/*****************************************************************************
 * Operations on flag atomics
 * _odp_atomic_flag_init - no return value
 * _odp_atomic_flag_load - return current value
 * _odp_atomic_flag_tas - return old value
 * _odp_atomic_flag_clear - no return value
 *
 * Flag atomics use Release Consistency memory consistency model, acquire
 * semantics for TAS and release semantics for clear.
 *****************************************************************************/

/**
 * Initialize a flag atomic variable
 *
 * @param[out] flag Pointer to a flag atomic variable
 * @param val The initial value of the variable
 */
static inline void _odp_atomic_flag_init(_odp_atomic_flag_t *flag,
		odp_bool_t val)
{
	__builtin_k1_wpurge();
	__builtin_k1_swu(flag, val);
	__builtin_k1_fence();
}

/**
 * Load atomic flag variable
 * @Note Operation has relaxed semantics.
 *
 * @param flag Pointer to a flag atomic variable
 * @return The current value of the variable
 */
static inline int _odp_atomic_flag_load(_odp_atomic_flag_t *flag)
{
	return !__builtin_k1_lwu(flag);
}

/**
 * Test-and-set of atomic flag variable
 * @Note Operation has acquire semantics. It pairs with a later
 * release operation.
 *
 * @param[in,out] flag Pointer to a flag atomic variable
 *
 * @retval 1 if the flag was already true - lock not taken
 * @retval 0 if the flag was false and is now set to true - lock taken
 */
static inline int _odp_atomic_flag_tas(_odp_atomic_flag_t *flag)
{
	return !__k1_atomic_test_and_clear(flag);
}

/**
 * Clear atomic flag variable
 * The flag variable is cleared (set to false).
 * @Note Operation has release semantics. It pairs with an earlier
 * acquire operation or a later load operation.
 *
 * @param[out] flag Pointer to a flag atomic variable
 */
static inline void _odp_atomic_flag_clear(_odp_atomic_flag_t *flag)
{
	__builtin_k1_swu(flag, 0x1ULL);
}

/*****************************************************************************
 * Operations on 64-bit atomics
 * _odp_atomic_u64_load_mm - return current value
 * _odp_atomic_u64_store_mm - no return value
 * _odp_atomic_u64_xchg_mm - return old value
 * _odp_atomic_u64_cmp_xchg_strong_mm - return bool
 * _odp_atomic_u64_fetch_add_mm - return old value
 * _odp_atomic_u64_add_mm - no return value
 * _odp_atomic_u64_fetch_sub_mm - return old value
 * _odp_atomic_u64_sub_mm - no return value
 *****************************************************************************/

/**
 * Atomic load of 64-bit atomic variable
 *
 * @param atom Pointer to a 64-bit atomic variable
 * @param mmodel Memory order associated with the load operation
 *
 * @return Value of the variable
 */
static inline uint64_t _odp_atomic_u64_load_mm(odp_atomic_u64_t *atom,
		_odp_memmodel_t mmodel ODP_UNUSED)
{
	return odp_atomic_load_u64(atom);
}

/**
 * Atomic store to 64-bit atomic variable
 *
 * @param[out] atom Pointer to a 64-bit atomic variable
 * @param val  Value to write to the atomic variable
 * @param mmodel Memory order associated with the store operation
 */
static inline void _odp_atomic_u64_store_mm(odp_atomic_u64_t *atom,
		uint64_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)
{
	odp_atomic_store_u64(atom, val);
}

/**
 * Atomic fetch and add of 64-bit atomic variable
 *
 * @param[in,out] atom Pointer to a 64-bit atomic variable
 * @param val   Value to add to the atomic variable
 * @param mmodel Memory order associated with the add operation
 *
 * @return Value of the atomic variable before the addition
 */
static inline uint64_t _odp_atomic_u64_fetch_add_mm(odp_atomic_u64_t *atom,
		uint64_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)
{
	return odp_atomic_fetch_add_u64(atom, val);
}

/**
 * Atomic add of 64-bit atomic variable
 *
 * @param[in,out] atom Pointer to a 64-bit atomic variable
 * @param val   Value to add to the atomic variable
 * @param mmodel Memory order associated with the add operation.
 */
static inline void _odp_atomic_u64_add_mm(odp_atomic_u64_t *atom,
		uint64_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)

{
	odp_atomic_add_u64(atom, val);
}

/**
 * Atomic fetch and subtract of 64-bit atomic variable
 *
 * @param[in,out] atom Pointer to a 64-bit atomic variable
 * @param val   Value to subtract from the atomic variable
 * @param mmodel Memory order associated with the subtract operation
 *
 * @return Value of the atomic variable before the subtraction
 */
static inline uint64_t _odp_atomic_u64_fetch_sub_mm(odp_atomic_u64_t *atom,
		uint64_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)
{
	return odp_atomic_fetch_sub_u64(atom, val);
}

/**
 * Atomic subtract of 64-bit atomic variable
 *
 * @param[in,out] atom Pointer to a 64-bit atomic variable
 * @param val   Value to subtract from the atomic variable
 * @param mmodel Memory order associated with the subtract operation
 */
static inline void _odp_atomic_u64_sub_mm(odp_atomic_u64_t *atom,
		uint64_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)

{
	odp_atomic_sub_u64(atom, val);
}

#ifdef __cplusplus
}
#endif

#endif
