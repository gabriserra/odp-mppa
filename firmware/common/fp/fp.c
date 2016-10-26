#include <string.h>
#include <inttypes.h>
#include <HAL/hal/hal.h>
#include <odp/rpc/rpc.h>
#include <odp/rpc/api.h>
#include <mppa_noc.h>
#include <stdio.h>

#include "rpc-server.h"
#include "internal/rpc-server.h"

/* command buffers */
static char cmd[BSP_NB_CLUSTER_MAX][BUFSIZ];

static int
odp_mppa_rpc_handler(unsigned int remoteClus, mppa_rpc_odp_t *msg, uint8_t *payload)
{
	mppa_rpc_odp_answer_t answer = MPPA_RPC_ODP_ANSWER_INITIALIZER(msg);
	unsigned int interface;

	(void)payload;
	if (msg->pkt_class != MPPA_RPC_ODP_CLASS_FP)
		return -MPPA_RPC_ODP_ERR_INTERNAL_ERROR;
	if (msg->cos_version != MPPA_RPC_ODP_FP_VERSION)
		return -MPPA_RPC_ODP_ERR_VERSION_MISMATCH;

	switch (msg->pkt_subtype) {
	case MPPA_RPC_ODP_CMD_FP_CLI:
		__builtin_k1_dinval();
		if (cmd[remoteClus][0] == '\0') {
			answer.ack.status = 0;
			mppa_rpc_odp_server_ack(&answer);
			return 0;
		}

		/* null terminate for display and strtol */
		cmd[remoteClus][sizeof(cmd[remoteClus])-1] = '\0';
		msg->ack = 1;
		msg->data_len = strlen(cmd[remoteClus])+1;
		answer.ack.status = 1;
		answer.msg->inl_data = answer.ack.inl_data;
		interface = get_rpc_local_dma_id(msg->dma_id);

		mppa_rpc_odp_send_msg(interface, msg->dma_id, msg->dnoc_tag, msg,
				      &cmd[remoteClus]);
		memset(&cmd[remoteClus], 0, sizeof(cmd[remoteClus]));
		return 0;
	default:
		return -MPPA_RPC_ODP_ERR_BAD_SUBTYPE;
	}

	return -1;
}

static void  __attribute__ ((constructor))
__fp_rpc_constructor(void)
{
	__rpc_handlers[MPPA_RPC_ODP_CLASS_FP] = odp_mppa_rpc_handler;
}
