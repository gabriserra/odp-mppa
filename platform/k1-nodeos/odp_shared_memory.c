/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/*ftruncate _POSIX_C_SOURCE 200809L */
#define _POSIX_C_SOURCE 200809L

#include <odp/shared_memory.h>
#include <odp_internal.h>
#include <odp/spinlock.h>
#include <odp/align.h>
#include <odp/system_info.h>
#include <odp/debug.h>
#include <odp_debug_internal.h>
#include <odp_align_internal.h>
#include <odp/config.h>

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>

_ODP_STATIC_ASSERT(ODP_CONFIG_SHM_BLOCKS >= ODP_CONFIG_POOLS,
		   "ODP_CONFIG_SHM_BLOCKS < ODP_CONFIG_POOLS");

typedef struct {
	char      name[ODP_SHM_NAME_LEN];
	uint64_t  size;
	uint64_t  align;
	uint64_t  alloc_size;
	void      *addr_orig;
	void      *addr;
	int       huge;
	odp_shm_t hdl;
	uint32_t  flags;
	int       fd;

} odp_shm_block_t;


typedef struct {
	odp_shm_block_t block[ODP_CONFIG_SHM_BLOCKS];
	odp_spinlock_t  lock;

} odp_shm_table_t;

/* Global shared memory table */
static odp_shm_table_t *odp_shm_tbl;


static inline uint32_t from_handle(odp_shm_t shm)
{
	return _odp_typeval(shm) - 1;
}


static inline odp_shm_t to_handle(uint32_t index)
{
	return _odp_cast_scalar(odp_shm_t, index + 1);
}


int odp_shm_init_global(void)
{
	void *addr;

#ifndef MAP_HUGETLB
	ODP_DBG("NOTE: mmap does not support huge pages\n");
#endif

	addr = malloc(sizeof(odp_shm_table_t));

	if (addr == NULL)
		return -1;

	odp_shm_tbl = addr;

	memset(odp_shm_tbl, 0, sizeof(odp_shm_table_t));
	odp_spinlock_init(&odp_shm_tbl->lock);

	return 0;
}

int odp_shm_term_global(void)
{
	free(odp_shm_tbl);

	return 0;
}


int odp_shm_init_local(void)
{
	return 0;
}


static int find_block(const char *name, uint32_t *index)
{
	uint32_t i;

	for (i = 0; i < ODP_CONFIG_SHM_BLOCKS; i++) {
		if (strcmp(name, odp_shm_tbl->block[i].name) == 0) {
			/* found it */
			if (index != NULL)
				*index = i;

			return 1;
		}
	}

	return 0;
}

int odp_shm_free(odp_shm_t shm)
{
	uint32_t i;
	odp_shm_block_t *block;

	if (shm == ODP_SHM_INVALID) {
		ODP_DBG("odp_shm_free: Invalid handle\n");
		return -1;
	}

	i = from_handle(shm);

	if (i >= ODP_CONFIG_SHM_BLOCKS) {
		ODP_DBG("odp_shm_free: Bad handle\n");
		return -1;
	}

	odp_spinlock_lock(&odp_shm_tbl->lock);

	block = &odp_shm_tbl->block[i];

	if (block->addr == NULL) {
		ODP_DBG("odp_shm_free: Free block\n");
		odp_spinlock_unlock(&odp_shm_tbl->lock);
		return 0;
	}

	free(block->addr_orig);

	if (block->flags & ODP_SHM_PROC) {
		/* FIXME */
		ODP_ABORT("ODP_SHM_PROC Unsupported");
		odp_spinlock_unlock(&odp_shm_tbl->lock);
		return -1;
	}
	memset(block, 0, sizeof(odp_shm_block_t));
	odp_spinlock_unlock(&odp_shm_tbl->lock);
	return 0;
}

