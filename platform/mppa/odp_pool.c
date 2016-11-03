/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/std_types.h>
#include <odp/pool.h>
#include <odp_buffer_internal.h>
#include <odp_pool_internal.h>
#include <odp_buffer_inlines.h>
#include <odp_packet_internal.h>
#include <odp_timer_internal.h>
#include <odp_align_internal.h>
#include <odp/shared_memory.h>
#include <odp/align.h>
#include <odp_internal.h>
#include <odp/config.h>
#include <odp/hints.h>
#include <odp_debug_internal.h>
#include <odp_atomic_internal.h>
#include <odp_packet_internal.h>

#include <string.h>
#include <stdlib.h>


#if ODP_CONFIG_POOLS > ODP_BUFFER_MAX_POOLS
#error ODP_CONFIG_POOLS > ODP_BUFFER_MAX_POOLS
#endif


typedef union buffer_type_any_u {
	odp_buffer_hdr_t  buf;
	odp_packet_hdr_t  pkt;
	odp_timeout_hdr_t tmo;
} odp_anybuf_t;

/* Any buffer type header */
typedef struct {
	union buffer_type_any_u any_hdr;    /* any buffer type */
} odp_any_buffer_hdr_t;

typedef struct odp_any_hdr_stride {
	uint8_t pad[ODP_CACHE_LINE_SIZE_ROUNDUP(sizeof(odp_any_buffer_hdr_t))];
} odp_any_hdr_stride;

/* The pool table */
pool_table_t pool_tbl;

/* Local cache for buffer alloc/free acceleration */
static __thread local_cache_t local_cache[POOL_HAS_LOCAL_CACHE * ODP_CONFIG_POOLS];

static inline void *get_pool_entry(uint32_t pool_id)
{
	return &pool_tbl.pool[pool_id];
}

int odp_pool_init_global(void)
{
	uint32_t i;

	memset(&pool_tbl, 0, sizeof(pool_table_t));

	for (i = 0; i < ODP_CONFIG_POOLS; i++) {
		/* init locks */
		pool_entry_t *pool = &pool_tbl.pool[i];
		POOL_LOCK_INIT(&pool->s.lock);
		POOL_LOCK_INIT(&pool->s.buf_lock);
		POOL_LOCK_INIT(&pool->s.blk_lock);
		pool->s.pool_id = i;
		odp_atomic_init_u32(&pool->s.blkcount, 0);
	}

	ODP_DBG("\nPool init global\n");
	ODP_DBG("  pool_entry_s size     %zu\n", sizeof(struct pool_entry_s));
	ODP_DBG("  pool_entry_t size     %zu\n", sizeof(pool_entry_t));
	ODP_DBG("  odp_buffer_hdr_t size %zu\n", sizeof(odp_buffer_hdr_t));
	ODP_DBG("\n");
	return 0;
}

int odp_pool_term_global(void)
{
	int i;
	pool_entry_t *pool;
	int rc = 0;

	for (i = 0; i < ODP_CONFIG_POOLS; i++) {
		pool = get_pool_entry(i);

		POOL_LOCK(&pool->s.lock);
		if (pool->s.pool_shm != ODP_SHM_INVALID) {
			ODP_ERR("Not destroyed pool: %s\n", pool->s.name);
			rc = -1;
		}
		POOL_UNLOCK(&pool->s.lock);
	}

	return rc;
}

int odp_pool_term_local(void)
{
	_odp_flush_caches();
	return 0;
}

/**
 * Pool creation
 */

