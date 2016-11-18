/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/api/init.h>
#include <odp_internal.h>
#include <odp/api/debug.h>
#include <odp_debug_internal.h>
#include <HAL/hal/core/mp.h>
#include <odp/rpc/api.h>
#include <odp_rx_internal.h>
#include <errno.h>
#include <mOS_scoreboard_c.h>

/**
 * WARNING: this file must be aligned with mOS one.
 * if mOS update its internal structures, this file must be updated too
 */

/**************************************************************************************************/
/************************* START : CUSTOM MMU SETUP ***********************************************/
/**************************************************************************************************/

#define TLB_INVAL_ENTRY_LOW  TLB_INVAL_ENTRY_LOW_K1B
#define TLB_INVAL_ENTRY_HIGH TLB_INVAL_ENTRY_HIGH_K1B
#define MOS_ETHERNET_TLB_ENTRY (0x0442000004400093ULL)

extern int TLB_INVAL_ENTRY_LOW  __attribute__((weak));
extern int TLB_INVAL_ENTRY_HIGH __attribute__((weak));

extern int _bin_size __attribute__((weak));
extern int _bin_start_frame;
extern int _bin_end_frame;

extern int _scoreboard_offset;
extern int _scb_mem_frames_array_offset;
extern int _vstart;

extern int MOS_RESERVED;
extern int BINARY_SIZE          __attribute__((weak));
extern int _LIBMPPA_DSM_CLIENT_PAGE_SIZE    __attribute__((weak));
extern int _MOS_SECURITY_LEVEL;

#define ADDR_0(A)           (A - 1)
#define ADDR_1(A)           (ADDR_0(A) | (ADDR_0(A) >> 1))
#define ADDR_2(A)           (ADDR_1(A) | (ADDR_1(A) >> 2))
#define ADDR_3(A)           (ADDR_2(A) | (ADDR_2(A) >> 4))
#define ADDR_4(A)           (ADDR_3(A) | (ADDR_3(A) >> 8))
#define ADDR_5(A)           (ADDR_4(A) | (ADDR_4(A) >> 16))
#define MEM_SIZE_ALIGN(A)   (ADDR_5(A) + 1)

extern int __MPPA_BURN_TX;
extern int __MPPA_BURN_FDIR;
// APPLICATION REQUIREMENTS

#ifdef __k1dp__
#undef MOS_ETHERNET_TLB_ENTRY
#define MOS_ETHERNET_TLB_ENTRY MOS_NULL_TLB_ENTRY
#endif

#define AF_ALG_BUF_PTR_PHYS 0xe0000
#define AF_ALG_BUF_PTR_VIRT 0x600000
#define AF_ALG_BUF_PTR_SIZE 0x100000

volatile mOS_bin_desc_t bin_descriptor __attribute__((section (".locked.binaries"))) = {
	.pe_pool            = (0x1 << ((BSP_NB_PE_P & ~(0x3)))) - 1,      // don't touch
	.smem_start_frame        = (int)&_bin_start_frame,                // don't touch
	.smem_end_frame          = (int)&_bin_end_frame,                  // don't touch

	.ddr_start_frame        = 0,                                      // don't touch
	.ddr_end_frame          = 0,                                      // don't touch

	.tlb_small_size         = 0x10000,    // 64K                       <---  YOU CAN TOUCH , must fit entries in jtlb
	.tlb_big_size           = 0x100000,   // 1M                        <---  YOU CAN TOUCH , must fit entries in jtlb
	.entry_point               = (uint32_t) & _vstart,
	.security_level     = (int) &_MOS_SECURITY_LEVEL,                 // don't touch
	.scoreboard_offset  = ( int ) &(_scoreboard_offset),              // don't touch
	.ltlb               = {
		{._dword        = MOS_NULL_TLB_ENTRY                                }, // <---- YOU CAN TOUCH
		{._dword        = MOS_NULL_TLB_ENTRY                                }, // <---- YOU CAN TOUCH
		{
			._dword     = (0x00000000000000dbULL) | (((uint64_t)MEM_SIZE_ALIGN(BSP_CLUSTER_INTERNAL_MEM_SIZE_P)) << 31) // <---- YOU CAN TOUCH, Here 0->2Mo RWX entry
		},
		{
			._dword     = (0x00000000000000dFULL) | (((uint64_t)MEM_SIZE_ALIGN(BSP_CLUSTER_INTERNAL_MEM_SIZE_P)) << 31) | (((uint64_t)MEM_SIZE_ALIGN(BSP_CLUSTER_INTERNAL_MEM_SIZE_P)) << 32)// <---- YOU CAN TOUCH, Here 2->4Mo RWX entry (uncached alias)
		},
		[ 4 ... (MOS_VC_NB_USER_LTLB - 1) ] = {
			._dword        = MOS_NULL_TLB_ENTRY                                // <---- YOU CAN TOUCH
		},
	},

	.jtlb               = {
		/* 64 x 64K entries, we map a two sections 0x600000+1M segment and  0x700000+1M at same physical address 0x50000 */

		[ 0 ... 127 ] = {
			._dword        = MOS_NULL_TLB_ENTRY                                // <---- YOU CAN TOUCH
		},	},

	.rx_pool    = { . interface [ 0 ... MOS_NB_DMA_MAX -1 ] = {  .array64_bit = { ~(0x0ULL),         ~(0x0ULL),~(0x0ULL), ~(0x0ULL)}}},
	.uc_pool    = { . interface [ 0 ... MOS_NB_DMA_MAX -1 ] = ~(0x0)},
	//.tx_pool    = { . interface [ 0 ... MOS_NB_DMA_MAX -1 ] = ((0x1 << MOS_NB_TX_CHANNELS)-1) & 0x3F}, /* All tx from 0 to 6 exclusted */
	.tx_pool    = { . interface [ 0 ... MOS_NB_DMA_MAX -1 ] = ((0x1 << MOS_NB_TX_CHANNELS)-1)}, /* All tx */
	.mb_pool    = { . interface [ 0 ... MOS_NB_DMA_MAX -1 ] = { .array64_bit = { ~(0x0ULL),         ~(0x0ULL)}}},
	.mb_tx_pool = { . interface [ 0 ... MOS_NB_DMA_MAX -1 ] = 0xF},
	.fdir_pool  = { . interface [ 0 ... MOS_NB_DMA_MAX -1 ] =  0x1F},  /* Only loopback dir */
	.burn_tx    = (int)&__MPPA_BURN_TX,             // don't touch
	.burn_fdir  = (int)&__MPPA_BURN_FDIR,             // don't touch
	.hook_rm    = 0             // don't touch
};

