/* Copyright 2016 6WIND S.A. */

/* Inspired from odp firmware main.c */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <HAL/hal/hal.h>
#include <unistd.h>
#include <string.h>

#include <mppa_noc.h>

#include <odp/rpc/api.h>
#include "internal/rpc-server.h"
#include "rpc-server.h"
#include "boot/boot.h"

/* command buffers */
static char cmd[BSP_NB_CLUSTER_MAX][BUFSIZ];

int
main(int argc, char *const argv[])
{

	int ret;
	unsigned int clusters = 0;
	unsigned int n_clusters = 0;
	int opt;

	while ((opt = getopt(argc, argv, "c:h")) != -1) {
		switch (opt) {
		case 'c':
			{
				unsigned int mask = 1 << atoi(optarg);
				if ((clusters & mask) == 0)
					n_clusters++;
				clusters |= mask;
			}
			break;
		case 'h':
			printf("Usage: %s [ -c <clus_id> -c <clus_id> -c ... ]",
				argv[0]);
			exit(0);
			break;
		default: /* '?' */
			fprintf(stderr, "Wrong arguments\n");
			return -1;
		}
	}
	if (clusters == 0) {
		clusters = 0xffff;
		n_clusters = 16;
	}

	ret = odp_rpc_server_start();
	if (ret) {
		fprintf(stderr, "[RPC] Error: Failed to start server\n");
		exit(EXIT_FAILURE);
	}


	return odp_rpc_server_thread();
}

static int
odp_mppa_rpc_handler(unsigned int remoteClus, mppa_rpc_odp_t *msg, uint8_t *payload)
{
	mppa_rpc_odp_answer_t answer = MPPA_RPC_ODP_ANSWER_INITIALIZER(msg);
	unsigned int interface;

	(void)payload;
	if (msg->pkt_class != MPPA_RPC_ODP_CLASS_FP) {
		fprintf(stderr, "[RPC-FP] Message is not of class FP\n");
		return -MPPA_RPC_ODP_ERR_INTERNAL_ERROR;
	}

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
		fprintf(stderr, "[RPC-FP] Unhandled message type\n");
		return -MPPA_RPC_ODP_ERR_BAD_SUBTYPE;
	}

	return -1;
}

static void  __attribute__ ((constructor))
odp_mppa_rpc_init(void)
{
#if defined(MAGIC_SCALL)
	return;
#endif
	__rpc_handlers[MPPA_RPC_ODP_CLASS_FP] = odp_mppa_rpc_handler;
}
