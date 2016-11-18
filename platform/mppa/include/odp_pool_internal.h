/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */


/**
 * @file
 *
 * ODP buffer pool - internal header
 */

#ifndef ODP_POOL_INTERNAL_H_
#define ODP_POOL_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/api/std_types.h>
#include <odp/api/align.h>
#include <odp_align_internal.h>
#include <odp/api/pool.h>
#include <odp_buffer_internal.h>
#include <odp_buffer_ring_internal.h>
#include <odp/api/hints.h>
#include <odp_config_internal.h>
#include <odp/api/debug.h>
#include <odp/api/shared_memory.h>
#include <odp/api/atomic.h>
#include <odp_atomic_internal.h>
#include <string.h>

/**
 * Buffer initialization routine prototype
 *
 * @note Routines of this type MAY be passed as part of the
 * _odp_buffer_pool_init_t structure to be called whenever a
 * buffer is allocated to initialize the user metadata
 * associated with that buffer.
 */
typedef void (_odp_buf_init_t)(odp_buffer_t buf, void *buf_init_arg);

/**
 * Buffer pool initialization parameters
 * Used to communicate buffer pool initialization options. Internal for now.
 */
typedef struct _odp_buffer_pool_init_t {
	size_t udata_size;         /**< Size of user metadata for each buffer */
	_odp_buf_init_t *buf_init; /**< Buffer initialization routine to use */
	void *buf_init_arg;        /**< Argument to be passed to buf_init() */
} _odp_buffer_pool_init_t;         /**< Type of buffer initialization struct */

/* Local cache for buffer alloc/free acceleration */
typedef struct local_cache_t {
	odp_buffer_hdr_t *buf_freelist;  /* The local cache */
	uint64_t bufallocs;              /* Local buffer alloc count */
	uint64_t buffrees;               /* Local buffer free count */
} local_cache_t;

/* Use ticketlock instead of spinlock */
#define POOL_USE_TICKETLOCK
#define POOL_HAS_LOCAL_CACHE 1
#define POOL_MULTI_MAX 16

#ifdef POOL_USE_TICKETLOCK
#include <odp/api/ticketlock.h>
#define POOL_LOCK(a)      odp_ticketlock_lock(a)
#define POOL_UNLOCK(a)    odp_ticketlock_unlock(a)
#define POOL_LOCK_INIT(a) odp_ticketlock_init(a)
#else
#include <odp/api/spinlock.h>
#define POOL_LOCK(a)      odp_spinlock_lock(a)
#define POOL_UNLOCK(a)    odp_spinlock_unlock(a)
#define POOL_LOCK_INIT(a) odp_spinlock_init(a)
#endif

struct pool_entry_s {
#ifdef POOL_USE_TICKETLOCK
	odp_ticketlock_t        lock ODP_ALIGNED_CACHE;
	odp_ticketlock_t        buf_lock;
	odp_ticketlock_t        blk_lock;
#else
	odp_spinlock_t          lock ODP_ALIGNED_CACHE;
	odp_spinlock_t          buf_lock;
	odp_spinlock_t          blk_lock;
#endif

	char                    name[ODP_POOL_NAME_LEN];
	odp_pool_param_t        params;
	uint32_t                udata_size;
	uint32_t                pool_id;
	odp_shm_t               pool_shm;
	union {
		uint32_t all;
		struct {
			uint32_t has_name:1;
			uint32_t user_supplied_shm:1;
			uint32_t unsegmented:1;
			uint32_t zeroized:1;
			uint32_t predefined:1;
		};
	} flags;
	uint32_t                quiesced;
	uint32_t                low_wm_assert;
	uint8_t                *pool_base_addr;
	uint8_t                *pool_mdata_addr;
	size_t                  pool_size;
	uint32_t                buf_align;
	uint32_t                buf_stride;
	odp_buffer_hdr_t       *buf_freelist;

	odp_buffer_ring_t       ring;