odp_pool_t odp_pool_create(const char *name, odp_pool_param_t *params)
{
	odp_pool_t pool_hdl = ODP_POOL_INVALID;
	pool_entry_t *pool;
	uint32_t i, headroom = 0, tailroom = 0;
	odp_shm_t shm;

	if (params == NULL)
		return ODP_POOL_INVALID;

	/* Default size and align for timeouts */
	if (params->type == ODP_POOL_TIMEOUT) {
		params->buf.size  = 0; /* tmo.__res1 */
		params->buf.align = 0; /* tmo.__res2 */
	}

	/* Default initialization parameters */
	uint32_t p_udata_size = 0;
	uint32_t udata_stride = 0;

	/* Restriction for v1.0: All non-packet buffers are unsegmented */
	int unseg = 1;

	/* Restriction for v1.0: No zeroization support */
	const int zeroized = 0;

	uint32_t blk_size, buf_stride, buf_num, seg_len = 0;
	uint32_t buf_align =
		params->type == ODP_POOL_BUFFER ? params->buf.align : 0;

	/* Validate requested buffer alignment */
	if (buf_align > ODP_CONFIG_BUFFER_ALIGN_MAX ||
	    buf_align != ODP_ALIGN_ROUNDDOWN_POWER_2(buf_align, buf_align))
		return ODP_POOL_INVALID;

	/* Set correct alignment based on input request */
	if (buf_align == 0)
		buf_align = ODP_CACHE_LINE_SIZE;
	else if (buf_align < ODP_CONFIG_BUFFER_ALIGN_MIN)
		buf_align = ODP_CONFIG_BUFFER_ALIGN_MIN;

	/* Calculate space needed for buffer blocks and metadata */
	switch (params->type) {
	case ODP_POOL_BUFFER:
		buf_num  = params->buf.num;
		blk_size = params->buf.size;

		/* Optimize small raw buffers */
		blk_size = ODP_ALIGN_ROUNDUP(blk_size, buf_align);

		buf_stride = sizeof(odp_buffer_hdr_stride);
		break;

	case ODP_POOL_PACKET:
		unseg = 0; /* Packets are always segmented */
		headroom = ODP_CONFIG_PACKET_HEADROOM;
		tailroom = ODP_CONFIG_PACKET_TAILROOM;
		buf_num = params->pkt.num;

		seg_len = params->pkt.seg_len + headroom + tailroom;
		seg_len = seg_len <= ODP_CONFIG_PACKET_SEG_LEN_MIN ?
			ODP_CONFIG_PACKET_SEG_LEN_MIN :
			(seg_len <= ODP_CONFIG_PACKET_SEG_LEN_MAX ?
			 seg_len : ODP_CONFIG_PACKET_SEG_LEN_MAX);

		seg_len = ODP_ALIGN_ROUNDUP(seg_len,
					    ODP_CONFIG_BUFFER_ALIGN_MIN);

		blk_size = params->pkt.len <= seg_len ? seg_len :
			ODP_ALIGN_ROUNDUP(params->pkt.len + headroom + tailroom, seg_len);

		/* Reject create if pkt.len needs too many segments */
		if (blk_size / seg_len > ODP_BUFFER_MAX_SEG) {
			if(params->pkt.seg_len != 0) {
				return ODP_POOL_INVALID;
			} else {
				seg_len = (blk_size + ODP_BUFFER_MAX_SEG - 1) /
					ODP_BUFFER_MAX_SEG;
				seg_len = ODP_ALIGN_ROUNDUP(seg_len,
							    ODP_CONFIG_BUFFER_ALIGN_MIN);
			}
		}

		p_udata_size = params->pkt.uarea_size;
		udata_stride = ODP_ALIGN_ROUNDUP(p_udata_size,
						 sizeof(uint64_t));

		buf_stride = sizeof(odp_packet_hdr_stride);
		break;

	case ODP_POOL_TIMEOUT:
		blk_size = 0;
		buf_num = params->tmo.num;
		buf_stride = sizeof(odp_timeout_hdr_stride);
		break;

	default:
		return ODP_POOL_INVALID;
	}

	/* Validate requested number of buffers against addressable limits */
	if (buf_num >
	    (ODP_BUFFER_MAX_BUFFERS / (buf_stride / ODP_CACHE_LINE_SIZE)))
		return ODP_POOL_INVALID;

	/* Find an unused buffer pool slot and iniitalize it as requested */
	for (i = 0; i < ODP_CONFIG_POOLS; i++) {
		pool = get_pool_entry(i);

		POOL_LOCK(&pool->s.lock);
		if (pool->s.pool_shm != ODP_SHM_INVALID) {
			POOL_UNLOCK(&pool->s.lock);
			continue;
		}

		/* found free pool */
		size_t block_size, pad_size, mdata_size, udata_size,
			ring_size, ring_pad_size;

		pool->s.flags.all = 0;

		if (name == NULL) {
			pool->s.name[0] = 0;
		} else {
			strncpy(pool->s.name, name,
				ODP_POOL_NAME_LEN - 1);
			pool->s.name[ODP_POOL_NAME_LEN - 1] = 0;
			pool->s.flags.has_name = 1;
		}

		pool->s.params = *params;
		pool->s.buf_align = buf_align;

		/* Optimize for short buffers: Data stored in buffer hdr */
		block_size = buf_num * blk_size;
		pool->s.buf_align = buf_align;

		pad_size = ODP_CACHE_LINE_SIZE_ROUNDUP(block_size) - block_size;
		mdata_size = buf_num * buf_stride;
		udata_size = buf_num * udata_stride;
		ring_size = (buf_num + 1) * sizeof(void*) ;
		ring_pad_size = ODP_CACHE_LINE_SIZE_ROUNDUP(ring_size) - ring_size;
		pool->s.buf_num   = buf_num;
		pool->s.pool_size = ODP_PAGE_SIZE_ROUNDUP(ring_size +
							  ring_pad_size +
							  block_size +
							  pad_size +
							  mdata_size +
							  udata_size);

		shm = odp_shm_reserve(pool->s.name,
				      pool->s.pool_size,
				      ODP_PAGE_SIZE, 0);
		if (shm == ODP_SHM_INVALID) {
			POOL_UNLOCK(&pool->s.lock);
			return ODP_POOL_INVALID;
		}
		pool->s.pool_base_addr = odp_shm_addr(shm);
		pool->s.pool_shm = shm;

		/* Now safe to unlock since pool entry has been allocated */
		POOL_UNLOCK(&pool->s.lock);

		pool->s.flags.unsegmented = unseg;
		pool->s.flags.zeroized = zeroized;
		pool->s.seg_size = unseg ? blk_size : seg_len;
		pool->s.blk_size = blk_size;

		odp_buffer_hdr_t **ring_base_addr = (odp_buffer_hdr_t**)pool->s.pool_base_addr;
		uint8_t *block_base_addr = ((uint8_t*)ring_base_addr) + ring_size + ring_pad_size;
		uint8_t *mdata_base_addr =
			block_base_addr + block_size + pad_size;
		uint8_t *udata_base_addr = mdata_base_addr + mdata_size;

		/* Pool mdata addr is used for indexing buffer metadata */
		pool->s.pool_mdata_addr = mdata_base_addr;
		pool->s.udata_size = p_udata_size;

		pool->s.buf_stride = buf_stride;
		pool->s.blk_freelist = NULL;

		odp_buffer_ring_init(&pool->s.ring, ring_base_addr, buf_num + 1);

		/* Initialization will increment these to their target vals */
		odp_atomic_store_u32(&pool->s.blkcount, 0);

		/* Reset other pool globals to initial state */
		pool->s.low_wm_assert = 0;
		pool->s.quiesced = 0;
		pool->s.headroom = headroom;
		pool->s.tailroom = tailroom;

		uint8_t *buf = udata_base_addr - buf_stride;
		uint8_t *udat = udata_stride == 0 ? NULL :
			udata_base_addr + udata_size - udata_stride;
		uint8_t *blk =
			block_base_addr + block_size - pool->s.seg_size;

		/* Init buffer common header and add to pool buffer freelist */
		do {
			odp_buffer_hdr_t *tmp =
				(odp_buffer_hdr_t *)(void *)buf;

			/* Iniitalize buffer metadata */
			tmp->flags.all = 0;
			tmp->flags.zeroized = zeroized;
			tmp->size = pool->s.seg_size;
			odp_atomic_init_u32(&tmp->ref_count, 0);
			tmp->type = params->type;
			tmp->event_type = params->type;
			tmp->pool_hdl = (odp_pool_t)pool;
			tmp->uarea_addr = (void *)udat;

			/* Set 1st seg addr for zero-len buffers */
			tmp->addr = blk;

			/* Push buffer onto pool's freelist */
			ret_buf(&pool->s, &tmp, 1);
			buf  -= buf_stride;
			udat -= udata_stride;
			blk -= pool->s.seg_size;
		} while (buf >= mdata_base_addr && blk >= block_base_addr);


		/* Watermarks are hard-coded for now to control caching */
		pool->s.high_wm = buf_num / 2;
		pool->s.low_wm  = buf_num / 4;

		pool_hdl = (odp_pool_t)pool;
		__k1_wmb();
		break;
	}

	return pool_hdl;
}


