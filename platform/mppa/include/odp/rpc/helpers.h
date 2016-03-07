#ifndef __ODP_RPC_HELPERS_H__
#define __ODP_RPC_HELPERS_H__

#include <HAL/hal/hal.h>

static inline int ODP_RPC_FUNCTION(get_cluster_id)(int local_if){
	int reg_cluster_id = __k1_get_cluster_id();
	int base_cluster_id = (reg_cluster_id / 64 * 64) + (reg_cluster_id % 32);

#ifdef K1B_EXPLORER
	local_if = local_if % 4;
#endif

	if (local_if >= 4)
		return base_cluster_id + 32 + local_if - 4;

	return base_cluster_id + local_if;
}

static inline int ODP_RPC_FUNCTION(densify_cluster_id)(unsigned cluster_id){
	if(cluster_id >= 128 && cluster_id <= 131)
		cluster_id = 16 + (cluster_id - 128);
	else if(cluster_id >= 192 && cluster_id <= 195)
		cluster_id = 20 + (cluster_id - 192);
	return cluster_id;
}

static inline int ODP_RPC_FUNCTION(undensify_cluster_id)(unsigned cluster_id){
	if(cluster_id >= 16 && cluster_id <= 19)
		cluster_id = 128 + (cluster_id - 16);
	else if(cluster_id >= 20 && cluster_id <= 23)
		cluster_id = 192 + (cluster_id - 20);
	return cluster_id;
}

static inline int ODP_RPC_FUNCTION(get_io_dma_id)(unsigned io_id, unsigned cluster_id){
	int dense_id = ODP_RPC_FUNCTION(densify_cluster_id)(cluster_id);
	int dma_offset = (dense_id / 4) % 4;
#if defined(K1B_EXPLORER)
	dma_offset = 0;
#endif

	switch(io_id){
	case 0:
		/* North */
		return 160 + dma_offset;
	case 1:
		/* South */
		return 224 + dma_offset;
	default:
		return -1;
	}
}

static inline int ODP_RPC_FUNCTION(get_io_tag_id)(unsigned cluster_id){
	int dense_id = ODP_RPC_FUNCTION(densify_cluster_id)(cluster_id);
	int tag_offset = (dense_id / 16) * 4 + dense_id % 4;

#if defined(K1B_EXPLORER)
	/* Only DMA4 available on explorer + eth530 */
	tag_offset = dense_id;
#endif
	return RPC_BASE_RX + tag_offset;
}

#endif /* __ODP_RPC_HELPERS_H__ */
