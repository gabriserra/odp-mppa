/* Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/api/atomic.h>

int odp_atomic_lock_free_u64(odp_atomic_op_t *atomic_op)
{
#ifdef ODP_ENABLE_CAS64
	/* All operations have locks */
	if (atomic_op)
		atomic_op->all_bits = 0;

	return 0;
#else
	/* All operations are lock-free */
	if (atomic_op) {
		atomic_op->all_bits = ~((uint32_t)0);
		atomic_op->op.min = 0;
		atomic_op->op.max = 0;
		atomic_op->op.cas = 0;
		atomic_op->op.xchg = 0;
		atomic_op->op.init  = 0;
	}

	return 2;
#endif
}