odp_pool_t odp_pool_lookup(const char *name)
{
	uint32_t i;
	pool_entry_t *pool;

	for (i = 0; i < ODP_CONFIG_POOLS; i++) {
		pool = get_pool_entry(i);

		POOL_LOCK(&pool->s.lock);
		if (strcmp(name, pool->s.name) == 0) {
			/* found it */
			POOL_UNLOCK(&pool->s.lock);
			return (odp_pool_t)pool;
		}
		POOL_UNLOCK(&pool->s.lock);
	}

	return ODP_POOL_INVALID;
}

int odp_pool_info(odp_pool_t pool_hdl, odp_pool_info_t *info)
{
	pool_entry_t *pool = (pool_entry_t *)pool_hdl;

	if (pool == NULL || info == NULL)
		return -1;

	info->name = pool->s.name;
	info->params = pool->s.params;

	return 0;
}

int odp_pool_destroy(odp_pool_t pool_hdl)
{
	pool_entry_t *pool = (pool_entry_t *)pool_hdl;

	if (pool == NULL)
		return -1;

	POOL_LOCK(&pool->s.lock);

	/* Call fails if pool is not allocated or predefined*/
	if (pool->s.pool_shm == ODP_SHM_INVALID ||
	    pool->s.flags.predefined) {
		POOL_UNLOCK(&pool->s.lock);
		return -1;
	}

	/* Make sure local cache is empty */
	if(POOL_HAS_LOCAL_CACHE)
		flush_cache(&local_cache[pool->s.pool_id], &pool->s);

	/* Call fails if pool has allocated buffers */
	uint32_t bufcount = odp_buffer_ring_get_count(&pool->s.ring);
	if (bufcount < pool->s.buf_num) {
		POOL_UNLOCK(&pool->s.lock);
		return -1;
	}

	odp_shm_free(pool->s.pool_shm);
	pool->s.pool_shm = ODP_SHM_INVALID;
	POOL_UNLOCK(&pool->s.lock);

	return 0;
}

