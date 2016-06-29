#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <HAL/hal/hal.h>

#include <odp/rpc/rpc.h>
#include <stdio.h>
#include <mppa_noc.h>

#include "internal/rpc-server.h"

#define C2C_RPC_ERR_MSG(answer, x...)			\
	do {						\
		RPC_ERROR(answer, "[C2C]", ##x);	\
	} while(0)

typedef struct {
	uint8_t opened     : 1;
	uint8_t rx_enabled : 1;
	uint8_t tx_enabled : 1;
	uint8_t min_rx     : 8;
	uint8_t max_rx     : 8;
	uint8_t cnoc_rx    : 8;
	uint16_t rx_size   :16;
} c2c_status_t;

static c2c_status_t c2c_status[RPC_MAX_CLIENTS][RPC_MAX_CLIENTS];

void c2c_open(unsigned src_cluster, mppa_rpc_odp_t *msg,
			mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_c2c_open_t data = { .inl_data = msg->inl_data };
	const unsigned dst_cluster = data.cluster_id;

	if (c2c_status[src_cluster][dst_cluster].opened){
		C2C_RPC_ERR_MSG(answer, "Cluster2Cluster link %d => %d is already opened\n",
				src_cluster, dst_cluster);
		return;
	}

	c2c_status[src_cluster][dst_cluster].opened = 1;
	c2c_status[src_cluster][dst_cluster].rx_enabled = data.rx_enabled;
	c2c_status[src_cluster][dst_cluster].tx_enabled = data.tx_enabled;
	c2c_status[src_cluster][dst_cluster].min_rx = data.min_rx;
	c2c_status[src_cluster][dst_cluster].max_rx = data.max_rx;
	c2c_status[src_cluster][dst_cluster].rx_size = data.mtu;
	c2c_status[src_cluster][dst_cluster].cnoc_rx = data.cnoc_rx;
	return;
}

void c2c_close(unsigned src_cluster, mppa_rpc_odp_t *msg,
	       mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_c2c_clos_t data = { .inl_data = msg->inl_data };
	const unsigned dst_cluster = data.cluster_id;

	if (!c2c_status[src_cluster][dst_cluster].opened){
		C2C_RPC_ERR_MSG(answer, "Cluster2Cluster link %d => %d is not open\n",
				src_cluster, dst_cluster);
		return;
	}

	memset(&c2c_status[src_cluster][dst_cluster], 0, sizeof(c2c_status_t));
	return;
}

void c2c_query(unsigned src_cluster, mppa_rpc_odp_t *msg,
	       mppa_rpc_odp_answer_t *answer)
{
	mppa_rpc_odp_cmd_c2c_query_t data = { .inl_data = msg->inl_data };
	const unsigned dst_cluster = data.cluster_id;

	const c2c_status_t * s2d = &c2c_status[src_cluster][dst_cluster];
	const c2c_status_t * d2s = &c2c_status[dst_cluster][src_cluster];

	if (!s2d->opened || !d2s->opened){
		answer->ack.status = 1;
		answer->ack.cmd.c2c_query.closed = 1;
		return;
	}

	if (!s2d->tx_enabled || !d2s->rx_enabled) {
		answer->ack.status = 1;
		answer->ack.cmd.c2c_query.eacces = 1;
		return;
	}
	answer->ack.cmd.c2c_query.mtu = d2s->rx_size;
	if (s2d->rx_size < answer->ack.cmd.c2c_query.mtu)
		answer->ack.cmd.c2c_query.mtu = s2d->rx_size;

	answer->ack.cmd.c2c_query.min_rx = d2s->min_rx;
	answer->ack.cmd.c2c_query.max_rx = d2s->max_rx;
	answer->ack.cmd.c2c_query.cnoc_rx = d2s->cnoc_rx;
	return;
}

static int c2c_rpc_handler(unsigned remoteClus, mppa_rpc_odp_t *msg, uint8_t *payload)
{
	mppa_rpc_odp_answer_t answer = MPPA_RPC_ODP_ANSWER_INITIALIZER(msg);

	if (msg->pkt_class != MPPA_RPC_ODP_CLASS_C2C)
		return -MPPA_RPC_ODP_ERR_INTERNAL_ERROR;
	if (msg->cos_version != MPPA_RPC_ODP_C2C_VERSION)
		return -MPPA_RPC_ODP_ERR_VERSION_MISMATCH;

	(void)payload;
	switch (msg->pkt_subtype){
	case MPPA_RPC_ODP_CMD_C2C_OPEN:
		c2c_open(remoteClus, msg, &answer);
		break;
	case MPPA_RPC_ODP_CMD_C2C_CLOS:
		c2c_close(remoteClus, msg, &answer);
		break;
	case MPPA_RPC_ODP_CMD_C2C_QUERY:
		c2c_query(remoteClus, msg, &answer);
		break;
	default:
		return -MPPA_RPC_ODP_ERR_BAD_SUBTYPE;
	}
	mppa_rpc_odp_server_ack(&answer);
	return -MPPA_RPC_ODP_ERR_NONE;
}

void  __attribute__ ((constructor)) __c2c_rpc_constructor()
{
	__rpc_handlers[MPPA_RPC_ODP_CLASS_C2C] = c2c_rpc_handler;
}
