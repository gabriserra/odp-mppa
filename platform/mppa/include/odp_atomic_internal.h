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

#include <odp/std_types.h>
#include <odp/align.h>
#include <odp/hints.h>
#include <odp/atomic.h>
#include <HAL/hal/core/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pointer atomic type
 */
typedef odp_atomic_u32_t  _odp_atomic_ptr_t;

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
 * Operations on 32-bit atomics
 * _odp_atomic_u32_load_mm - return current value
 * _odp_atomic_u32_store_mm - no return value
 * _odp_atomic_u32_xchg_mm - return old value
 * _odp_atomic_u32_cmp_xchg_strong_mm - return bool
 * _odp_atomic_u32_fetch_add_mm - return old value
 * _odp_atomic_u32_add_mm - no return value
 * _odp_atomic_u32_fetch_sub_mm - return old value
 * _odp_atomic_u32_sub_mm - no return value
 *****************************************************************************/

/**
 * Atomic load of 32-bit atomic variable
 *
 * @param atom Pointer to a 32-bit atomic variable
 * @param mmodel Memory ordering associated with the load operation
 *
 * @return Value of the variable
 */
static inline uint32_t _odp_atomic_u32_load_mm(odp_atomic_u32_t *atom,
		_odp_memmodel_t mmodel ODP_UNUSED)
{
	return odp_atomic_load_u32(atom);
}

/**
 * Atomic store to 32-bit atomic variable
 *
 * @param[out] atom Pointer to a 32-bit atomic variable
 * @param val    Value to store in the atomic variable
 * @param mmodel Memory order associated with the store operation
 */
static inline void _odp_atomic_u32_store_mm(odp_atomic_u32_t *atom,
		uint32_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)
{
	odp_atomic_store_u32(atom, val);
}

/**
 * Atomic exchange (swap) of 32-bit atomic variable
 *
 * @param[in,out] atom Pointer to a 32-bit atomic variable
 * @param val    New value to store in the atomic variable
 * @param mmodel Memory order associated with the exchange operation
 *
 * @return Old value of the variable
 */
static inline uint32_t _odp_atomic_u32_xchg_mm(odp_atomic_u32_t *atom,
		uint32_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)

{
	uint32_t old_val = LOAD_U32(atom->v);
	do {
		__k1_uint64_t tmp = 0;
		tmp = __builtin_k1_acwsu((void *)&atom->v, val, old_val );

		uint32_t ret_val = tmp & 0xFFFFFFFF;
		if (ret_val == old_val)
			return 1;

		old_val = ret_val;
	} while(1);
}

/**
 * Atomic compare and exchange (swap) of 32-bit atomic variable
 * "Strong" semantics, will not fail spuriously.
 *
 * @param[in,out] atom Pointer to a 32-bit atomic variable
 * @param[in,out] exp Pointer to expected value (updated on failure)
 * @param val   New value to write
 * @param success Memory order associated with a successful compare-and-swap
 * operation
 * @param failure Memory order associated with a failed compare-and-swap
 * operation
 *
 * @retval 1 exchange successul
 * @retval 0 exchange failed and '*exp' updated with current value
 */
static inline int _odp_atomic_u32_cmp_xchg_strong_mm(
		odp_atomic_u32_t *atom,
		uint32_t *exp,
		uint32_t val,
		_odp_memmodel_t success ODP_UNUSED,
		_odp_memmodel_t failure ODP_UNUSED)
{
	__k1_uint64_t tmp = 0;
	tmp = __builtin_k1_acwsu((void *)&atom->v, val, *exp );
	if((tmp & 0xFFFFFFFF) == *exp){
		return 1;
	}else{
		*exp = (tmp & 0xFFFFFFFF);
		return 0;
	}
}

/**
 * Atomic fetch and add of 32-bit atomic variable
 *
 * @param[in,out] atom Pointer to a 32-bit atomic variable
 * @param val Value to add to the atomic variable
 * @param mmodel Memory order associated with the add operation
 *
 * @return Value of the atomic variable before the addition
 */
static inline uint32_t _odp_atomic_u32_fetch_add_mm(odp_atomic_u32_t *atom,
		uint32_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)
{
	return odp_atomic_fetch_add_u32(atom, val);
}

/**
 * Atomic add of 32-bit atomic variable
 *
 * @param[in,out] atom Pointer to a 32-bit atomic variable
 * @param val Value to add to the atomic variable
 * @param mmodel Memory order associated with the add operation
 */