int buffer_alloc(odp_pool_t pool_hdl, size_t size,
		 odp_buffer_hdr_t *buf[], int num)
{
	pool_entry_t *pool = (pool_entry_t *)pool_hdl;
	uintmax_t totsize = pool->s.headroom + size + pool->s.tailroom;
	int count = 0;
	/* Reject oversized allocation requests */
	if ((pool->s.flags.unsegmented && totsize > pool->s.seg_size) ||
	    (!pool->s.flags.unsegmented &&
	     totsize > pool->s.seg_size * ODP_BUFFER_MAX_SEG))
		return -1;

	/* Try to satisfy request from the local cache */
	if(POOL_HAS_LOCAL_CACHE)
		for(; count < num; ++count) {
			buf[count] = get_local_buf(&local_cache[pool->s.pool_id],
						   &pool->s, totsize);
			if(buf[count] == NULL)
				break;
		}


	if (!count) {
		/* If cache is empty, satisfy request from the pool */
		count = get_buf_multi(&pool->s, buf, num);
	}

	for (int i = 0; i < count; ++i) {
		/* By default, buffers inherit their pool's zeroization setting */
		buf[i]->flags.zeroized = pool->s.flags.zeroized;

		/* By default, buffers are not associated with an ordered queue */
		buf[i]->origin_qe = NULL;
	}
	return count;
}

