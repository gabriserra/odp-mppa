/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */


/**
 * @file
 *
 * ODP buffer descriptor - implementation internal
 */

#ifndef ODP_BUFFER_INTERNAL_H_
#define ODP_BUFFER_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/api/std_types.h>
#include <odp/api/atomic.h>
#include <odp/api/pool.h>
#include <odp/api/buffer.h>
#include <odp/api/debug.h>
#include <odp/api/align.h>
#include <odp_align_internal.h>
#include <odp/api/byteorder.h>
#include <odp/api/thread.h>
#include <odp/api/event.h>
#include <odp_forward_typedefs_internal.h>
#include <odp_schedule_if.h>


#define ODP_BITSIZE(x) \
	((x) <=     2 ?  1 : \
	((x) <=     4 ?  2 : \
	((x) <=     8 ?  3 : \
	((x) <=    16 ?  4 : \
	((x) <=    32 ?  5 : \
	((x) <=    64 ?  6 : \
	((x) <=   128 ?  7 : \
	((x) <=   256 ?  8 : \
	((x) <=   512 ?  9 : \
	((x) <=  1024 ? 10 : \
	((x) <=  2048 ? 11 : \
	((x) <=  4096 ? 12 : \
	((x) <=  8196 ? 13 : \
	((x) <= 16384 ? 14 : \
	((x) <= 32768 ? 15 : \
	((x) <= 65536 ? 16 : \
	 (0/0)))))))))))))))))

#define ODP_BUFFER_MAX_SEG 1

#define ODP_BUFFER_POOL_BITS   ODP_BITSIZE(ODP_CONFIG_POOLS)
#define ODP_BUFFER_SEG_BITS    ODP_BITSIZE(ODP_BUFFER_MAX_SEG)
#define ODP_BUFFER_INDEX_BITS  (32 - ODP_BUFFER_POOL_BITS - ODP_BUFFER_SEG_BITS)
#define ODP_BUFFER_PREFIX_BITS (ODP_BUFFER_POOL_BITS + ODP_BUFFER_INDEX_BITS)
#define ODP_BUFFER_MAX_POOLS   (1 << ODP_BUFFER_POOL_BITS)
#define ODP_BUFFER_MAX_BUFFERS (1 << ODP_BUFFER_INDEX_BITS)

#define ODP_BUFFER_MAX_INDEX     (ODP_BUFFER_MAX_BUFFERS - 2)
#define ODP_BUFFER_INVALID_INDEX (ODP_BUFFER_MAX_BUFFERS - 1)

/* Common buffer header */
struct odp_buffer_hdr_t {
	struct odp_buffer_hdr_t *next;       /* next buf in a list--keep 1st */
	union {
		uint16_t all;
		struct {
			uint16_t zeroized:1; /* Zeroize buf data on free */
			uint16_t hdrdata:1;  /* Data is in buffer hdr */
			uint16_t sustain:1;  /* Sustain order */
		};
	} flags;
	int8_t                   type;       /* buffer type */
	int8_t                   event_type; /* for reuse as event */
	uint16_t                 size;       /* max data size */
	void                    *uarea_addr; /* user area address */
	uint32_t                 uarea_size; /* size of user area */
	odp_pool_t               pool_hdl;   /* buffer pool handle */
	union {
		void            *buf_ctx;    /* user context */
		const void      *buf_cctx;   /* const alias for ctx */
	};
	void                    *addr;       /* block addrs */
	queue_entry_t           *origin_qe;  /* ordered queue origin */
	odp_atomic_u32_t         ref_count;  /* reference count */
	uint64_t                 order;      /* sequence for ordered queues */
	union {
		struct {
			queue_entry_t   *target_qe;  /* ordered queue target */
			struct odp_buffer_hdr_t *link;
		};
		uint64_t         sync[SCHEDULE_ORDERED_LOCKS_PER_QUEUE];
	};
};

typedef struct odp_buffer_hdr_stride {
	uint8_t pad[ODP_CACHE_LINE_SIZE_ROUNDUP(sizeof(odp_buffer_hdr_t))];
} odp_buffer_hdr_stride;

/* Raw buffer header */
typedef struct {
	odp_buffer_hdr_t buf_hdr;    /* common buffer header */
} odp_raw_buffer_hdr_t;

/* Free buffer marker */
#define ODP_FREEBUF -1

/* Forward declarations */
int buffer_alloc(odp_pool_t pool_hdl, size_t size,
		 odp_buffer_hdr_t *buf[], int num);

#ifdef __cplusplus
}
#endif

#endif