mOS_scoreboard_t scoreboard __attribute__((section (".locked.scoreboard")));

#define INSTANCE_ID 0xdeadbeef


enum init_stage {
	NO_INIT = 0,    /* No init stages completed */
	CPUMASK_INIT,
	TIME_INIT,
	SYSINFO_INIT,
	SHM_INIT,
	THREAD_INIT,
	POOL_INIT,
	QUEUE_INIT,
	SCHED_INIT,
	RPC_INIT,
	PKTIO_INIT,
	TIMER_INIT,
	CRYPTO_INIT,
	CLASSIFICATION_INIT,
	TRAFFIC_MNGR_INIT,
	NAME_TABLE_INIT,
	ALL_INIT      /* All init stages completed */
};

struct odp_global_data_s odp_global_data;


static int _odp_term_global(enum init_stage stage)
{
	int rc = 0;

	switch (stage) {
	case ALL_INIT:
	case NAME_TABLE_INIT:
	case TRAFFIC_MNGR_INIT:
	case CLASSIFICATION_INIT:
		if (odp_classification_term_global()) {
			ODP_ERR("ODP classificatio term failed.\n");
			rc = -1;
		}

	case CRYPTO_INIT:
#ifndef NO_CRYPTO
		if (odp_crypto_term_global()) {
			ODP_ERR("ODP crypto term failed.\n");
			rc = -1;
		}
#endif
	case TIMER_INIT:
	case PKTIO_INIT:
		if (odp_pktio_term_global()) {
			ODP_ERR("ODP pktio term failed.\n");
			rc = -1;
		}
	case RPC_INIT:
		if (mppa_rpc_odp_client_term()) {
			ODP_ERR("ODP RPC tem failed.\n");
			rc = -1;
		}
	case SCHED_INIT:
		if (sched_fn->term_global()) {
			ODP_ERR("ODP schedule term failed.\n");
			rc = -1;
		}
	case QUEUE_INIT:
		if (odp_queue_term_global()) {
			ODP_ERR("ODP queue term failed.\n");
			rc = -1;
		}
	case POOL_INIT:
		if (odp_pool_term_global()) {
			ODP_ERR("ODP buffer pool term failed.\n");
			rc = -1;
		}
	case THREAD_INIT:
		if (odp_thread_term_global()) {
			ODP_ERR("ODP thread term failed.\n");
			rc = -1;
		}
	case SHM_INIT:
		if (odp_shm_term_global()) {
			ODP_ERR("ODP shm term failed.\n");
			rc = -1;
		}
	case SYSINFO_INIT:
	case TIME_INIT:
	case CPUMASK_INIT:
	case NO_INIT:
		;
	}

	return rc;
}

