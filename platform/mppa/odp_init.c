/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/init.h>
#include <odp_internal.h>
#include <odp/debug.h>
#include <odp_debug_internal.h>
#include <odp_rpc_internal.h>
#include <odp_rx_internal.h>
#include <errno.h>

#include <HAL/hal/hal.h>

struct odp_global_data_s odp_global_data;
static int cluster_iopcie_sync(int exit)
{
	unsigned cluster_id = __k1_get_cluster_id();
	odp_rpc_t *ack_msg;
	odp_rpc_t cmd = {
		.pkt_type = exit ? ODP_RPC_CMD_BAS_EXIT : ODP_RPC_CMD_BAS_SYNC,
		.data_len = 0,
		.flags = 0,
	};
	odp_rpc_cmd_ack_t ack;
	const unsigned int rpc_server_id = odp_rpc_client_get_default_server();
	uint64_t timeout = 120 * RPC_TIMEOUT_1S;
	int iter = 0;
	do {
		odp_rpc_do_query(odp_rpc_get_ioddr_dma_id(rpc_server_id, cluster_id),
				 odp_rpc_get_ioddr_tag_id(rpc_server_id, cluster_id),
				 &cmd, NULL);

		if (odp_rpc_wait_ack(&ack_msg, NULL, timeout) != 1) {
			printf("Timeout %d\n", iter);
			return -1;
		}
		/* We had an answer. No need to timeout anymore */
		timeout = 3600 * RPC_TIMEOUT_1S;
		ack.inl_data = ack_msg->inl_data;
		iter++;
	} while(ack.status == EAGAIN);

	if(ack.status != 0)
		printf("Bad ret %d\n", ack.status);
	return ack.status;
}

int odp_init_global(const odp_init_t *params,
		    const odp_platform_init_t *platform_params ODP_UNUSED)
{
	odp_global_data.log_fn = odp_override_log;
	odp_global_data.abort_fn = odp_override_abort;
	odp_global_data.n_rx_thr = DEF_N_RX_THR;

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
	}

	if (odp_time_global_init()) {
		ODP_ERR("ODP time init failed.\n");
		return -1;
	}

	if (odp_system_info_init()) {
		ODP_ERR("ODP system_info init failed.\n");
		return -1;
	}

	if (odp_shm_init_global()) {
		ODP_ERR("ODP shm init failed.\n");
		return -1;
	}

	if (odp_thread_init_global()) {
		ODP_ERR("ODP thread init failed.\n");
		return -1;
	}

	if (odp_pool_init_global()) {
		ODP_ERR("ODP pool init failed.\n");
		return -1;
	}

	if (odp_queue_init_global()) {
		ODP_ERR("ODP queue init failed.\n");
		return -1;
	}

	if (odp_schedule_init_global()) {
		ODP_ERR("ODP schedule init failed.\n");
		return -1;
	}

	if (odp_rpc_client_init()) {
		ODP_ERR("ODP RPC init failed.\n");
		return -1;
	}

	/* We need to sync only when spawning from another IO */
	if (__k1_spawn_type() == __MPPA_MPPA_SPAWN) {
		if (cluster_iopcie_sync(0)) {
			ODP_ERR("ODP failed to sync with boot cluster.\n");
			return -1;
		}
	}

	if (odp_pktio_init_global()) {
		ODP_ERR("ODP packet io init failed.\n");
		return -1;
	}

	if (odp_timer_init_global()) {
		ODP_ERR("ODP timer init failed.\n");
		return -1;
	}

#ifndef NO_CRYPTO
	if (odp_crypto_init_global()) {
		ODP_ERR("ODP crypto init failed.\n");
		return -1;
	}
#endif

	if (odp_classification_init_global()) {
		ODP_ERR("ODP classification init failed.\n");
		return -1;
	}

	return 0;
}

int odp_term_global(void)
{
	int rc = 0;

	if (odp_classification_term_global()) {
		ODP_ERR("ODP classificatio term failed.\n");
		rc = -1;
	}

#ifndef NO_CRYPTO
	if (odp_crypto_term_global()) {
		ODP_ERR("ODP crypto term failed.\n");
		rc = -1;
	}
#endif
	/* We need to sync only when spawning from another IO */
	if (__k1_spawn_type() == __MPPA_MPPA_SPAWN)
		cluster_iopcie_sync(1);

	if (odp_rpc_client_term()) {
		ODP_ERR("ODP RPC tem failed.\n");
		return -1;
	}

	if (odp_pktio_term_global()) {
		ODP_ERR("ODP pktio term failed.\n");
		rc = -1;
	}

	if (odp_schedule_term_global()) {
		ODP_ERR("ODP schedule term failed.\n");
		rc = -1;
	}

	if (odp_queue_term_global()) {
		ODP_ERR("ODP queue term failed.\n");
		rc = -1;
	}

	if (odp_pool_term_global()) {
		ODP_ERR("ODP buffer pool term failed.\n");
		rc = -1;
	}

	if (odp_thread_term_global()) {
		ODP_ERR("ODP thread term failed.\n");
		rc = -1;
	}

	if (odp_shm_term_global()) {
		ODP_ERR("ODP shm term failed.\n");
		rc = -1;
	}

	return rc;
}

int odp_init_local(odp_thread_type_t thr_type)
{
	if (odp_shm_init_local()) {
		ODP_ERR("ODP shm local init failed.\n");
		return -1;
	}

	if (odp_thread_init_local(thr_type)) {
		ODP_ERR("ODP thread local init failed.\n");
		return -1;
	}

	if (odp_pktio_init_local()) {
		ODP_ERR("ODP packet io local init failed.\n");
		return -1;
	}

	if (odp_schedule_init_local()) {
		ODP_ERR("ODP schedule local init failed.\n");
		return -1;
	}

	return 0;
}

int odp_term_local(void)
{
	int rc = 0;
	int rc_thd = 0;

	if (odp_schedule_term_local()) {
		ODP_ERR("ODP schedule local term failed.\n");
		rc = -1;
	}

	if (odp_pool_term_local()) {
		ODP_ERR("ODP buffer pool local term failed.\n");
		rc = -1;
	}

	rc_thd = odp_thread_term_local();
	if (rc_thd < 0) {
		ODP_ERR("ODP thread local term failed.\n");
		rc = -1;
	} else {
		if (!rc)
			rc = rc_thd;
	}

	return rc;
}