static inline void _odp_atomic_u32_add_mm(odp_atomic_u32_t *atom,
		uint32_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)

{
	odp_atomic_add_u32(atom, val);
}

/**
 * Atomic fetch and subtract of 32-bit atomic variable
 *
 * @param[in,out] atom Pointer to a 32-bit atomic variable
 * @param val Value to subtract from the atomic variable
 * @param mmodel Memory order associated with the subtract operation
 *
 * @return Value of the atomic variable before the subtraction
 */
static inline uint32_t _odp_atomic_u32_fetch_sub_mm(odp_atomic_u32_t *atom,
		uint32_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)
{
	return odp_atomic_fetch_sub_u32(atom, val);
}

/**
 * Atomic subtract of 32-bit atomic variable
 *
 * @param[in,out] atom Pointer to a 32-bit atomic variable
 * @param val Value to subtract from the atomic variable
 * @param mmodel Memory order associated with the subtract operation
 */
static inline void _odp_atomic_u32_sub_mm(odp_atomic_u32_t *atom,
		uint32_t val,
		_odp_memmodel_t mmodel ODP_UNUSED)

{
	return odp_atomic_sub_u32(atom, val);
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

/*****************************************************************************
 * Operations on pointer atomics
 * _odp_atomic_ptr_init - no return value
 * _odp_atomic_ptr_load - return current value
 * _odp_atomic_ptr_store - no return value
 * _odp_atomic_ptr_xchg - return old value
 *****************************************************************************/

/**
 * Initialization of pointer atomic variable
 *
 * @param[out] atom Pointer to a pointer atomic variable
 * @param val   Value to initialize the variable with
 */
static inline void _odp_atomic_ptr_init(_odp_atomic_ptr_t *atom, void *val)
{
	_odp_atomic_u32_store_mm((odp_atomic_u32_t*)atom, (uint32_t)val, __ATOMIC_RELAXED);
}

/**
 * Atomic load of pointer atomic variable
 *
 * @param atom Pointer to a pointer atomic variable
 * @param mmodel Memory order associated with the load operation
 *
 * @return Value of the variable
 */
static inline void *_odp_atomic_ptr_load(const _odp_atomic_ptr_t *atom,
		_odp_memmodel_t mmodel)
{
	return (void*)_odp_atomic_u32_load_mm((odp_atomic_u32_t*)atom, mmodel);
}

/**
 * Atomic store to pointer atomic variable
 *
 * @param[out] atom Pointer to a pointer atomic variable
 * @param val  Value to write to the atomic variable
 * @param mmodel Memory order associated with the store operation
 */
static inline void _odp_atomic_ptr_store(_odp_atomic_ptr_t *atom,
		void *val,
		_odp_memmodel_t mmodel)
{
	_odp_atomic_u32_store_mm((odp_atomic_u32_t*)atom, (uint32_t)val, mmodel);
}

/**
 * Atomic exchange (swap) of pointer atomic variable
 *
 * @param[in,out] atom Pointer to a pointer atomic variable
 * @param val   New value to write
 * @param mmodel Memory order associated with the exchange operation
 *
 * @return Old value of variable
 */
static inline void *_odp_atomic_ptr_xchg(_odp_atomic_ptr_t *atom,
		void *val,
		_odp_memmodel_t mmodel)
{
	return (void*)_odp_atomic_u32_xchg_mm((odp_atomic_u32_t*)atom, (uint32_t)val, mmodel);
}

/**
 * Atomic compare and exchange (swap) of pointer atomic variable
 * "Strong" semantics, will not fail spuriously.
 *
 * @param[in,out] atom Pointer to a pointer atomic variable
 * @param[in,out] exp Pointer to expected value (updated on failure)
 * @param val   New value to write
 * @param success Memory order associated with a successful compare-and-swap
 * operation
 * @param failure Memory order associated with a failed compare-and-swap
 * operation
 *
 * @retval 1 exchange successful
 * @retval 0 exchange failed and '*exp' updated with current value
 */
static inline int _odp_atomic_ptr_cmp_xchg_strong(
		_odp_atomic_ptr_t *atom,
		void **exp,
		void *val,
		_odp_memmodel_t success,
		_odp_memmodel_t failure)
{
	return _odp_atomic_u32_cmp_xchg_strong_mm((odp_atomic_u32_t*)atom, (uint32_t*) exp, (uint32_t)val, success, failure);
}

#ifdef __cplusplus
}
#endif

#endif
