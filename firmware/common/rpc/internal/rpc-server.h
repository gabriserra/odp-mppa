#ifndef RPC_SERVER_INTERNAL__H
#define RPC_SERVER_INTERNAL__H

#define MAX_RPC_HANDLERS 32
#define RPC_MAX_CLIENTS (BSP_NB_CLUSTER_MAX + BSP_NB_IOCLUSTER_MAX * 4)

typedef int (*odp_rpc_handler_t)(unsigned remoteClus, odp_rpc_t *msg, uint8_t *payload);

int odp_rpc_server_ack(odp_rpc_t * msg, odp_rpc_ack_t ack,
		       const uint8_t *payload, uint16_t payload_len);

/** Global structure for modules to register their handlers */
extern odp_rpc_handler_t __rpc_handlers[ODP_RPC_N_CLASS];

static inline int get_rpc_tag_id(unsigned cluster_id)
{
	return odp_rpc_get_io_tag_id(cluster_id);
}

static inline int get_rpc_local_dma_id(unsigned cluster_id)
{
	int if_id = odp_rpc_get_io_dma_id(0, cluster_id) - 160;
	/* On K1B, DMA 0-3 belong to IODDR */
	if_id += 4;

	return if_id;
}


#endif /* RPC_SERVER_INTERNAL__H */