int odp_init_global(odp_instance_t *instance,
		    const odp_init_t *params,
		    const odp_platform_init_t *platform_params ODP_UNUSED)
{
	enum init_stage stage = NO_INIT;

	odp_global_data.log_fn = odp_override_log;
	odp_global_data.abort_fn = odp_override_abort;
	odp_global_data.n_rx_thr = DEF_N_RX_THR;
	odp_global_data.enable_pkt_nofree = 0;
	odp_global_data.sort_buffers = 0;
	if (params != NULL) {
		if (params->log_fn != NULL)
			odp_global_data.log_fn = params->log_fn;
		if (params->abort_fn != NULL)
			odp_global_data.abort_fn = params->abort_fn;
	}

	if (platform_params != NULL) {
		if (platform_params->n_rx_thr) {
			odp_global_data.n_rx_thr =
				platform_params->n_rx_thr > MAX_RX_THR ?
				MAX_RX_THR : platform_params->n_rx_thr;
		}
		if (platform_params->sort_buffers)
			odp_global_data.sort_buffers = 1;
		odp_global_data.enable_pkt_nofree =
			platform_params->enable_pkt_nofree;
	}
	if (odp_global_data.n_rx_thr == 1)
		odp_global_data.sort_buffers = 1;

	if (odp_time_global_init()) {
		ODP_ERR("ODP time init failed.\n");
		goto init_fail;
	}
	stage = TIME_INIT;

	if (odp_system_info_init()) {
		ODP_ERR("ODP system_info init failed.\n");
		goto init_fail;
	}
	stage = SYSINFO_INIT;

	if (odp_shm_init_global()) {
		ODP_ERR("ODP shm init failed.\n");
		goto init_fail;
	}
	stage = SHM_INIT;

	if (odp_thread_init_global()) {
		ODP_ERR("ODP thread init failed.\n");
		goto init_fail;
	}
	stage = THREAD_INIT;

	if (odp_pool_init_global()) {
		ODP_ERR("ODP pool init failed.\n");
		goto init_fail;
	}
	stage = POOL_INIT;

	if (odp_queue_init_global()) {
		ODP_ERR("ODP queue init failed.\n");
		goto init_fail;
	}
	stage = QUEUE_INIT;

	if (sched_fn->init_global()) {
		ODP_ERR("ODP schedule init failed.\n");
		goto init_fail;
	}
	stage = SCHED_INIT;

	if (mppa_rpc_odp_client_init()) {
		ODP_ERR("ODP RPC init failed.\n");
		goto init_fail;
	}
	stage = RPC_INIT;

	if (odp_pktio_init_global()) {
		ODP_ERR("ODP packet io init failed.\n");
		goto init_fail;
	}
	stage = PKTIO_INIT;

	if (odp_timer_init_global()) {
		ODP_ERR("ODP timer init failed.\n");
		goto init_fail;
	}
	stage = TIMER_INIT;

#ifndef NO_CRYPTO
	if (odp_crypto_init_global()) {
		ODP_ERR("ODP crypto init failed.\n");
		goto init_fail;
	}
	stage = CRYPTO_INIT;
#endif

	if (odp_classification_init_global()) {
		ODP_ERR("ODP classification init failed.\n");
		goto init_fail;
	}
	stage = CLASSIFICATION_INIT;

	stage = ALL_INIT;
	/* Dummy support for single instance */
	*instance = INSTANCE_ID;

	return 0;

init_fail:
	_odp_term_global(stage);
	return -1;
}

int odp_term_global(odp_instance_t instance)
{
	if (instance != INSTANCE_ID) {
		ODP_ERR("Bad instance.\n");
		return -1;
	}
	return _odp_term_global(ALL_INIT);
}

static int _odp_term_local(enum init_stage stage)
{
	int rc = 0;
	int rc_thd = 0;

	switch (stage) {
	case ALL_INIT:

	case SCHED_INIT:
		if (sched_fn->term_local()) {
			ODP_ERR("ODP schedule local term failed.\n");
			rc = -1;
		}
		/* Fall through */

	case POOL_INIT:
		if (odp_pool_term_local()) {
			ODP_ERR("ODP buffer pool local term failed.\n");
			rc = -1;
		}
		/* Fall through */

	case THREAD_INIT:
		rc_thd = odp_thread_term_local();
		if (rc_thd < 0) {
			ODP_ERR("ODP thread local term failed.\n");
			rc = -1;
		} else {
			if (!rc)
				rc = rc_thd;
		}
		/* Fall through */

	default:
		break;
	}

	return rc;
}

int odp_init_local(odp_instance_t instance, odp_thread_type_t thr_type)
{
	enum init_stage stage = NO_INIT;

	if (instance != INSTANCE_ID) {
		ODP_ERR("Bad instance.\n");
		goto init_fail;
	}

	if (odp_shm_init_local()) {
		ODP_ERR("ODP shm local init failed.\n");
		goto init_fail;
	}
	stage = SHM_INIT;

	if (odp_thread_init_local(thr_type)) {
		ODP_ERR("ODP thread local init failed.\n");
		goto init_fail;
	}
	stage = THREAD_INIT;

	if (odp_pktio_init_local()) {
		ODP_ERR("ODP packet io local init failed.\n");
		goto init_fail;
	}
	stage = PKTIO_INIT;

	if (odp_pool_init_local()) {
		ODP_ERR("ODP pool local init failed.\n");
		goto init_fail;
	}
	stage = POOL_INIT;

	if (sched_fn->init_local()) {
		ODP_ERR("ODP schedule local init failed.\n");
		goto init_fail;
	}
	stage = SCHED_INIT;

	return 0;

init_fail:
	_odp_term_local(stage);
	return -1;
}

int odp_term_local(void)
{
	return _odp_term_local(ALL_INIT);
}