odp_buffer_t odp_buffer_alloc(odp_pool_t pool_hdl)
{
	odp_buffer_t buf = ODP_BUFFER_INVALID;
	buffer_alloc(pool_hdl, odp_pool_to_entry(pool_hdl)->s.params.buf.size,
		     (odp_buffer_hdr_t **)&buf, 1);

	return buf;
}


int odp_buffer_alloc_multi(odp_pool_t pool_hdl, odp_buffer_t buf[], int num)
{
	size_t buf_size = odp_pool_to_entry(pool_hdl)->s.params.buf.size;

	return buffer_alloc(pool_hdl, buf_size, (odp_buffer_hdr_t **)buf, num);
}

void odp_buffer_free(odp_buffer_t buf)
{
	odp_buffer_hdr_t *buf_hdr = odp_buf_to_hdr(buf);
	pool_entry_t *pool = odp_buf_to_pool(buf_hdr);

	if (!POOL_HAS_LOCAL_CACHE || odp_unlikely(LOAD_U32(pool->s.low_wm_assert)))
		ret_buf(&pool->s, &buf_hdr, 1);
	else
		ret_local_buf(&local_cache[pool->s.pool_id], buf_hdr, buf_hdr);
}

void odp_buffer_free_multi(const odp_buffer_t _buf_tbl[], int num)
{
	odp_buffer_hdr_t ** buf_tbl = (odp_buffer_hdr_t**)_buf_tbl;
	int offset = 0;
	while (offset < num) {
		odp_pool_t pool = buf_tbl[offset]->pool_hdl;
		int end;

		for (end = offset + 1; end < num &&
			     buf_tbl[end]->pool_hdl == pool; end++) {}

		ret_buf(&((pool_entry_t *)pool)->s,
			(odp_buffer_hdr_t **)&buf_tbl[offset], end - offset);
		offset = end;
	}
}

void _odp_flush_caches(void)
{
	int i;
	if (POOL_HAS_LOCAL_CACHE) {
		for (i = 0; i < ODP_CONFIG_POOLS; i++) {
			pool_entry_t *pool = get_pool_entry(i);
			flush_cache(&local_cache[i], &pool->s);
		}
	}
}

