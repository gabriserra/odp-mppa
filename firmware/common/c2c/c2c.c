#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <HAL/hal/hal.h>

#include <odp/rpc/rpc.h>
#include <stdio.h>
#include <mppa_noc.h>

#include "internal/rpc-server.h"

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

odp_rpc_ack_t  c2c_open(unsigned src_cluster, odp_rpc_t *msg)
{
	odp_rpc_ack_t ack = { .status = 0};
	odp_rpc_cmd_c2c_open_t data = { .inl_data = msg->inl_data };
	const unsigned dst_cluster = data.cluster_id;

	if (c2c_status[src_cluster][dst_cluster].opened){
		fprintf(stderr, "[C2C] Error: %d => %d is already opened\n",
			src_cluster, dst_cluster);
		goto err;
	}

	c2c_status[src_cluster][dst_cluster].opened = 1;
	c2c_status[src_cluster][dst_cluster].rx_enabled = data.rx_enabled;
	c2c_status[src_cluster][dst_cluster].tx_enabled = data.tx_enabled;
	c2c_status[src_cluster][dst_cluster].min_rx = data.min_rx;
	c2c_status[src_cluster][dst_cluster].max_rx = data.max_rx;
	c2c_status[src_cluster][dst_cluster].rx_size = data.mtu;
	c2c_status[src_cluster][dst_cluster].cnoc_rx = data.cnoc_rx;
	return ack;

 err:
	ack.status = 1;
	return ack;
}

odp_rpc_ack_t  c2c_close(unsigned src_cluster, odp_rpc_t *msg)
{
	odp_rpc_ack_t ack = { .status = 0};
	odp_rpc_cmd_c2c_clos_t data = { .inl_data = msg->inl_data };
	const unsigned dst_cluster = data.cluster_id;

	if (!c2c_status[src_cluster][dst_cluster].opened){
		fprintf(stderr, "[C2C] Error: %d => %d is not open\n",
			src_cluster, dst_cluster);
		goto err;
	}

	memset(&c2c_status[src_cluster][dst_cluster], 0, sizeof(c2c_status_t));
	return ack;

 err:
	ack.status = 1;
	return ack;
}

odp_rpc_ack_t  c2c_query(unsigned src_cluster, odp_rpc_t *msg)
{
	odp_rpc_ack_t ack = { .status = 0};
	odp_rpc_cmd_c2c_query_t data = { .inl_data = msg->inl_data };
	const unsigned dst_cluster = data.cluster_id;

	const c2c_status_t * s2d = &c2c_status[src_cluster][dst_cluster];
	const c2c_status_t * d2s = &c2c_status[dst_cluster][src_cluster];

	if (!s2d->opened || !d2s->opened){
		ack.status = 1;
		ack.cmd.c2c_query.closed = 1;
		return ack;
	}

	if (!s2d->tx_enabled || !d2s->rx_enabled) {
		ack.status = 1;
		ack.cmd.c2c_query.eacces = 1;
		return ack;
	}
	ack.cmd.c2c_query.mtu = d2s->rx_size;
	if (s2d->rx_size < ack.cmd.c2c_query.mtu)
		ack.cmd.c2c_query.mtu = s2d->rx_size;

	ack.cmd.c2c_query.min_rx = d2s->min_rx;
	ack.cmd.c2c_query.max_rx = d2s->max_rx;
	ack.cmd.c2c_query.cnoc_rx = d2s->cnoc_rx;
	return ack;
}

static int c2c_rpc_handler(unsigned remoteClus, odp_rpc_t *msg, uint8_t *payload)
{
	odp_rpc_ack_t ack = ODP_RPC_CMD_ACK_INITIALIZER;

	if (msg->pkt_class != ODP_RPC_CLASS_C2C)
		return -ODP_RPC_ERR_INTERNAL_ERROR;
	if (msg->cos_version != ODP_RPC_C2C_VERSION)
		return -ODP_RPC_ERR_VERSION_MISMATCH;

	(void)payload;
	switch (msg->pkt_subtype){
	case ODP_RPC_CMD_C2C_OPEN:
		ack = c2c_open(remoteClus, msg);
		break;
	case ODP_RPC_CMD_C2C_CLOS:
		ack = c2c_close(remoteClus, msg);
		break;
	case ODP_RPC_CMD_C2C_QUERY:
		ack = c2c_query(remoteClus, msg);
		break;
	default:
		return -ODP_RPC_ERR_BAD_SUBTYPE;
	}
	odp_rpc_server_ack(msg, ack, NULL, 0);
	return -ODP_RPC_ERR_NONE;
}

void  __attribute__ ((constructor)) __c2c_rpc_constructor()
{
	__rpc_handlers[ODP_RPC_CLASS_C2C] = c2c_rpc_handler;
}