	void                   *blk_freelist;
	odp_atomic_u32_t        blkcount;
	uint32_t                buf_num;
	uint32_t                seg_size;
	uint32_t                blk_size;
	uint32_t                high_wm;
	uint32_t                low_wm;
	uint32_t                headroom;
	uint32_t                tailroom;
};

typedef union pool_entry_u {
	struct pool_entry_s s;

	uint8_t pad[ODP_CACHE_LINE_SIZE_ROUNDUP(sizeof(struct pool_entry_s))];
} pool_entry_t;

typedef struct pool_table_t {
	pool_entry_t pool[ODP_CONFIG_POOLS];
} pool_table_t;


/* The pool table */
extern pool_table_t pool_tbl;

#if defined(ODP_CONFIG_SECURE_POOLS) && (ODP_CONFIG_SECURE_POOLS == 1)
#define buffer_is_secure(buf) (buf->flags.zeroized)
#define pool_is_secure(pool) (pool->flags.zeroized)
#else
#define buffer_is_secure(buf) 0
#define pool_is_secure(pool) 0
#endif

int get_buf_multi(struct pool_entry_s *pool, odp_buffer_hdr_t *buffers[],
		  unsigned n_buffers);

void ret_buf(struct pool_entry_s *pool, odp_buffer_hdr_t *buffers[],
	     const unsigned n_buffers);

static inline void *get_local_buf(local_cache_t *buf_cache,
				  struct pool_entry_s *pool ODP_UNUSED,
				  size_t totsize)
{
	odp_buffer_hdr_t *buf = buf_cache->buf_freelist;

	if (odp_likely(buf != NULL)) {
		buf_cache->buf_freelist = buf->next;

		if (odp_unlikely(buf->size < totsize)) {
			return NULL;
		}
	}

	return buf;
}

static inline void ret_local_buf(local_cache_t *buf_cache,
				 odp_buffer_hdr_t *head, odp_buffer_hdr_t *tail)
{
	tail->next = buf_cache->buf_freelist;
	buf_cache->buf_freelist = head;
}

static inline void flush_cache(local_cache_t *buf_cache,
			       struct pool_entry_s *pool)
{
	odp_buffer_hdr_t *buf = buf_cache->buf_freelist;
	uint32_t flush_count = 0;
	odp_buffer_hdr_t *bufs[POOL_MULTI_MAX];
	int n_bufs = 0;

	while (buf != NULL) {
		odp_buffer_hdr_t *next = buf->next;
		bufs[n_bufs++] = buf;
		if(n_bufs == POOL_MULTI_MAX) {
			ret_buf(pool, bufs, n_bufs);
			n_bufs = 0;
		}
		buf = next;
		flush_count++;
	}
	if(n_bufs)
		ret_buf(pool, bufs, n_bufs);

	buf_cache->buf_freelist = NULL;
}

static inline pool_entry_t *odp_pool_to_entry(odp_pool_t pool)
{
	return (pool_entry_t *)pool;
}

static inline pool_entry_t *odp_buf_to_pool(odp_buffer_hdr_t *buf)
{
	return odp_pool_to_entry(buf->pool_hdl);
}

static inline uint32_t odp_buffer_pool_segment_size(odp_pool_t pool)
{
	return odp_pool_to_entry(pool)->s.seg_size;
}

static inline uint32_t odp_buffer_pool_headroom(odp_pool_t pool)
{
	return odp_pool_to_entry(pool)->s.headroom;
}

static inline uint32_t odp_buffer_pool_tailroom(odp_pool_t pool)
{
	return odp_pool_to_entry(pool)->s.tailroom;
}

static inline int pool_to_id(odp_pool_t pool)
{
	pool_entry_t * entry = odp_pool_to_entry(pool);
	return entry - pool_tbl.pool;
}

static inline odp_pool_t pool_handle(pool_entry_t *pool)
{
	return (odp_pool_t)pool;
}
#ifdef __cplusplus
}
#endif

#endif