void odp_pool_print(odp_pool_t pool_hdl)
{
	pool_entry_t *pool = (pool_entry_t *)pool_hdl;

	uint32_t bufcount  = odp_buffer_ring_get_count(&pool->s.ring);
	uint32_t blkcount  = odp_atomic_load_u32(&pool->s.blkcount);

	ODP_DBG("Pool info\n");
	ODP_DBG("---------\n");
	ODP_DBG(" pool            %" PRIu64 "\n",
		odp_pool_to_u64((odp_pool_t)pool));
	ODP_DBG(" name            %s\n",
		pool->s.flags.has_name ? pool->s.name : "Unnamed Pool");
	ODP_DBG(" pool type       %s\n",
		pool->s.params.type == ODP_POOL_BUFFER ? "buffer" :
	       (pool->s.params.type == ODP_POOL_PACKET ? "packet" :
	       (pool->s.params.type == ODP_POOL_TIMEOUT ? "timeout" :
		"unknown")));
	ODP_DBG(" pool storage    ODP managed shm handle %" PRIu64 "\n",
		odp_shm_to_u64(pool->s.pool_shm));
	ODP_DBG(" pool status     %s\n",
		pool->s.quiesced ? "quiesced" : "active");
	ODP_DBG(" pool opts       %s, %s, %s\n",
		pool->s.flags.unsegmented ? "unsegmented" : "segmented",
		pool->s.flags.zeroized ? "zeroized" : "non-zeroized",
		pool->s.flags.predefined  ? "predefined" : "created");
	ODP_DBG(" pool base       %p\n",  pool->s.pool_base_addr);
	ODP_DBG(" pool size       %zu (%zu pages)\n",
		pool->s.pool_size, pool->s.pool_size / ODP_PAGE_SIZE);
	ODP_DBG(" pool mdata base %p\n",  pool->s.pool_mdata_addr);
	ODP_DBG(" udata size      %zu\n", pool->s.udata_size);
	ODP_DBG(" headroom        %u\n",  pool->s.headroom);
	ODP_DBG(" tailroom        %u\n",  pool->s.tailroom);
	if (pool->s.params.type == ODP_POOL_BUFFER) {
		ODP_DBG(" buf size        %zu\n", pool->s.params.buf.size);
		ODP_DBG(" buf align       %u requested, %u used\n",
			pool->s.params.buf.align, pool->s.buf_align);
	} else if (pool->s.params.type == ODP_POOL_PACKET) {
		ODP_DBG(" seg length      %u requested, %u used\n",
			pool->s.params.pkt.seg_len, pool->s.seg_size);
		ODP_DBG(" pkt length      %u requested, %u used\n",
			pool->s.params.pkt.len, pool->s.blk_size);
	}
	ODP_DBG(" num bufs        %u\n",  pool->s.buf_num);
	ODP_DBG(" bufs available  %u %s\n", bufcount,
		pool->s.low_wm_assert ? " **low wm asserted**" : "");
	ODP_DBG(" bufs in use     %u\n",  pool->s.buf_num - bufcount);
	ODP_DBG(" blk size        %zu\n", pool->s.seg_size);
	ODP_DBG(" blks available  %u\n",  blkcount);
	ODP_DBG(" high wm value   %lu\n", pool->s.high_wm);
	ODP_DBG(" low wm value    %lu\n", pool->s.low_wm);
}


odp_pool_t odp_buffer_pool(odp_buffer_t buf)
{
	return odp_buf_to_hdr(buf)->pool_hdl;
}

void odp_pool_param_init(odp_pool_param_t *params)
{
	memset(params, 0, sizeof(odp_pool_param_t));
}


int get_buf_multi(struct pool_entry_s *pool, odp_buffer_hdr_t *buffers[],
		  unsigned n_buffers)
{
	unsigned n_bufs;
	uint32_t bufcount = 0;

	if(n_buffers > POOL_MULTI_MAX)
		n_buffers = POOL_MULTI_MAX;

	n_bufs = odp_buffer_ring_get_multi(&pool->ring, buffers, n_buffers, 0,
					   POOL_HAS_LOCAL_CACHE ?
					   &bufcount : NULL);
	/* Check for low watermark condition */
	if (POOL_HAS_LOCAL_CACHE &&
	    bufcount <= pool->low_wm &&
	    bufcount + n_buffers > pool->low_wm &&
	    !LOAD_U32(pool->low_wm_assert)) {
		STORE_U32(pool->low_wm_assert, 1);
	}

	return n_bufs;
}

void ret_buf(struct pool_entry_s *pool, odp_buffer_hdr_t *buffers[],
	     const unsigned n_buffers)
{
	if (pool->params.type == ODP_POOL_PACKET) {
		for (unsigned i = 0; i < n_buffers; ++i) {
			packet_init((pool_entry_t *)pool,
				    (odp_packet_hdr_t*)buffers[i],
				    pool->params.pkt.len, true);
		}
	}
	__builtin_k1_wpurge();

	uint32_t bufcount;
	odp_buffer_ring_push_multi(&pool->ring, buffers, n_buffers,
				   POOL_HAS_LOCAL_CACHE ? &bufcount : NULL);

	if(POOL_HAS_LOCAL_CACHE &&
	   bufcount >= pool->high_wm &&
	   bufcount - n_buffers < pool->high_wm &&
	   LOAD_U32(pool->low_wm_assert)) {
		STORE_U32(pool->low_wm_assert, 0);
	}
}