odp_shm_t odp_shm_reserve(const char *name, uint64_t size, uint64_t align,
			  uint32_t flags)
{
	uint32_t i;
	odp_shm_block_t *block;
	void *addr;
	int fd = -1;
	uint64_t alloc_size;

	alloc_size = size + align;



	if (flags & ODP_SHM_PROC) {
		/* FIXME */
		ODP_DBG("ODP_SHM_PROC Unsupported");
		return ODP_SHM_INVALID;
	} 

	odp_spinlock_lock(&odp_shm_tbl->lock);

	if (find_block(name, NULL)) {
		/* Found a block with the same name */
		odp_spinlock_unlock(&odp_shm_tbl->lock);
		ODP_DBG("name \"%s\" already used.\n", name);
		return ODP_SHM_INVALID;
	}

	for (i = 0; i < ODP_CONFIG_SHM_BLOCKS; i++) {
		if (odp_shm_tbl->block[i].addr == NULL) {
			/* Found free block */
			break;
		}
	}

	if (i > ODP_CONFIG_SHM_BLOCKS - 1) {
		/* Table full */
		odp_spinlock_unlock(&odp_shm_tbl->lock);
		ODP_DBG("%s: no more blocks.\n", name);
		return ODP_SHM_INVALID;
	}

	block = &odp_shm_tbl->block[i];

	block->hdl  = to_handle(i);
	addr        = NULL;

	/* Use normal pages for small or failed huge page allocations */
	if (addr == NULL) {
		addr = malloc(alloc_size);

		if (addr == NULL) {
			odp_spinlock_unlock(&odp_shm_tbl->lock);
			ODP_DBG("%s malloc failed.\n", name);
			return ODP_SHM_INVALID;
		} else {
			block->alloc_size = alloc_size;
			block->huge = 0;
		}
	}

	block->addr_orig = addr;

	/* move to correct alignment */
	addr = ODP_ALIGN_ROUNDUP_PTR(addr, align);

	strncpy(block->name, name, ODP_SHM_NAME_LEN - 1);
	block->name[ODP_SHM_NAME_LEN - 1] = 0;
	block->size       = size;
	block->align      = align;
	block->flags      = flags;
	block->fd         = fd;
	block->addr       = addr;

	odp_spinlock_unlock(&odp_shm_tbl->lock);
	return block->hdl;
}

odp_shm_t odp_shm_lookup(const char *name)
{
	uint32_t i;
	odp_shm_t hdl;

	odp_spinlock_lock(&odp_shm_tbl->lock);

	if (find_block(name, &i) == 0) {
		odp_spinlock_unlock(&odp_shm_tbl->lock);
		return ODP_SHM_INVALID;
	}

	hdl = odp_shm_tbl->block[i].hdl;
	odp_spinlock_unlock(&odp_shm_tbl->lock);

	return hdl;
}


void *odp_shm_addr(odp_shm_t shm)
{
	uint32_t i;

	i = from_handle(shm);

	if (i > (ODP_CONFIG_SHM_BLOCKS - 1))
		return NULL;

	return odp_shm_tbl->block[i].addr;
}


int odp_shm_info(odp_shm_t shm, odp_shm_info_t *info)
{
	odp_shm_block_t *block;
	uint32_t i;

	i = from_handle(shm);

	if (i > (ODP_CONFIG_SHM_BLOCKS - 1))
		return -1;

	block = &odp_shm_tbl->block[i];

	info->name      = block->name;
	info->addr      = block->addr;
	info->size      = block->size;
	info->page_size = ODP_PAGE_SIZE;
	info->flags     = block->flags;

	return 0;
}


void odp_shm_print_all(void)
{
	int i;

	ODP_PRINT("\nShared memory\n");
	ODP_PRINT("--------------\n");
	ODP_PRINT("  page size:      %"PRIu64" kB\n",
		  odp_sys_page_size() / 1024);
	ODP_PRINT("  huge page size: %"PRIu64" kB\n",
		  odp_sys_huge_page_size() / 1024);
	ODP_PRINT("\n");

	ODP_PRINT("  id name                       kB align huge addr\n");

	for (i = 0; i < ODP_CONFIG_SHM_BLOCKS; i++) {
		odp_shm_block_t *block;

		block = &odp_shm_tbl->block[i];

		if (block->addr) {
			ODP_PRINT("  %2i %-24s %4"PRIu64"  %4"PRIu64
				  " %2c   %p\n",
				  i,
				  block->name,
				  block->size/1024,
				  block->align,
				  (block->huge ? '*' : ' '),
				  block->addr);
		}
	}

	ODP_PRINT("\n");
}
